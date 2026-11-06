#!/bin/sh
# Copyright (c) 2026 Dalsin Limited / the AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# ogles3.library on GFOpenGPU, and OGLES3Check, with the GCC 16 stove
# (open6, GCC 16.2). For the Cradle's stove_build (STOVE= and OUT= set,
# target library/ogles3), or by hand:
#   STOVE=~/AmiCradle/stoves/os32-gcc16-open6 sh library/ogles3/build-amiga.sh [OUT_DIR]
# The library is bare (no startup code, no C library), built -ffixed-a4 as
# GFOpenGPU's other faces are, and holds no GL: its GL calls go to
# LIBS:GFOpenGPU/GL.module.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16-open6}
OUT=${OUT:-${1:-$ROOT/build-amiga}}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then PREFIX=$STOVE/prefix; else PREFIX=$STOVE; fi
CC=${CC:-$PREFIX/bin/m68k-amigaos-gcc}
NM=${CC%gcc}nm
GEN="$OUT/ogles3-gen"
mkdir -p "$OUT/libs" "$GEN"
"$CC" --version | head -1
python3 "$HERE/gen_ogles3.py" "$GEN"
SOFT="-specs=$ROOT/tools/m68k-soft-m68040.specs"
WARN="-Wall -Wextra -Werror -Wno-unused-parameter"
# -fno-delete-null-pointer-checks: address 0 is memory on an Amiga.
# -fno-toplevel-reorder: start() stays the first code.
# shellcheck disable=SC2086
"$CC" -m68040 $SOFT -O2 -ffixed-a4 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder \
    -fno-builtin $WARN -nostartfiles -nostdlib -I"$ROOT/include" -I"$GEN" -I"$HERE" \
    -o "$OUT/libs/ogles3.library" "$HERE/ogles3_lib.c" "$GEN/ogles3_table.c" "$HERE/ogles3_call.s" \
    "$GEN/ogles3_thunks.s" "$ROOT/library/string.c" -lgcc
und=$("$NM" -u "$OUT/libs/ogles3.library" || true)
[ -z "$und" ] || { echo "undefined in ogles3.library: $und" >&2; exit 1; }
echo "$OUT/libs/ogles3.library ($(wc -c < "$OUT/libs/ogles3.library") bytes)"
# shellcheck disable=SC2086
"$CC" -m68040 -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul -I"$GEN" \
    -o "$OUT/OGLES3Check" "$HERE/ogles3check.c"
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/libs/ogles3.library" "$OUT/OGLES3Check"
cp "$HERE/Install-ogles3" "$OUT/Install-ogles3"
sha256sum "$OUT/libs/ogles3.library" "$OUT/OGLES3Check"
