#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GLUT and GLU on GFOpenGPU, built and linked as programs use them (on the
# PC: running them needs an Amiga with GL.module).
#   tests/glut/build_tests.sh [OUT_DIR]       (default build/)
# Builds library/modules/glu and glut into OUT_DIR, then with pkg-config's
# flags (glut.pc, which needs glu.pc and the kit's gl.pc):
#   the kit's GLUT examples (sdk/examples/glut): GLUTSpin, GLUScene, GLUTCore
#   two of mesa-demos' programs, unchanged: gears (src/demos/gears.c) and
#   brick (src/glsl/brick.c, GLSL, with its shaders in data/), from the
#   commit below, each file checked by its SHA-256. Their glad is glad/gl.h
#   here (libGL.a has every GL call).
# The programs go to OUT_DIR/glut-tests/, ready for an Amiga: a drawer with
# data/ beside brick. KIT, NDK, STOVE16 and TARBALLS are the modules' own.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$HERE/../.." && pwd)
STOVE16=${STOVE16:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}
KIT=${KIT:-$STOVE16/m68k-amigaos}
OUT=${1:-$REPO/build}
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
export STOVE16 KIT
CC=${CC:-$STOVE16/bin/m68k-amigaos-gcc}
"$REPO/library/modules/glu/build.sh" "$OUT"
"$REPO/library/modules/glut/build.sh" "$OUT"

T=$OUT/glut-tests
mkdir -p "$T/data" "$T/src"
export PKG_CONFIG_LIBDIR="$OUT/glut/lib/pkgconfig:$OUT/glu/lib/pkgconfig:$KIT/lib/pkgconfig"
FLAGS=$(pkg-config --cflags --libs glut)
# GCC 16: its loop-shift miscompile (the kit's sdl2-config adds the same).
CF="-O2 -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns -Wall -Wno-unused-parameter -Wno-unused-function"

for p in glutspin:GLUTSpin gluscene:GLUScene glutcore:GLUTCore; do
    "$CC" $CF -Werror -o "$T/${p#*:}" "$REPO/sdk/examples/glut/${p%%:*}.c" $FLAGS
    echo "$T/${p#*:}"
done

# mesa-demos at this commit, each file by its SHA-256.
DEMOS=649baedafcb90313ade69909fdef1ee156ab5f8d
RAW=https://gitlab.freedesktop.org/mesa/demos/-/raw/$DEMOS
while read -r sum path; do
    f=$T/src/$(basename "$path")
    [ -f "$f" ] || curl -sSfL -o "$f" "$RAW/$path"
    echo "$sum  $f" | sha256sum -c --quiet - || { echo "glut tests: $path's sha256 differs" >&2; exit 1; }
done <<'EOF'
6bf8c10e4b343e08b9b3b9c002259d4722c67b082e022d14b7f12cc8c34bcd23 src/demos/gears.c
fdc8983db46ffcc018a02bd19954f0a64682b367d7ed62337f1378aef5a3a7ee src/glsl/brick.c
ee95e6fa31ec84c38857bd5d56a293ca1ea17daeb3b073553e2aa0b94a31cf74 src/util/shaderutil.c
0d569c0d9bcc0dea84964658f96ba396f5e48f8e185162008f23176e959c5017 src/util/shaderutil.h
6eca041eae5f034303b41f9517d141516aec65773eeda06c3be7e264af00e23c src/util/data.h
2ef5d9f41529bfb54ca96cfc5cd26d62cd1bd0f7c3a60000c4871dc17ca7084a src/util/glut_wrap.h
35ae527eee4b46627a77b589d4e3267ce5c5bed22695bf80469ffd580293aa3d src/data/CH06-brick.frag
18c5044415c2c7fed389dcd4ac85a55224e53472a965723d66c7e01be35d7c90 src/data/CH06-brick.vert
EOF
cp "$T/src/CH06-brick.frag" "$T/src/CH06-brick.vert" "$T/data/"
DF="-w -I$HERE -I$T/src"
"$CC" $CF $DF -o "$T/gears" "$T/src/gears.c" $FLAGS
echo "$T/gears"
"$CC" $CF $DF -DDEMOS_DEFAULT_DATA_DIR='"PROGDIR:data"' -o "$T/brick" "$T/src/brick.c" "$T/src/shaderutil.c" $FLAGS
echo "$T/brick"
