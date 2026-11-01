#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target library/opengpu):
# opengpu.library alone, as library/build.sh builds it, with the stove's
# compiler (library/build-amiga.sh builds the RTG libraries, not this one).
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$HERE/.." && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
NM=${CC%gcc}nm
SOFT="-specs=$ROOT/tools/m68k-soft-m68040.specs"
mkdir -p "$OUT/libs"
"$CC" --version | head -1
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
    $D3 -o "$OUT/libs/opengpu.library" "$HERE/opengpu/opengpu_lib.c" "$HERE/opengpu/ogpu_core.c" $D3SRC "$HERE/opengpu/ogpu_module.c" "$HERE/string.c" \
    "$OUT/ogfx_lib.o" "$OUT/ogfx_leaves.o" "$OUT/ogfx_gfx_core.o" "$OUT/ogfx_gfx_rp.o" -lgcc
und=$("$NM" -u "$OUT/libs/opengpu.library" || true)
[ -z "$und" ] || { echo "undefined in opengpu.library: $und" >&2; exit 1; }
python3 "$ROOT/tools/hunk_rw_check.py" "$OUT/libs/opengpu.library"
rm -f "$OUT"/*.o
rm -rf "$OUT/ogfx-core"
echo "$OUT/libs/opengpu.library ($(wc -c < "$OUT/libs/opengpu.library") bytes)"
sha256sum "$OUT/libs/opengpu.library"
