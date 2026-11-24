#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# minigl.library and its test programs for the Cradle's stove_build (STOVE= and OUT= set, target library/minigl):
# build.sh's work goes under OUT/work, and what a machine takes is laid out as it sits on its disk.
#   library/minigl/build-amiga.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/build-amiga}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
sh "$HERE/build.sh" "$OUT/work"
mkdir -p "$OUT/libs" "$OUT/lib" "$OUT/c"
cp "$OUT/work/minigl.library" "$OUT/libs/minigl.library"
cp "$OUT/work/libminigl.a" "$OUT/work/libmgl.a" "$OUT/lib/"
cp "$OUT/work/MGLTest" "$OUT/work/MGLGears" "$OUT/work/MGLTestStatic" "$OUT/work/MGLGlutTest" "$OUT/c/"
sha256sum "$OUT/libs/minigl.library" "$OUT/c/MGLTest" "$OUT/c/MGLGears" "$OUT/c/MGLGlutTest"
