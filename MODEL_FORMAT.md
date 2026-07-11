# `.m7nnue` model format

This repository trains the Mini7 client network, not Fairy-Stockfish's native NNUE format.

## Architecture

- Input: 10 piece channels × 49 squares + 2 side-to-move features
- Hidden layer: 64 ReLU units
- Output: normalized red-side evaluation
- Stored scalar target convention: red advantage is positive

## Header

The binary starts with:

```text
magic[8] = "M7NNUE1\0"
uint32 version
uint32 input_size
uint32 hidden_size
uint64 training_steps
float output_bias
```

It is followed by the hidden biases, output weights, and input weights as IEEE-754 `float` arrays.

## Compatibility boundary

`.m7nnue` cannot be passed to Fairy-Stockfish through `setoption name EvalFile`. The desktop client loads it and uses it to re-score Fairy-Stockfish MultiPV root candidates.
