# Test report

Test environment: Linux x86-64 container, GCC 14.2.0, Clang, CMake 3.31.6.

## Completed tests

### Native rule tests

```text
rules_test: PASS
```

Coverage includes the 7×7 initial position, soldier movement, palace boundaries, horse-leg blocking, cannon screens, flying-general validation, three consecutive rook checks, repetition draw, and natural-move draw.

### Model serialization tests

```text
net_test: PASS
```

Coverage includes one weight update, `.m7nnue` save, reload, and evaluation consistency.

### Headless CLI smoke and resume

A deterministic C++ mock UCI engine returned the legal initial move `a2a3` with a teacher score. The CLI was run twice against the same checkpoint:

```text
first run:  steps 0 -> 1
second run: steps 1 -> 2
result: PASS
```

This verifies UCI process communication, legal-move filtering, one training update, atomic checkpoint replacement, model reload, and resumed training.

### Sanitizers

Rules, model tests, and a two-update CLI run passed with:

```text
-fsanitize=address,undefined -fno-omit-frame-pointer
```

No AddressSanitizer or UndefinedBehaviorSanitizer finding was reported.

### Compiler coverage

- GCC Release build: PASS
- Clang Release build: PASS

## Windows-specific verification boundary

The delivered GitHub Actions workflow compiles with Visual Studio 2022 on `windows-latest`, then runs the same unit tests and a two-stage mock UCI resume test before starting the real Fairy-Stockfish job.

This local Linux environment cannot execute the bundled Windows `fairy-stockfish.exe`, so the real-engine Windows launch is intentionally verified by the workflow after the repository is pushed. The workflow refuses to train if:

- the engine SHA-256 does not match;
- MSVC compilation fails;
- native tests fail;
- the first mock run does not reach step 1;
- the second mock run does not resume from step 1 and reach step 2.

Bundled engine SHA-256:

```text
f894e6db3e5f2842da57dbeab33505aabf976f55afccd30bb87c78cb8bcf2bb3
```
