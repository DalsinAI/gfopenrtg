#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target tests/amiga/egl),
# or by hand: libEGL.a (library/egl/build.sh), the kit's EGLTriangle
# (sdk/examples/egl) and the two opengles3-book samples
# (library/egl/samples/opengles3-book: the book is fetched), linked against
# the GCC 16 stove's libGL.a. Run them on an Amiga with
# LIBS:GFOpenGPU/GL.module, on virgl and on softpipe (SetEnv
# GFOpenGPU/GLDriver cpu); each takes SECONDS n. ParticleSystemTransformFeedback
# reads smoke.tga from the current directory.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then P=$STOVE/prefix; else P=$STOVE; fi
CC=$P/bin/m68k-amigaos-gcc
SYS=$P/m68k-amigaos
"$CC" --version | head -1
GL_LIB=
for base in "$SYS/lib" "$P/lib" "$SYS" "$P"; do
    [ -d "$base" ] || continue
    GL_LIB=$(find "$base" -type f -name libGL.a -print -quit 2>/dev/null || true)
    [ -z "$GL_LIB" ] || break
done
[ -n "$GL_LIB" ] || { echo "EGL tests: no libGL.a in $STOVE"; exit 5; }
echo "libGL.a: $GL_LIB"

# The stove laid out as a kit (include/, lib/libGL.a)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
KIT=$OUT/egl-kit
rm -rf "$KIT"
mkdir -p "$KIT/lib"
ln -s "$SYS/include" "$KIT/include"
ln -s "$GL_LIB" "$KIT/lib/libGL.a"

CC=$CC AR=$P/bin/m68k-amigaos-ar MESA_INCLUDE=$SYS/include "$ROOT/library/egl/build.sh" "$OUT"
mkdir -p "$OUT/c"
"$CC" -O2 -noixemul -m68040 -m68881 -fno-delete-null-pointer-checks -Wall -Wextra -Werror \
    -I"$ROOT/include" -I"$SYS/include" -o "$OUT/c/EGLTriangle" "$ROOT/sdk/examples/egl/egltriangle.c" \
    -L"$OUT" -L"$KIT/lib" -lEGL -lGL -lm
CC=$CC "$ROOT/library/egl/samples/opengles3-book/build.sh" "$KIT" "$OUT"
cp "$OUT/opengles3-book/Hello_Triangle/Hello_Triangle" "$OUT/c/"
cp "$OUT/opengles3-book/ParticleSystemTransformFeedback/ParticleSystemTransformFeedback" "$OUT/c/"
cp "$OUT/opengles3-book/ParticleSystemTransformFeedback/smoke.tga" "$OUT/c/"
for p in EGLTriangle Hello_Triangle ParticleSystemTransformFeedback; do
    python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/c/$p" || true
    sha256sum "$OUT/c/$p"
done
