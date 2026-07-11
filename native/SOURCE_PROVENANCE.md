# Source provenance

## Engine source

`native/engine` is based on the complete Fairy-Stockfish source archive supplied by the project owner on 2026-07-11. The archive did not contain `.git`, so an exact upstream commit cannot be proven from the archive alone.

Project modifications are confined to the dedicated `mini7xiangqi` variant and its consecutive-rook-check state tracking:

- `src/variant.h`
- `src/parser.cpp`
- `src/variant.cpp`
- `src/position.h`
- `src/position.cpp`
- `src/variants.ini`

The engine remains GPLv3-or-later under `native/engine/Copying.txt`.

## Pinned external training components

GitHub Actions fetch exact commits rather than floating branches:

- `fairy-stockfish/variant-nnue-tools` at `c8df2c39515a2654d5b52ba55b4ee585b20430a8`
- `fairy-stockfish/variant-nnue-pytorch` at `b15df38a9aae8ab9b40b2378020b3099c7c5d179`

The tools checkout receives the same Mini7 rules through `native/scripts/patch_tools_engine.py`. The trainer checkout receives an additional CPU backend through `native/scripts/patch_trainer_cpu.py` while preserving its CUDA implementation.
