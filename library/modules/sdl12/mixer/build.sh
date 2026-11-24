#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# SDL_mixer 1.2's calls on AHI, the Team's own (README.md): libSDL_mixer.a
# and SDL/SDL_mixer.h, with the GCC 16 stove.
#   library/modules/sdl12/mixer/build.sh [OUT_DIR]     (default build/)
# Needs OUT_DIR/sdl12/include/SDL (library/modules/sdl12/build.sh). Puts
# libSDL_mixer.a and include/SDL/SDL_mixer.h in OUT_DIR/sdl12/, which
# tools/make_sdk.py puts in the developer kit.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
STOVE16=${STOVE16:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}
OUT=${1:-$REPO/build}
OUT=$(CDPATH= cd -- "$OUT" && pwd)
CC=$STOVE16/bin/m68k-amigaos-gcc
AR=$STOVE16/bin/m68k-amigaos-ar
[ -f "$OUT/sdl12/include/SDL/SDL.h" ] || { echo "sdl12/mixer: no SDL 1.2 headers in $OUT/sdl12 (library/modules/sdl12/build.sh first)" >&2; exit 1; }
# As libSDL.a: the 68040 with its FPU, and -fno-delete-null-pointer-checks
# (address 0 is memory) and -fno-tree-loop-distribute-patterns (GCC 16).
CF="-m68040 -m68881 -mnobitfield -O2 -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns -fomit-frame-pointer -noixemul"
O=$OUT/sdl12-mixer-obj
rm -rf "$O"
mkdir -p "$O" "$OUT/sdl12/include/SDL"
for f in mix_core mix_wav mix_amiga; do
    "$CC" $CF -std=gnu99 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers \
        -I"$HERE/include" -I"$HERE/src" -I"$OUT/sdl12/include/SDL" -c -o "$O/$f.o" "$HERE/src/$f.c"
done
LIB=$OUT/sdl12/libSDL_mixer.a
rm -f "$LIB"
"$AR" rcs "$LIB" "$O/mix_core.o" "$O/mix_wav.o" "$O/mix_amiga.o"
cp "$HERE/include/SDL_mixer.h" "$OUT/sdl12/include/SDL/SDL_mixer.h"
python3 "$REPO/tools/fpcr_check.py" "${CC%gcc}objdump" "$O"/*.o
echo "$LIB ($(wc -c < "$LIB") bytes)"
