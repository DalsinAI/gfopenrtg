#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# libendian.a, amiga_endian.h's array calls as real functions:
#   library/endian/build.sh [OUT_DIR]    (CC, default m68k-amigaos-gcc;
#   CPU, default the kit's -m68040 -m68881; libendian.a goes in OUT_DIR)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-$ROOT/build/endian}
CC=${CC:-m68k-amigaos-gcc}
AR=${AR:-${CC%gcc}ar}
CPU=${CPU:--m68040 -m68881}
mkdir -p "$OUT"
# shellcheck disable=SC2086
"$CC" $CPU -noixemul -O2 -fomit-frame-pointer -Wall -Wextra -Werror -I"$ROOT/include" \
    -c -o "$OUT/endian.o" "$HERE/endian.c"
rm -f "$OUT/libendian.a"
"$AR" rcs "$OUT/libendian.a" "$OUT/endian.o"
echo "$OUT/libendian.a"
