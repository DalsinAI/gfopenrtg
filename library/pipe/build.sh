#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The OpenGPU shared pipe (docs/design/OpenGPU-Shared-Pipe-Plan.md), stage M2: its screen and transport.
#
# The pipe is linked into the libraries that use it (minigl.library, Warp3D.library: their build.sh compile
# pipe_screen.c and pipe_xport_os3.c with their own flags), so this builds nothing to ship. It checks that the
# pipe's files build for the 68k on their own (every warning an error), as libogpupipe.a, and runs the host test.
#   library/pipe/build.sh [OUT]       STOVE as for the libraries (CC= to name the compiler)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
AR=${AR:-$STOVE/prefix/bin/m68k-amigaos-ar}
OUT=${1:-$ROOT/build}
OBJ="$OUT/pipe-obj"
mkdir -p "$OBJ"
for f in pipe_screen pipe_xport_os3; do
    "$CC" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -ffixed-a4 -fno-builtin \
        -Wall -Wextra -Werror -Wno-unused-parameter -I"$ROOT/include" -c -o "$OBJ/$f.o" "$HERE/$f.c"
done
rm -f "$OUT/libogpupipe.a"
"$AR" rcs "$OUT/libogpupipe.a" "$OBJ/pipe_screen.o" "$OBJ/pipe_xport_os3.o"
echo "$OUT/libogpupipe.a"
OUT="$OUT/tests" sh "$ROOT/tests/pipe/test_screen.sh"
