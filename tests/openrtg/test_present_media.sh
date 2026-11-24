#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# ORTG_Present's MEDIA kind (library/present_media.h, gfopenrtg.library 0.30, M8 step 2.6): an ACMedia frame held
# on the host, written by a fake host into the visible pieces, tested as zero-copy. Also part of tests/run.sh.
#   tests/openrtg/test_present_media.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_present_media" "$ROOT/tests/test_present_media.c"
"$OUT/test_present_media"
