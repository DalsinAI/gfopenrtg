#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The GL module's own PC test as the kitchen runs it: library/modules/gl/tests/run.sh (Mesa and the GLA core built
# for this machine, softpipe against the stored pictures, and virgl on llvmpipe through the machine's
# libvirglrenderer: the old route and the pipe's, picture for picture). The first run builds Mesa and is long.
#   tests/gl/test_gla.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
: "${ACVIRGL_LIB:=/usr/lib/x86_64-linux-gnu/libvirglrenderer.so.1}"
if [ ! -e "$ACVIRGL_LIB" ]; then echo "test_gla: no $ACVIRGL_LIB: the virgl half cannot run here"; exit 1; fi
export ACVIRGL_LIB
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
exec sh "$HERE/../../library/modules/gl/tests/run.sh"
