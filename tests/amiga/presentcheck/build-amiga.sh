#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# PresentCheck (presentcheck.c): M8's level 3 check for frames in memory (ORTG_SURF_MEMORY),
# for the Cradle's stove_build (STOVE= and OUT= set, target tests/amiga/presentcheck).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
mkdir -p "$OUT"
"$CC" --version | head -1
"$CC" -m68040 -specs="$ROOT/tools/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul \
    -I"$ROOT/include" -I"$ROOT/library" -o "$OUT/PresentCheck" "$HERE/presentcheck.c"
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/PresentCheck"
echo "$OUT/PresentCheck ($(wc -c < "$OUT/PresentCheck") bytes)"
sha256sum "$OUT/PresentCheck"
