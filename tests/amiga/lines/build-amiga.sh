#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target tests/amiga/lines),
# or by hand with STOVE or CC as in tools/build.sh:
#   OpenRTGLineCheck  (line_check.c) Draw and PolyDraw through gfopenrtg.library
#                     against a planar bitmap, and their times
#   OpenRTGLineModel  (tests/test_lines.c) the host test on the 68k: 0.25's
#                     line code against graphics.library's and 0.24's
#   OpenRTGLineTime   (line_time.c) a pixel at a time against a line at a
#                     time, through a real layer, without the library
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32}
OUT=${OUT:-$ROOT/build-amiga}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
mkdir -p "$OUT/c"
"$CC" --version | head -1
"$CC" -m68040 -specs="$ROOT/tools/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul \
    -I"$ROOT/include" -o "$OUT/c/OpenRTGLineCheck" "$HERE/line_check.c"
"$CC" -m68040 -specs="$ROOT/tools/m68k-soft-m68040.specs" -mnobitfield -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul \
    -I"$ROOT/include" -o "$OUT/c/OpenRTGLineModel" "$ROOT/tests/test_lines.c" \
    "$ROOT/library/opengpu/ogpu_core.c" "$ROOT/library/opengpu/ogpu_dwt.c" "$ROOT/library/opengpu/ogpu_build.c" -lm
"$CC" -m68040 -specs="$ROOT/tools/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul \
    -I"$ROOT/include" -o "$OUT/c/OpenRTGLineTime" "$HERE/line_time.c"
for t in OpenRTGLineCheck OpenRTGLineModel OpenRTGLineTime; do
    python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/c/$t"
    echo "$OUT/c/$t ($(wc -c < "$OUT/c/$t") bytes)"
    sha256sum "$OUT/c/$t"
done
