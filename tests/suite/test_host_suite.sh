#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The whole host suite (tests/run.sh) as one kitchen test, so a branch can be run through
# repo_test before its merge request while bigguy CI doesn't run a PR's own tests (LV-16).
#   tests/suite/test_host_suite.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
OUT=$OUT sh "$HERE/../run.sh"
echo "test_host_suite: tests/run.sh passed"
