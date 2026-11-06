#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# What virglrenderer hands the Amiga on this machine (virgl_caps_here.c):
# the GPU it draws on and whether its caps let Mesa's virgl driver give GLES
# 3.1 with compute shaders, SSBOs and image load/store. Desktop GL (as the
# runtime asks first), then a GLES host context (AC_VIRGL_GLES=1).
# virglrenderer: ACVIRGL_LIB, else the one amicradle's
# scripts/build_virglrenderer.sh built into its cache, whose source gives
# virgl_hw.h (VIRGL_SRC names another). Skipped without them; nothing is
# fetched.
#   sh tests/virgl/test_virgl_caps.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$here/../..
cache=${AC_BUILD_CACHE:-$HOME/.cache/amicradle}/virglrenderer-1.3.0
src=${VIRGL_SRC:-$cache/virglrenderer-1.3.0/src}
lib=${ACVIRGL_LIB:-}
[ -n "$lib" ] || lib=$(ls "$cache"/build/src/libvirglrenderer.so.1 2>/dev/null | head -1 || true)
[ -n "$lib" ] && [ -f "$lib" ] || { echo "skipped: no libvirglrenderer.so.1 (ACVIRGL_LIB, or $cache/build)"; exit 0; }
[ -f "$src/virgl_hw.h" ] || { echo "skipped: no virgl_hw.h in $src (VIRGL_SRC)"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I"$src" -o "$tmp/virgl_caps_here" "$here/virgl_caps_here.c" "$top/host/virgl/acvirgl.c" -ldl
echo "virglrenderer: $lib"
rc=0
"$tmp/virgl_caps_here" "$lib" || rc=$?
[ "$rc" = 2 ] && { echo "skipped: virglrenderer didn't start here"; exit 0; }
grc=0
echo "--- a GLES host context (AC_VIRGL_GLES=1)"
AC_VIRGL_GLES=1 "$tmp/virgl_caps_here" "$lib" || grc=$?
[ "$grc" = 2 ] && echo "(no GLES host context here)"
[ "$rc" = 0 ] || exit 1
echo "test_virgl_caps.sh: passed"
