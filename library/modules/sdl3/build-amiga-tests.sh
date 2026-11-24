#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (script build-amiga-tests.sh, target
# library/modules/sdl3): build-amiga.sh with the test programs too, so a lab
# can run them: OUT/tests/testsprite.m, testrendercopyex.m, rendercompare.m,
# cursor_lifecycle.m and nomodule.m on SDL3.module, and the static ones.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGETS="module stub static tests" exec sh "$HERE/build-amiga.sh"
