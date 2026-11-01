#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# PixTest (pixtest.c), and PixConvTest (tests/test_pixconv.c: library/pixconv.h
# against 0.24's per-pixel code, on the Amiga's own 68k, with BENCH timings),
# for the Cradle's stove_build (STOVE= and OUT= set, target tests/amiga/pixels),
# as tools/build.sh builds the other tools.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32}
OUT=${OUT:-$ROOT/build-amiga}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
mkdir -p "$OUT"
"$CC" --version | head -1
"$CC" -m68040 -specs="$ROOT/tools/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul \
    -I"$ROOT/include" -o "$OUT/PixTest" "$HERE/pixtest.c" -lm
"$CC" -m68040 -specs="$ROOT/tools/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -noixemul \
    -o "$OUT/PixConvTest" "$ROOT/tests/test_pixconv.c" -lm
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/PixTest" "$OUT/PixConvTest"
echo "$OUT/PixTest ($(wc -c < "$OUT/PixTest") bytes), $OUT/PixConvTest ($(wc -c < "$OUT/PixConvTest") bytes)"
sha256sum "$OUT/PixTest" "$OUT/PixConvTest"
