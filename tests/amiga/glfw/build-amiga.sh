#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target tests/amiga/glfw),
# or by hand: libglfw3.a (library/modules/glfw/build.sh) and its three test
# programs (library/modules/glfw/tests/build-tests.sh), linked against the
# GCC 16 stove's libGL.a, and GLFWCheck (glfwcheck.c), which checks each kind
# of context from inside a program. Run them on an Amiga with
# LIBS:GFOpenGPU/GL.module: TriangleGLES and Gears on virgl and softpipe,
# SokolTriangle on virgl, GLFWCheck on both (SetEnv GFOpenGPU/GLDriver cpu).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then P=$STOVE/prefix; else P=$STOVE; fi
SYS=$P/m68k-amigaos
"$P/bin/m68k-amigaos-gcc" --version | head -1
GL_LIB=
for base in "$SYS/lib" "$P/lib" "$SYS" "$P"; do
    [ -d "$base" ] || continue
    GL_LIB=$(find "$base" -type f -name libGL.a -print -quit 2>/dev/null || true)
    [ -z "$GL_LIB" ] || break
done
[ -n "$GL_LIB" ] || { echo "GLFW tests: no libGL.a in $STOVE"; exit 5; }
echo "libGL.a: $GL_LIB"

# The stove laid out as the kit build-tests.sh wants (include/, lib/libGL.a)
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
KIT=$OUT/glfw-kit
rm -rf "$KIT"
mkdir -p "$KIT/lib"
ln -s "$SYS/include" "$KIT/include"
ln -s "$GL_LIB" "$KIT/lib/libGL.a"

STOVE16=$P "$ROOT/library/modules/glfw/build.sh" "$OUT"
STOVE16=$P "$ROOT/library/modules/glfw/tests/build-tests.sh" "$KIT" "$OUT"
mkdir -p "$OUT/c"
"$P/bin/m68k-amigaos-gcc" -O2 -noixemul -m68040 -m68881 -fno-delete-null-pointer-checks \
    -Wall -Wextra -Werror -I"$OUT/glfw/include" -I"$SYS/include" -I"$ROOT/library/modules/gl" \
    -o "$OUT/c/GLFWCheck" "$HERE/glfwcheck.c" -L"$OUT/glfw/lib" -L"$KIT/lib" -lglfw3 -lGL -lm
sha256sum "$OUT/c/GLFWCheck"
for p in TriangleGLES Gears SokolTriangle; do
    cp "$OUT/glfw/tests/$p" "$OUT/c/$p"
    sha256sum "$OUT/c/$p"
done
