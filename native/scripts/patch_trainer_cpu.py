#!/usr/bin/env python3
"""Add a real CPU backend to the official Fairy NNUE trainer.

Upstream uses CuPy CUDA kernels and explicitly moves the model to CUDA. This
patch keeps the CUDA path intact and adds a differentiable pure-PyTorch sparse
accumulator for CPU-only GitHub runners.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one marker, found {count}")
    return text.replace(old, new, 1)


def patch_feature_transformer(path: Path) -> None:
    s = path.read_text(encoding="utf-8")
    s = replace_once(
        s,
        "import cupy as cp\n",
        "try:\n    import cupy as cp\nexcept ImportError:\n    cp = None\n",
        "optional cupy import",
    )
    helper = '''\ndef _feature_transformer_slice_cpu(feature_indices, feature_values, weight, bias):
    """Sparse feature accumulation using regular PyTorch autograd on CPU."""
    if feature_indices.ndim != 2 or feature_values.shape != feature_indices.shape:
        raise ValueError("feature indices/values must have the same [batch, active] shape")
    valid = feature_indices >= 0
    safe_indices = feature_indices.clamp_min(0).to(dtype=torch.long)
    safe_values = torch.where(valid, feature_values, torch.zeros_like(feature_values))
    gathered = weight.index_select(0, safe_indices.reshape(-1))
    gathered = gathered.reshape(feature_indices.shape[0], feature_indices.shape[1], weight.shape[1])
    return bias.unsqueeze(0) + (gathered * safe_values.unsqueeze(-1)).sum(dim=1)

'''
    marker = "class FeatureTransformerSliceFunction(autograd.Function):\n"
    if helper not in s:
        s = replace_once(s, marker, helper + marker, "CPU sparse helper")

    old = """    def forward(self, feature_indices, feature_values):
        return FeatureTransformerSliceFunction.apply(feature_indices, feature_values, self.weight, self.bias)
"""
    new = """    def forward(self, feature_indices, feature_values):
        if feature_indices.is_cuda:
            if cp is None:
                raise RuntimeError("CuPy is required for CUDA NNUE training")
            return FeatureTransformerSliceFunction.apply(feature_indices, feature_values, self.weight, self.bias)
        return _feature_transformer_slice_cpu(feature_indices, feature_values, self.weight, self.bias)
"""
    s = replace_once(s, old, new, "single feature transformer CPU branch")

    old = """    def forward(self, feature_indices_0, feature_values_0, feature_indices_1, feature_values_1):
        return DoubleFeatureTransformerSliceFunction.apply(feature_indices_0, feature_values_0, feature_indices_1, feature_values_1, self.weight, self.bias)
"""
    new = """    def forward(self, feature_indices_0, feature_values_0, feature_indices_1, feature_values_1):
        if feature_indices_0.is_cuda:
            if cp is None:
                raise RuntimeError("CuPy is required for CUDA NNUE training")
            return DoubleFeatureTransformerSliceFunction.apply(feature_indices_0, feature_values_0, feature_indices_1, feature_values_1, self.weight, self.bias)
        return (
            _feature_transformer_slice_cpu(feature_indices_0, feature_values_0, self.weight, self.bias),
            _feature_transformer_slice_cpu(feature_indices_1, feature_values_1, self.weight, self.bias),
        )
"""
    s = replace_once(s, old, new, "double feature transformer CPU branch")
    path.write_text(s, encoding="utf-8", newline="\n")


def patch_dataset(path: Path) -> None:
    s = path.read_text(encoding="utf-8")
    pattern = re.compile(r"    def get_tensors\(self, device\):\n.*?        return us, them, white_indices, white_values, black_indices, black_values, outcome, score, psqt_indices, layer_stack_indices\n", re.S)
    replacement = '''    def get_tensors(self, device):
        # The C++ loader owns the underlying buffers. On CPU, .to('cpu') would
        # alias those buffers, which are freed immediately after this method.
        # clone() is therefore mandatory before destroy_sparse_batch().
        use_cuda = str(device).startswith('cuda')

        def tensor(ptr, shape, *, dtype=None):
            value = torch.from_numpy(np.ctypeslib.as_array(ptr, shape=shape)).clone()
            if dtype is not None:
                value = value.to(dtype=dtype)
            if use_cuda:
                value = value.pin_memory().to(device=device, non_blocking=True)
            else:
                value = value.to(device=device)
            return value

        white_values = tensor(self.white_values, (self.size, self.max_active_features))
        black_values = tensor(self.black_values, (self.size, self.max_active_features))
        white_indices = tensor(self.white, (self.size, self.max_active_features))
        black_indices = tensor(self.black, (self.size, self.max_active_features))
        us = tensor(self.is_white, (self.size, 1))
        them = 1.0 - us
        outcome = tensor(self.outcome, (self.size, 1))
        score = tensor(self.score, (self.size, 1))
        psqt_indices = tensor(self.psqt_indices, (self.size,), dtype=torch.long)
        layer_stack_indices = tensor(self.layer_stack_indices, (self.size,), dtype=torch.long)
        return us, them, white_indices, white_values, black_indices, black_values, outcome, score, psqt_indices, layer_stack_indices
'''
    if "clone() is therefore mandatory" not in s:
        s, count = pattern.subn(replacement, s, count=1)
        if count != 1:
            raise RuntimeError(f"nnue_dataset.py: get_tensors replacement count={count}")
    path.write_text(s, encoding="utf-8", newline="\n")


def patch_train(path: Path) -> None:
    s = path.read_text(encoding="utf-8")
    s = s.replace("    nnue.cuda()\n", "")
    s = replace_once(
        s,
        "  if batch_size <= 0:\n    batch_size = 16384\n",
        "  if batch_size <= 0:\n    batch_size = 16384 if torch.cuda.is_available() else 128\n",
        "CPU batch-size default",
    )
    s = replace_once(
        s,
        "  main_device = trainer.strategy.root_device if trainer.strategy.root_device.index is None else 'cuda:' + str(trainer.strategy.root_device.index)\n",
        "  main_device = str(trainer.strategy.root_device)\n",
        "trainer root device",
    )
    path.write_text(s, encoding="utf-8", newline="\n")


def add_cpu_self_test(root: Path) -> None:
    path = root / "test_cpu_backend.py"
    path.write_text('''import torch\nfrom feature_transformer import DoubleFeatureTransformerSlice\n\ntorch.manual_seed(7)\nlayer = DoubleFeatureTransformerSlice(32, 16)\nidx0 = torch.tensor([[1, 3, 5, -1], [2, 2, 9, -1]], dtype=torch.int32)\nidx1 = torch.tensor([[4, 6, -1, -1], [7, 8, 10, 11]], dtype=torch.int32)\nval0 = torch.tensor([[1.0, 1.0, 0.5, 0.0], [1.0, 0.5, 1.0, 0.0]], dtype=torch.float32)\nval1 = torch.ones((2, 4), dtype=torch.float32)\na, b = layer(idx0, val0, idx1, val1)\nloss = (a.square().mean() + b.square().mean())\nloss.backward()\nassert a.shape == (2, 16)\nassert layer.weight.grad is not None\nassert torch.isfinite(layer.weight.grad).all()\nprint('CPU feature-transformer forward/backward passed')\n''', encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkout", type=Path)
    args = parser.parse_args()
    root = args.checkout.resolve()
    patch_feature_transformer(root / "feature_transformer.py")
    patch_dataset(root / "nnue_dataset.py")
    patch_train(root / "train.py")
    add_cpu_self_test(root)
    print("Patched official Fairy NNUE trainer with CPU backend.")


if __name__ == "__main__":
    main()
