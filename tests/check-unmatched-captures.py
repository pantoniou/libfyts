#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path


def main(argv):
    if len(argv) < 3 or len(argv[2:]) % 2:
        print("usage: check-unmatched-captures.py FYTS-HIGHLIGHT LANG FILE ...", file=sys.stderr)
        return 2

    highlighter = argv[1]
    failures = []

    for i in range(2, len(argv), 2):
        lang = argv[i]
        path = Path(argv[i + 1])
        result = subprocess.run(
            [highlighter, "--report-unmatched-captures", "--lang", lang, str(path)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if result.returncode != 0:
            failures.append(f"{lang}: exited {result.returncode}: {result.stderr.strip()}")
        elif result.stdout.strip():
            captures = " ".join(result.stdout.split())
            failures.append(f"{lang}: unmatched captures: {captures}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
