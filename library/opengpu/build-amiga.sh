#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target library/opengpu):
# gfopengpu.library alone, as library/build.sh builds it, with the stove's
# compiler (library/build-amiga.sh builds the RTG libraries, not this one).
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$HERE/.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
NM=${CC%gcc}nm
SOFT="-specs=$ROOT/tools/m68k-soft-m68040.specs"
mkdir -p "$OUT/libs"
"$CC" --version | head -1
# The release build (library/build.sh) takes only GFOpenAmigaGCC 16.2.0-open4 or -open5;
# this one, for the Cradle's stove, builds with what the stove has and says so.
"$CC" --version | head -1 | grep -q -E "OpenAmigaGCC 16.2.0-open[45]\)" \
    || echo "note: not GFOpenAmigaGCC 16.2.0-open4 or -open5, which the release gfopengpu.library is built with" >&2
D3= D3SRC=
if [ -f "$HERE/opengpu/ogpu_3d.c" ]; then D3=-DOGPU_WITH_3D; D3SRC="$HERE/opengpu/ogpu_3d.c"; fi
sh "$ROOT/tools/fetch_ogfx_core.sh" "$OUT"
for f in gfx_core gfx_rp; do
    "$CC" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
        -I"$OUT/ogfx-core" -c -o "$OUT/ogfx_$f.o" "$OUT/ogfx-core/$f.c"
done
"$CC" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
    -I"$ROOT/include" -I"$OUT/ogfx-core" -c -o "$OUT/ogfx_lib.o" "$HERE/ogfx/ogfx_lib.c"
"$CC" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror \
    -c -o "$OUT/ogfx_leaves.o" "$HERE/ogfx/ogfx_leaves.c"
# shellcheck disable=SC2086
"$CC" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
    -nostartfiles -nostdlib $SOFT -I"$ROOT/include" \
    $D3 -o "$OUT/libs/gfopengpu.library" "$HERE/opengpu/opengpu_lib.c" "$HERE/opengpu/ogpu_core.c" $D3SRC "$HERE/opengpu/ogpu_module.c" "$HERE/opengpu/ogpu_retained.c" "$HERE/string.c" \
    "$OUT/ogfx_lib.o" "$OUT/ogfx_leaves.o" "$OUT/ogfx_gfx_core.o" "$OUT/ogfx_gfx_rp.o" -lgcc
und=$("$NM" -u "$OUT/libs/gfopengpu.library" || true)
[ -z "$und" ] || { echo "undefined in gfopengpu.library: $und" >&2; exit 1; }
python3 "$ROOT/tools/hunk_rw_check.py" "$OUT/libs/gfopengpu.library"
rm -f "$OUT"/*.o
rm -rf "$OUT/ogfx-core"
echo "$OUT/libs/gfopengpu.library ($(wc -c < "$OUT/libs/gfopengpu.library") bytes)"
sha256sum "$OUT/libs/gfopengpu.library"
