#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# library/modules/sdl12/mixer's core and readers on a PC (mixer_host.c), with
# the sanitizers.
#   tests/sdl12/mixer/test_mixer_host.sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
M=$ROOT/library/modules/sdl12/mixer/src
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT HUP INT TERM
CC=${CC:-cc}
$CC -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I"$M" -o "$T/mixer_host" "$HERE/mixer_host.c" "$M/mix_core.c" "$M/mix_wav.c"
"$T/mixer_host"
