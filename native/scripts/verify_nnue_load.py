#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("engine", type=Path)
    parser.add_argument("network", type=Path)
    args = parser.parse_args()
    engine = args.engine.resolve()
    network = args.network.resolve()
    if not engine.is_file() or not network.is_file():
        raise FileNotFoundError(f"engine={engine}, network={network}")
    if "mini7xiangqi" not in network.name.lower():
        raise ValueError("network filename must contain 'mini7xiangqi' for Fairy-Stockfish variant selection")

    commands = f"""uci
setoption name UCI_Variant value mini7xiangqi
setoption name EvalFile value {network}
setoption name Use NNUE value true
isready
position startpos
go depth 2
quit
"""
    completed = subprocess.run(
        [str(engine)], input=commands, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=60, check=False,
    )
    print(completed.stdout)
    if completed.returncode != 0:
        return completed.returncode
    checks = ["NNUE evaluation using", "bestmove "]
    missing = [x for x in checks if x not in completed.stdout]
    if missing:
        print("NNUE verification failed; missing: " + ", ".join(missing), file=sys.stderr)
        return 1
    print("Native NNUE load and search test passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
