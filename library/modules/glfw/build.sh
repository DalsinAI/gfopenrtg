#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GLFW 3.4 (Zlib) on GFOpenGPU: upstream GLFW with an AmigaOS 3 platform of
# the Team's (src/amiga_*.c: Intuition windows, GL and GLES through GLA),
# built as libglfw3.a with the GCC 16 stove.
#   library/modules/glfw/build.sh [OUT_DIR]     (default build/)
# Needs GLFW's zip from TARBALLS (or downloads it once into
# OUT_DIR/glfw-work), checked against UPSTREAM.json's sha256. NDK is an
# extra include directory for the Amiga's own headers (proto/, inline/,
# intuition/ ...) when the stove's compiler doesn't have them built in.
# Makes OUT_DIR/glfw/, laid out as the kit: include/GLFW/ (glfw3.h,
# glfw3native.h), lib/libglfw3.a, lib/pkgconfig/glfw3.pc,
# lib/cmake/glfw3/, and LICENSE.md, which tools/make_sdk.py --glfw puts in
# the developer kit.
set -eu
export LC_ALL=C
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$HERE/../../.." && pwd)
STOVE16=${STOVE16:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}
TARBALLS=${TARBALLS:-$HOME/AmiCradle-dev/upstream-tarballs}
NDK=${NDK:-}
OUT=${1:-$REPO/build}
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
CC=$STOVE16/bin/m68k-amigaos-gcc
AR=$STOVE16/bin/m68k-amigaos-ar
eval "$(python3 - "$HERE/UPSTREAM.json" <<'PY'
import json, sys
u = json.load(open(sys.argv[1]))["glfw"]
print(f"FILE='{u['file']}' DIR='{u['dir']}' SHA='{u['sha256']}' URL='{u['url']}'")
PY
)"

W=$OUT/glfw-work
mkdir -p "$W"
ZIP=$TARBALLS/$FILE
if [ ! -f "$ZIP" ]; then
    ZIP=$W/$FILE
    [ -f "$ZIP" ] || curl -fsL -o "$ZIP" "$URL"
fi
echo "$SHA  $ZIP" | sha256sum -c --quiet - || { echo "glfw: $FILE's sha256 is not UPSTREAM.json's" >&2; exit 1; }

# A fresh tree each time: GLFW, the patch to its shared files (the platform
# ID, platform.h's lists, glfw3native.h's calls) and the platform's files.
S=$W/$DIR
rm -rf "$S"
python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$ZIP" "$W"
for p in "$HERE"/patches/*.patch; do patch -s -d "$S" -p1 < "$p"; done
cp "$HERE"/src/amiga_*.c "$HERE"/src/amiga_*.h "$S/src/"

# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access. GCC 16 also gets
# -fno-tree-loop-distribute-patterns (its loop-shift miscompile).
CF="-m68040 -m68881 -mnobitfield -O2 -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns -fomit-frame-pointer -noixemul"
INC="-I$S/include -I$REPO/library/modules/gl${NDK:+ -I$NDK}"
O=$W/obj
rm -rf "$O"
mkdir -p "$O"
# GLFW's shared files as upstream's CMake lists them, the Null platform
# (glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL)) and the Amiga's.
for f in context init input monitor platform vulkan window egl_context osmesa_context \
         null_init null_monitor null_window null_joystick; do
    "$CC" $CF -D_GLFW_AMIGAOS $INC -w -c -o "$O/$f.o" "$S/src/$f.c"
done
for f in amiga_init amiga_monitor amiga_window amiga_context amiga_joystick \
         amiga_time amiga_thread amiga_module; do
    "$CC" $CF -D_GLFW_AMIGAOS $INC -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare \
        -c -o "$O/$f.o" "$S/src/$f.c"
done
python3 "$REPO/tools/fpcr_check.py" "${CC%gcc}objdump" "$O"/*.o

K=$OUT/glfw
rm -rf "$K"
mkdir -p "$K/include/GLFW" "$K/lib/pkgconfig" "$K/lib/cmake/glfw3"
"$AR" rcs "$K/lib/libglfw3.a" "$O"/*.o
cp "$S/include/GLFW/glfw3.h" "$S/include/GLFW/glfw3native.h" "$K/include/GLFW/"
cp "$HERE/glfw3.pc" "$K/lib/pkgconfig/glfw3.pc"
cp "$HERE/cmake/glfw3Config.cmake" "$HERE/cmake/glfw3ConfigVersion.cmake" "$K/lib/cmake/glfw3/"
cp "$S/LICENSE.md" "$K/LICENSE.md"
echo "$K/lib/libglfw3.a ($(wc -c < "$K/lib/libglfw3.a") bytes)"
