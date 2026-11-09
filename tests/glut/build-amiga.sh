#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target tests/glut):
# build_tests.sh with the stove as compiler and kit. The programs are
# OUT/glut-tests/ (GLUTSpin, GLUScene, GLUTCore, gears, brick and data/).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$HERE/../../build-amiga}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then P=$STOVE/prefix; else P=$STOVE; fi
STOVE16=$P KIT=$P/m68k-amigaos sh "$HERE/build_tests.sh" "$OUT"
