#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# RetainedTest (retainedtest.c: gfopengpu.library 0.25's retained surfaces on
# an Amiga), for the Cradle's stove_build (STOVE= and OUT= set, target
# tests/amiga/retained), or by hand with STOVE or CC as in tools/build.sh.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
mkdir -p "$OUT"
"$CC" --version | head -1
"$CC" -m68040 -specs="$ROOT/tools/m68k-soft-m68040.specs" -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul \
    -I"$ROOT/include" -o "$OUT/RetainedTest" "$HERE/retainedtest.c" -lm
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/RetainedTest"
echo "$OUT/RetainedTest ($(wc -c < "$OUT/RetainedTest") bytes)"
sha256sum "$OUT/RetainedTest"
