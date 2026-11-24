#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# ACMWindow (acmwindow.c): a film into a window through ACMedia and ORTG_Present's MEDIA kind,
# for the Cradle's stove_build (STOVE= and OUT= set, target tests/amiga/acmwindow).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
mkdir -p "$OUT"
"$CC" --version | head -1
"$CC" -m68040 -specs="$ROOT/tools/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul \
    -I"$ROOT/include" -I"$ROOT/library" -o "$OUT/ACMWindow" "$HERE/acmwindow.c" "$ROOT/library/acmedia_stubs.c"
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/ACMWindow"
echo "$OUT/ACMWindow ($(wc -c < "$OUT/ACMWindow") bytes)"
sha256sum "$OUT/ACMWindow"
