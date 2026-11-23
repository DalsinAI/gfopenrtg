#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The version stamps of minigl.library and Warp3D.library, read from their sources: the $VER line is "name V.R
# (day.month.year)" with a real date, the library's ID string says the same, V.R are the numbers the library reports,
# and the date is no older than the day the shared pipe went under the library (23.11.2026).
#   tests/stamps/test_stamps.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/../.."
checks=0
failed=0
ok() { checks=$((checks + 1)); }
bad() { checks=$((checks + 1)); failed=$((failed + 1)); echo "FAIL $1"; }

# stamp NAME SOURCE HEADER PREFIX
stamp() {
    name=$1; src=$ROOT/$2; hdr=$ROOT/$3; pre=$4
    id=$(grep -E "^#define [A-Z0-9_]*_ID \"" "$src" | sed 's/^#define [A-Z0-9_]*_ID "\(.*\)"$/\1/' || true)
    if [ -n "$id" ]; then
        ver=$id; lib=$id
    else
        ver=$(grep -E 'lib_ver\[\]' "$src" | sed 's/.*"\\0\$VER: \(.*\)";$/\1/')
        lib=$(grep -E 'lib_id\[\]' "$src" | sed 's/.*= "\(.*\)\\r\\n";$/\1/')
    fi
    if [ -n "$ver" ]; then ok; else bad "$name: no \$VER line found in $2"; return; fi
    if [ "$ver" = "$lib" ]; then ok; else bad "$name: the ID string and the \$VER line differ ($lib / $ver)"; fi
    if grep -q 'lib_ver\[\].*"\\0\$VER: ' "$src"; then ok; else bad "$name: lib_ver does not start with \\0\$VER: "; fi
    head=$(echo "$ver" | sed -n "s/^$name \([0-9][0-9]*\)\.\([0-9][0-9]*\) (\([0-9][0-9]*\)\.\([0-9][0-9]*\)\.\([0-9][0-9][0-9][0-9]\)) .*/\1 \2 \3 \4 \5/p")
    if [ -n "$head" ]; then ok; else bad "$name: not \"$name V.R (d.m.yyyy) ...\": $ver"; return; fi
    set -- $head
    v=$1; r=$2; d=$3; m=$4; y=$5
    hv=$(sed -n "s/^#define ${pre}_LIB_VERSION  *\([0-9][0-9]*\).*/\1/p" "$hdr")
    hr=$(sed -n "s/^#define ${pre}_LIB_REVISION  *\([0-9][0-9]*\).*/\1/p" "$hdr")
    if [ "$v" = "$hv" ] && [ "$r" = "$hr" ]; then ok; else bad "$name: stamped $v.$r, reports $hv.$hr"; fi
    if [ "$d" -ge 1 ] && [ "$d" -le 31 ] && [ "$m" -ge 1 ] && [ "$m" -le 12 ]; then ok; else bad "$name: $d.$m.$y is not a date"; fi
    if [ $((y * 10000 + m * 100 + d)) -ge 20261123 ]; then ok; else bad "$name: stamped $d.$m.$y, older than the pipe under it (23.11.2026)"; fi
}

stamp minigl.library library/minigl/mgl_lib.c library/minigl/mgl_internal.h MGL
stamp Warp3D.library library/warp3d/w3d_lib.c library/warp3d/w3d_internal.h W3D
echo "version stamps: $checks checks, $failed failed"
[ "$failed" = 0 ]
