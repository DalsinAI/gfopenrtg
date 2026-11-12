#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# MGLCount's files, ranked: mglcount_report.py [--top N] FILE...
# Several files (one a run) are added together. Two tables: by calls and by
# time (the share of all samples taken inside each entry), with calls a
# second over the runs' time.
import argparse


def read(paths):
    calls, samples, ms, total = {}, {}, 0, 0
    for p in paths:
        with open(p, encoding="latin-1") as f:
            for line in f:
                line = line.strip()
                if line.startswith("# MGLCount:"):
                    w = line.split()
                    ms += int(w[2])
                    total += int(w[4])
                    continue
                if not line or line.startswith("#"):
                    continue
                n, name, c, s = line.split()
                calls[name] = calls.get(name, 0) + int(c)
                samples[name] = samples.get(name, 0) + int(s)
    return calls, samples, ms, total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    calls, samples, ms, total = read(a.files)
    secs = ms / 1000 or 1
    total = total or 1
    named = [k for k in calls if k != "outside"]
    print(f"{len(a.files)} run(s), {secs:.1f} s, {total} samples, "
          f"{100 * sum(samples[k] for k in named) / total:.1f}% inside MiniGL\n")
    print("By calls:\n| # | entry | calls | calls/s | time % |\n|---|---|---|---|---|")
    for i, k in enumerate(sorted(named, key=lambda k: -calls[k])[:a.top], 1):
        print(f"| {i} | {k} | {calls[k]} | {calls[k] / secs:.0f} | {100 * samples[k] / total:.1f} |")
    print("\nBy time:\n| # | entry | time % | calls | us/call |\n|---|---|---|---|---|")
    for i, k in enumerate(sorted(named, key=lambda k: -samples[k])[:a.top], 1):
        us = (samples[k] / total) * secs * 1e6 / calls[k] if calls[k] else 0
        print(f"| {i} | {k} | {100 * samples[k] / total:.1f} | {calls[k]} | {us:.1f} |")


if __name__ == "__main__":
    main()
