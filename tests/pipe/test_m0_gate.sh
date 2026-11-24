#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The shared pipe's gate as the kitchen runs it: tests/pipe_m0.sh the way its captures were made, on llvmpipe with
# the machine's libvirglrenderer (ACVIRGL_LIB names another). It must end "as captured".
#   tests/pipe/test_m0_gate.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
: "${ACVIRGL_LIB:=/usr/lib/x86_64-linux-gnu/libvirglrenderer.so.1}"
if [ ! -e "$ACVIRGL_LIB" ]; then echo "test_m0_gate: no $ACVIRGL_LIB: the virgl half cannot run here"; exit 1; fi
export ACVIRGL_LIB
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
exec sh "$HERE/../pipe_m0.sh"
