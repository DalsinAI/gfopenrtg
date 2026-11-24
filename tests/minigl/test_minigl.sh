#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# minigl.library's PC tests (tests/run_minigl.sh: its own renderer under the sanitizers, the golden scene, the
# ABI against the spec), under a name the kitchen's repo_test runs.
#   tests/minigl/test_minigl.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
exec sh "$HERE/../run_minigl.sh"
