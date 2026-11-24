#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# minigl.library and its test programs for the Cradle's stove_build (STOVE= and OUT= set, target library/minigl):
# build.sh's work is kept out of OUT (in a folder of its own, removed after), and what a machine takes is laid out in
# OUT as it sits on its disk.
#   library/minigl/build-amiga.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/build-amiga}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
W=$(mktemp -d "${TMPDIR:-/tmp}/mgl-build.XXXXXX")
trap 'rm -rf "$W"' EXIT
sh "$HERE/build.sh" "$W"
mkdir -p "$OUT/libs" "$OUT/lib" "$OUT/c"
cp "$W/minigl.library" "$OUT/libs/minigl.library"
cp "$W/libminigl.a" "$W/libmgl.a" "$OUT/lib/"
cp "$W/MGLTest" "$W/MGLGears" "$W/MGLTestStatic" "$W/MGLGlutTest" "$OUT/c/"
sha256sum "$OUT/libs/minigl.library" "$OUT/c/MGLTest" "$OUT/c/MGLGears" "$OUT/c/MGLGlutTest"
