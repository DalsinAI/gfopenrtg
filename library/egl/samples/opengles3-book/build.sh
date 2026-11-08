#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Two of the OpenGL ES 3.0 Programming Guide's samples on GFOpenGPU, through
# libEGL.a: Hello_Triangle (chapter 2) and ParticleSystemTransformFeedback
# (chapter 14). The book's code (opengles3-book, MIT) is fetched at a pinned
# commit, not kept here; esUtil_Amiga.c is its window back end for AmigaOS.
#   library/egl/samples/opengles3-book/build.sh KIT [OUT_DIR]     (default build/)
# KIT      a GFOpenGPU developer kit, or a stove's prefix/m68k-amigaos: GLES's
#          headers and libGL.a
# OUT_DIR  where library/egl/build.sh put libEGL.a; the samples go in
#          OUT_DIR/opengles3-book/, each in its own drawer
# BOOK_SRC a checkout of opengles3-book to use instead of fetching one.
# NDK_INCLUDE as for library/egl/build.sh.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
KIT=$(cd "${1:?KIT}" && pwd)
OUT=${2:-$ROOT/build}
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
BOOK_URL=https://github.com/danginsburg/opengles3-book.git
BOOK_REV=423d936e31f5240fb893fcc8ae5549c70efaa2bf      # 2 May 2021, master
[ -f "$OUT/libEGL.a" ] || { echo "no $OUT/libEGL.a: run library/egl/build.sh first" >&2; exit 1; }
SRC=${BOOK_SRC:-$OUT/opengles3-book-src}
if [ ! -d "$SRC/Common" ]; then
    git clone -q "$BOOK_URL" "$SRC"
    git -C "$SRC" checkout -q "$BOOK_REV"
fi
DST="$OUT/opengles3-book"
OBJ="$DST/obj"
mkdir -p "$OBJ"
NI=
[ -z "${NDK_INCLUDE:-}" ] || NI="-I$NDK_INCLUDE"
# The book's code as it is: its own warnings stay its own.
CFLAGS="-m68040 -m68881 -O2 -fno-delete-null-pointer-checks -noixemul -I$SRC/Common/Include -I$ROOT/include -I$KIT/include $NI"
# esUtil.c's esLoadTGA reads TGA's header as big-endian shorts on the 68k:
# renamed here, and esUtil_Amiga.c has one that reads the bytes.
"$CC" $CFLAGS -DesLoadTGA=esLoadTGA_upstream -c -o "$OBJ/esUtil.o" "$SRC/Common/Source/esUtil.c"
for f in esShader esShapes esTransform; do
    "$CC" $CFLAGS -c -o "$OBJ/$f.o" "$SRC/Common/Source/$f.c"
done
"$CC" $CFLAGS -Wall -Wextra -Werror -c -o "$OBJ/esUtil_Amiga.o" "$HERE/esUtil_Amiga.c"
COMMON="$OBJ/esUtil.o $OBJ/esShader.o $OBJ/esShapes.o $OBJ/esTransform.o $OBJ/esUtil_Amiga.o"
LIBS="-L$OUT -lEGL -L$KIT/lib -lGL -lm"

mkdir -p "$DST/Hello_Triangle"
"$CC" $CFLAGS -o "$DST/Hello_Triangle/Hello_Triangle" "$SRC/Chapter_2/Hello_Triangle/Hello_Triangle.c" $COMMON $LIBS
echo "$DST/Hello_Triangle/Hello_Triangle"

P="$SRC/Chapter_14/ParticleSystemTransformFeedback"
mkdir -p "$DST/ParticleSystemTransformFeedback"
"$CC" $CFLAGS -I"$P" -o "$DST/ParticleSystemTransformFeedback/ParticleSystemTransformFeedback" \
    "$P/ParticleSystemTransformFeedback.c" "$P/Noise3D.c" $COMMON $LIBS
cp "$P/smoke.tga" "$DST/ParticleSystemTransformFeedback/"     # read from the current directory
echo "$DST/ParticleSystemTransformFeedback/ParticleSystemTransformFeedback"
cp "$SRC/LICENSE" "$DST/LICENSE-opengles3-book.txt"
