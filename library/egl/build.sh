#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# EGL 1.4 on GFOpenGPU, with the os32 stove (bebbo's m68k-amigaos-gcc) or
# GFOpenAmigaGCC 16.2:
#   libEGL.a        EGL's calls on GLA (libGL.a); include/EGL/ has the headers
#   library/egl/build.sh [OUT_DIR]     (default build/)
# libEGL.a is built for the 68040 with its FPU, as libGL.a and SDL 2 are,
# and links with either stove. A compiler without the NDK's headers built in
# (GFOpenAmigaGCC on its own) takes them as NDK_INCLUDE.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
AR=${AR:-$STOVE/prefix/bin/m68k-amigaos-ar}
OUT=${1:-$ROOT/build}
OBJ="$OUT/egl-obj"
mkdir -p "$OUT" "$OBJ"
# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access.
WARN="-Wall -Wextra -Werror -Wno-unused-parameter"
# GLA's headers come from the GL module's source; Khronos's (KHR/khrplatform.h)
# from Mesa's, given as MESA_INCLUDE, or the kit's or a stove's include/.
GLA="$ROOT/library/modules/gl/gla"
mkdir -p "$OBJ/inc"
ln -sfn "$GLA" "$OBJ/inc/gla"
if [ -z "${MESA_INCLUDE:-}" ]; then
    ENVF=${GL_WORK:-$ROOT/library/modules/gl/build}/mesa-build-m68k-amigaos/gla-link.env
    [ ! -f "$ENVF" ] || MESA_INCLUDE=$(sed -n "s/^SRC='\(.*\)'$/\1/p" "$ENVF")/include
fi
MI=
[ -z "${MESA_INCLUDE:-}" ] || MI="-I$MESA_INCLUDE"
NI=
[ -z "${NDK_INCLUDE:-}" ] || NI="-I$NDK_INCLUDE"
"$CC" -m68040 -m68881 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -noixemul $WARN \
    -I"$ROOT/include" -I"$OBJ/inc" $MI $NI -c -o "$OBJ/egl.o" "$HERE/egl.c"
# No 68881 instruction the 68040 leaves to software.
if "${CC%gcc}objdump" -d "$OBJ/egl.o" | grep -Eq '\sf(sin|cos|tan|atan|etox|logn|log10|log2|intrz|int|mod|rem)[.a-z]*\s'; then
    echo "libEGL.a: has FPU instructions the 68040 lacks" >&2
    exit 1
fi
rm -f "$OUT/libEGL.a"
"$AR" rcs "$OUT/libEGL.a" "$OBJ/egl.o"
echo "$OUT/libEGL.a"
