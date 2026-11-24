#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# M9's matrix gate: the driver choice (library/opengpu/ogpu_driver_plan.h) for every variable
# combination, with and without virgl, a graphics chip, ACV_PRESENT and the stream route.
#   tests/pipe/test_driver_plan.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I"$ROOT/library/opengpu" -o "$OUT/test_driver_plan" "$ROOT/tests/test_driver_plan.c"
"$OUT/test_driver_plan"
