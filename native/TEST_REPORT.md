# v0.1.0 test report

## Locally executed successfully

1. Linux build of the modified native engine:

   ```text
   make -j2 ARCH=x86-64-modern COMP=gcc largeboards=yes nnue=no build
   ```

2. UCI registration of `mini7xiangqi`.
3. Initial-position `perft 1 = 19`.
4. The test sequence below terminates after Red's third consecutive rook-checking turn, with the side that delivered it losing:

   ```text
   position fen 3k3/7/R6/7/7/7/4K2 w - - 0 1 moves a5d5 d7c7 d5c5 c7d7 c5d5
   go depth 1
   ```

5. The engine exposes `Use NNUE` and external `EvalFile` options.
6. `patch_tools_engine.py` is idempotent on the supplied Fairy-Stockfish source layout.
7. The added pure-PyTorch CPU sparse feature-transformer completed forward and backward propagation with finite gradients.
8. All Python scripts pass `py_compile`; all workflow YAML files parse successfully.

## Deliberately not claimed yet

The following require the first GitHub Actions executions and are not represented as already proven:

- compilation of the pinned `variant-nnue-tools` checkout after patching;
- generation of valid `.bin` training shards on GitHub;
- full official data-loader compilation against generated Mini7 variant configuration;
- completion speed of one CPU epoch on the hosted runner;
- serialization and engine loading of the first trained `.nnue`;
- Elo improvement over the classical evaluator or a previous accepted network.

The workflows fail rather than silently publish a network when any of the first four pipeline stages does not complete.
