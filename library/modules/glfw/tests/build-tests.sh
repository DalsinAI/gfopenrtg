#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Builds three GLFW programs for AmigaOS 3 on build.sh's libglfw3.a, as a
# program from outside would be built (pkg-config's flags, no changes to
# their source):
#   TriangleGLES  GLFW's examples/triangle-opengles.c: OpenGL ES 2.0, glad
#   Gears         GLFW's examples/gears.c: OpenGL 1.x (compatibility), glad
#   SokolTriangle sokol-samples' glfw/triangle-glfw.c: GL 4.1 core, so
#                 virgl only (softpipe's core profile stops at 3.3)
#   library/modules/glfw/tests/build-tests.sh KIT [OUT_DIR]
# KIT is a GFOpenGPU developer kit (include/GL, include/gla, lib/libGL.a,
# lib/pkgconfig/gl.pc). OUT_DIR is build.sh's (default build/); the
# programs go in OUT_DIR/glfw/tests/. The sokol files are fetched at
# UPSTREAM.json's commits into OUT_DIR/glfw-work/sokol.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MOD=$(CDPATH= cd -- "$HERE/.." && pwd)
REPO=$(CDPATH= cd -- "$MOD/../../.." && pwd)
STOVE16=${STOVE16:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}
NDK=${NDK:-}
KIT=$(CDPATH= cd -- "${1:?KIT}" && pwd)
OUT=$(CDPATH= cd -- "${2:-$REPO/build}" && pwd)
CC=$STOVE16/bin/m68k-amigaos-gcc
S=$OUT/glfw-work/glfw-3.4
G=$OUT/glfw
[ -f "$G/lib/libglfw3.a" ] && [ -d "$S" ] || { echo "glfw tests: no $G/lib/libglfw3.a (build.sh first)" >&2; exit 1; }
[ -f "$KIT/lib/libGL.a" ] || { echo "glfw tests: no $KIT/lib/libGL.a" >&2; exit 1; }

# What pkg-config --cflags --libs glfw3 gives in the kit, with this build's
# GLFW in front of the kit's.
CF="-I$G/include -I$KIT/include${NDK:+ -I$NDK} -noixemul -m68040 -m68881 -O2 -fno-delete-null-pointer-checks"
LIBS="-L$G/lib -L$KIT/lib -lglfw3 -lGL -lm"
T=$G/tests
mkdir -p "$T"

"$CC" $CF -I"$S/deps" -o "$T/TriangleGLES" "$S/examples/triangle-opengles.c" $LIBS
"$CC" $CF -I"$S/deps" -o "$T/Gears" "$S/examples/gears.c" $LIBS

pin() { python3 -c "import json,sys; print(' '.join(json.load(open('$MOD/UPSTREAM.json'))[sys.argv[1]][sys.argv[2]]) if sys.argv[2]=='files' else json.load(open('$MOD/UPSTREAM.json'))[sys.argv[1]][sys.argv[2]])" "$1" "$2"; }
K=$OUT/glfw-work/sokol
mkdir -p "$K"
for p in sokol sokol-samples; do
    for f in $(pin $p files); do
        [ -f "$K/$f" ] || curl -fsL -o "$K/$f" "$(pin $p url)$f"
    done
done
# sokol knows no AmigaOS: its Linux branch (__unix__) is the one that fits,
# GL's calls linked as they are (libGL.a has every one) through Mesa's
# GL/gl.h with GL_GLEXT_PROTOTYPES, and its log on stderr
SOKOL="-D__unix__"
"$CC" $CF $SOKOL -std=gnu99 -I"$K" -o "$T/SokolTriangle" "$K/triangle-glfw.c" "$K/glfw_glue.c" $LIBS

for p in TriangleGLES Gears SokolTriangle; do
    echo "$T/$p ($(wc -c < "$T/$p") bytes)"
done
