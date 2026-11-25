#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# minigl.library's wrapper over GL.module (library/minigl/wrap/): what gen_wrap.py makes from the spec, against
# the spec and against the wrapper's own files. No compiler: the 68k build is library/minigl/build.sh's.
#   tests/minigl/test_wrap.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build/tests}/mglwrap
mkdir -p "$OUT"
python3 "$ROOT/library/minigl/wrap/gen_wrap.py" "$OUT"
python3 "$ROOT/library/minigl/wrap/gen_wrap.py" --list > "$OUT/list.txt"
python3 "$HERE/check_wrap.py" "$ROOT" "$OUT"
