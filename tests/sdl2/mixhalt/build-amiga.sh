#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target tests/sdl2/mixhalt):
# mixhalt on the stove's GFOpenGPU SDL 2 and SDL2_mixer link stubs.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16-open6}
OUT=${OUT:-$HERE/out}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then PREFIX=$STOVE/prefix; else PREFIX=$STOVE; fi
mkdir -p "$OUT"
"$PREFIX/bin/m68k-amigaos-gcc" -std=gnu11 -O2 -m68040 -m68881 -noixemul -Wall -Wextra \
    -o "$OUT/mixhalt" "$HERE/mixhalt.c" -lSDL2_mixer -lSDL2 -lm -lamiga
ls -l "$OUT/mixhalt"
