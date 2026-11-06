#!/usr/bin/env python3
"""Checks that two directories of `dwgread -O JSON` dumps (for example from the
Windows and macOS builds) are identical in value: same keys, same strings, and
numbers equal as doubles (so only real parsing differences fail, not printf
spelling). Exit code 1 lists the first differences per file."""
import json
import math
import os
import sys


def diff(a, b, path, out, limit=10):
    if len(out) >= limit:
        return
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                out.append(f"{path}/{k}: missing on one side")
            else:
                diff(a[k], b[k], f"{path}/{k}", out, limit)
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append(f"{path}: length {len(a)} vs {len(b)}")
        for i, (x, y) in enumerate(zip(a, b)):
            diff(x, y, f"{path}[{i}]", out, limit)
    elif isinstance(a, (int, float)) and isinstance(b, (int, float)) and not isinstance(a, bool):
        if not (float(a) == float(b) or (math.isnan(float(a)) and math.isnan(float(b)))):
            out.append(f"{path}: {a!r} vs {b!r}")
    elif a != b:
        out.append(f"{path}: {a!r} vs {b!r}")


def main():
    left, right = sys.argv[1], sys.argv[2]
    names = sorted(n for n in os.listdir(left) if n.endswith(".json"))
    failed = False
    for n in names:
        rp = os.path.join(right, n)
        if not os.path.exists(rp):
            print(f"{n}: missing in {right}")
            failed = True
            continue
        with open(os.path.join(left, n), encoding="utf-8") as f:
            a = json.load(f)
        with open(rp, encoding="utf-8") as f:
            b = json.load(f)
        out = []
        diff(a, b, "", out)
        if out:
            failed = True
            print(f"{n}: DIFFERENT")
            for line in out:
                print("   ", line)
        else:
            print(f"{n}: identical")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
