#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The 3D core's and the Vulkan 3D recorder's host tests (tests/run_3d.sh) as one kitchen test.
#   tests/suite/test_3d.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
OUT=$OUT sh "$HERE/../run_3d.sh"
echo "test_3d: tests/run_3d.sh passed"
