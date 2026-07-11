#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def run_engine(exe: Path, commands: str, timeout: int = 30) -> str:
    completed = subprocess.run(
        [str(exe)],
        input=commands,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )
    print(completed.stdout)
    if completed.returncode != 0:
        raise RuntimeError(f"engine exited with code {completed.returncode}")
    return completed.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("engine", type=Path)
    args = parser.parse_args()
    exe = args.engine.resolve()
    if not exe.is_file():
        raise FileNotFoundError(exe)

    commands = """uci
setoption name UCI_Variant value mini7xiangqi
setoption name Use NNUE value false
isready
position startpos
go perft 1
position fen 3k3/7/R6/7/7/7/4K2 w - - 0 1 moves a5d5 d7c7 d5c5 c7d7 c5d5
go depth 1
quit
"""
    output = run_engine(exe, commands)

    required = {
        "variant registration": "mini7xiangqi",
        "external NNUE option": "option name EvalFile",
        "start-position perft": "Nodes searched: 19",
        "third rook check loses": "bestmove (none)",
    }
    missing = [name for name, needle in required.items() if needle not in output]
    if missing:
        print("FAILED checks: " + ", ".join(missing), file=sys.stderr)
        return 1
    print("Mini7 native engine smoke test passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
