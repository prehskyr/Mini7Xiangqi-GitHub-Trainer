#!/usr/bin/env python3
"""Add wall-clock stopping and one-shot checkpoint saving to Fairy NNUE train.py.

Apply this after patch_trainer_cpu.py. Timed runs do not create periodic model
checkpoints. The timer stops at a training-step boundary, then train.py writes one
resumable checkpoint after trainer.fit() returns.
"""
from __future__ import annotations

import argparse
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one marker, found {count}")
    return text.replace(old, new, 1)


def patch_train(path: Path) -> None:
    s = path.read_text(encoding="utf-8")

    s = replace_once(
        s,
        "import argparse\n",
        "import argparse\nfrom datetime import timedelta\n",
        "timed trainer import",
    )

    timer_class = """class FreshWallClockTimer(pl.callbacks.Timer):
  # Never restore elapsed time from an older checkpoint.
  def __init__(self, seconds):
    super().__init__(duration=timedelta(seconds=seconds), interval='step', verbose=True)

  def state_dict(self):
    # Every workflow run receives a fresh time budget.
    return {}

  def load_state_dict(self, state_dict):
    # Ignore elapsed time from a previous run if an old checkpoint contains it.
    return None


"""
    if timer_class not in s:
        s = replace_once(
            s,
            "def main():\n",
            timer_class + "def main():\n",
            "wall-clock timer class",
        )

    parser_marker = (
        "  parser.add_argument(\"--validation-size\", type=int, default=1000000, "
        "dest='validation_size', help=\"Number of positions per validation step.\")\n"
    )
    parser_block = parser_marker + (
        "  parser.add_argument(\"--max-wall-seconds\", type=int, default=0, "
        "dest='max_wall_seconds', help=\"Stop training after this many wall-clock seconds; 0 disables the timer.\")\n"
        "  parser.add_argument(\"--final-checkpoint\", dest='final_checkpoint', "
        "help=\"Write one resumable checkpoint after fit returns.\")\n"
    )
    s = replace_once(s, parser_marker, parser_block, "timer arguments")

    old_callbacks = (
        "  checkpoint_callback = pl.callbacks.ModelCheckpoint(save_last=True, every_n_epochs=1, save_top_k=-1)\n"
        "  trainer = pl.Trainer.from_argparse_args(args, callbacks=[checkpoint_callback], logger=tb_logger)\n"
    )
    new_callbacks = """  callbacks = []
  trainer_overrides = {}
  if args.max_wall_seconds > 0:
    callbacks.append(FreshWallClockTimer(args.max_wall_seconds))
    # No periodic model writes during a timed run.
    trainer_overrides['enable_checkpointing'] = False
  else:
    callbacks.append(pl.callbacks.ModelCheckpoint(save_last=True, every_n_epochs=1, save_top_k=-1))

  trainer = pl.Trainer.from_argparse_args(
      args, callbacks=callbacks, logger=tb_logger, **trainer_overrides
  )
"""
    s = replace_once(s, old_callbacks, new_callbacks, "one-shot checkpoint mode")

    old_fit = "  trainer.fit(nnue, train, val)\n"
    new_fit = """  trainer.fit(nnue, train, val)

  if args.final_checkpoint:
    checkpoint_path = os.path.abspath(args.final_checkpoint)
    parent = os.path.dirname(checkpoint_path)
    if parent:
      os.makedirs(parent, exist_ok=True)
    trainer.save_checkpoint(checkpoint_path)
    print('Saved final timed checkpoint to {}'.format(checkpoint_path), flush=True)
"""
    s = replace_once(s, old_fit, new_fit, "final timed checkpoint")

    path.write_text(s, encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkout", type=Path)
    args = parser.parse_args()
    root = args.checkout.resolve()
    patch_train(root / "train.py")
    print("Patched Fairy NNUE trainer with wall-clock timing and one-shot saving.")


if __name__ == "__main__":
    main()
