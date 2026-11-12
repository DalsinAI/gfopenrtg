#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# MGLCount's entry names, from the dispatch table's own header:
#   mglcount_names.py include/libraries/minigl_dispatch.h OUT.h
# writes a C array of the 1-based entries' names in table order, and checks
# that the entries' offsets run 20, 24, ... with none missing.
import re
import sys

ENTRY = re.compile(r"\(\*(\w+)\)\(.*\);\s*/\*\s*(\d+):\s*(\d+)\s*\*/")


def names(text):
    out = []
    for m in ENTRY.finditer(text):
        name, n, off = m.group(1), int(m.group(2)), int(m.group(3))
        if n != len(out) + 1 or off != 16 + 4 * n:
            raise SystemExit(f"mglcount_names: entry {n} ({name}) at {off} is out of order")
        out.append(name)
    count = re.search(r"#define MGL_DISPATCH_ENTRIES\s+(\d+)", text)
    if not count or int(count.group(1)) != len(out):
        raise SystemExit(f"mglcount_names: found {len(out)} entries, the header says {count.group(1) if count else '?'}")
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: mglcount_names.py minigl_dispatch.h OUT.h")
    with open(sys.argv[1], encoding="utf-8") as f:
        n = names(f.read())
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write("/* Made by tools/mglcount_names.py from include/libraries/minigl_dispatch.h. */\n")
        f.write(f"#define MGLC_ENTRIES {len(n)}\n")
        f.write("static const char *const mglc_names[MGLC_ENTRIES] = {\n")
        for name in n:
            f.write(f'    "{name}",\n')
        f.write("};\n")


if __name__ == "__main__":
    main()
