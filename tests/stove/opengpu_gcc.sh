#!/bin/sh
# SPDX-License-Identifier: MIT
# The compilers library/build.sh takes for gfopengpu.library (OPENGPU_GCC and
# opengpu_gcc_ok, read from the script itself) and the note in
# library/opengpu/build-amiga.sh: open9 (os32-gcc16 since 18 Nov 2026) and open8
# (the stove from 14 Nov 2026) are taken; open7, open10 and an unnamed GCC are
# not. Host only: sh tests/stove/opengpu_gcc.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fails=0 checks=0
eval "$(grep -E '^OPENGPU_GCC=' "$ROOT/library/build.sh")"
eval "$(grep -E '^opengpu_gcc_ok\(\)' "$ROOT/library/build.sh")"
note=$(grep -oE 'OpenAmigaGCC 16\.2\.0-open\[[0-9]+\]' "$ROOT/library/opengpu/build-amiga.sh")
fake() { printf '#!/bin/sh\necho "m68k-amigaos-gcc (%s) 16.2.0b"\n' "$1" > "$T/gcc"; chmod +x "$T/gcc"; OPENGPU_CC=$T/gcc; }
check() {   # check NAME WANT(0 taken, 1 refused)
    checks=$((checks + 1)); fake "$1"
    opengpu_gcc_ok; got=$?
    printf '%s\n' "$($T/gcc --version)" | grep -q -E "${note%\]}\])" ; nt=$?
    if [ "$got" != "$2" ] || [ "$nt" != "$2" ]; then echo "FAIL: $1: build.sh $got, note $nt, want $2"; fails=$((fails + 1)); fi
}
check "GFOpenAmigaGCC 16.2.0-open9" 0
check "GFOpenAmigaGCC 16.2.0-open8" 0
check "GFOpenAmigaGCC 16.2.0-open6" 0
check "GFOpenAmigaGCC 16.2.0-open5" 0
check "OpenAmigaGCC 16.2.0-open4" 0
check "GFOpenAmigaGCC 16.2.0-open7" 1
check "GFOpenAmigaGCC 16.2.0-open10" 1
check "GCC" 1
echo "opengpu_gcc: $checks checks, $fails failed"
[ "$fails" = 0 ]
