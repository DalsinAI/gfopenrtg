#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# OpenRTGLineCheck (line_check.c) for the Cradle's stove_build (STOVE= and
# OUT= set, target tests/amiga/lines), or by hand with STOVE or CC as in
# tools/build.sh. It runs on an Amiga with openrtg.library.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
OUT=${OUT:-$ROOT/build-amiga}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
mkdir -p "$OUT/c"
"$CC" --version | head -1
"$CC" -m68040 -specs="$ROOT/tools/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul \
    -I"$ROOT/include" -o "$OUT/c/OpenRTGLineCheck" "$HERE/line_check.c"
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/c/OpenRTGLineCheck"
echo "$OUT/c/OpenRTGLineCheck ($(wc -c < "$OUT/c/OpenRTGLineCheck") bytes)"
sha256sum "$OUT/c/OpenRTGLineCheck"
