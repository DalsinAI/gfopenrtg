#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# tests/test_ortg_frame.c on its own (tests/run.sh runs it too), for the kitchen's repo_test:
# SDL2's and SDL3's window presents through ORTG_Present (library/modules/common/ortg_frame.h).
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$HERE/../build/tests}
mkdir -p "$OUT"
cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I"$HERE/gla_present-include" -I"$HERE/../include" -I"$HERE/../library/modules/common" \
    -o "$OUT/test_ortg_frame" "$HERE/test_ortg_frame.c"
"$OUT/test_ortg_frame"
