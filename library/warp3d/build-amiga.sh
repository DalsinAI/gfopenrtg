#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target library/warp3d):
# Warp3D.library, W3DTest, W3DCheck and Warp3DPrefs, as build.sh builds them.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build-amiga}
mkdir -p "$OUT"
"${CC:-${STOVE:-$HOME/AmiCradle/stoves/os32}/prefix/bin/m68k-amigaos-gcc}" --version | head -1
exec sh "$HERE/build.sh" "$OUT"
