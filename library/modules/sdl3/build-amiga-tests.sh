#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (script build-amiga-tests.sh, target
# library/modules/sdl3): build-amiga.sh with SDL's testsprite and
# testrendercopyex too, so a lab can run them: OUT/tests/testsprite and
# testrendercopyex, linked with all of SDL 3 (libSDL3_static.a, so they need
# no SDL3.module; the window present is the same code). The .m forms stay out:
# libSDL3.a's GL part needs GL.module's libGL.a, which this build hasn't.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDL3_BUILD_DIR=${SDL3_BUILD_DIR:-${TMPDIR:-/tmp}/sdl3-build-$$}
export SDL3_BUILD_DIR
T=$SDL3_BUILD_DIR/tests/sdl3
TARGETS="module stub static $T/testsprite $T/testrendercopyex" exec sh "$HERE/build-amiga.sh"
