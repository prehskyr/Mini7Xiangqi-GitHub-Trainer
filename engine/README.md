# Engine directory

Expected runtime file:

```text
fairy-stockfish.exe
```

Expected SHA-256:

```text
f894e6db3e5f2842da57dbeab33505aabf976f55afccd30bb87c78cb8bcf2bb3
```

The Actions workflow verifies this hash before compiling or training. When intentionally replacing the engine, update `scripts/verify-engine.ps1` and document the exact corresponding GPL source in `ENGINE_SOURCE_NOTICE.md`.
