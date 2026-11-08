#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target tests/endian),
# or by hand: test_endian for the 68040 twice, TestEndian with the header's
# inline asm and TestEndianC with AMIGA_ENDIAN_NO_ASM (GCC's own loops), so
# that "TestEndian bench 1024" and "TestEndianC bench 1024" compare the two
# on the same machine.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then P=$STOVE/prefix; else P=$STOVE; fi
CC=$P/bin/m68k-amigaos-gcc
"$CC" --version | head -1
mkdir -p "$OUT/c"
FLAGS="-m68040 -noixemul -std=c99 -O2 -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror -I$ROOT/include"
"$CC" $FLAGS -o "$OUT/c/TestEndian" "$HERE/test_endian.c"
"$CC" $FLAGS -DAMIGA_ENDIAN_NO_ASM -o "$OUT/c/TestEndianC" "$HERE/test_endian.c"
sha256sum "$OUT/c/TestEndian" "$OUT/c/TestEndianC"
