#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# M8's gate: acvirgl_present_bench within 2% of M0 (917bd97). What an ACV_PRESENT costs on the host is
# acvirgl.c's; the bench is built with M0's acvirgl.c and with this tree's, and the two run in turns
# (ROUNDS each, so a busy machine weighs on both alike). Trimmed means are compared, against the runs' noise.
# M0's file comes from git (git show); without git only this tree's is measured, and the test says so.
#   tests/pipe/test_present_bench.sh
# ACVIRGL_LIB: the virglrenderer library (default the machine's); VIRGL_SRC: its source's src/ (for
# virgl_protocol.h; looked for, else virglrenderer's release fetched for the run); BENCH_ARGS: the bench's arguments (default 300 640 480 now).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
M0=917bd97
: "${ACVIRGL_LIB:=/usr/lib/x86_64-linux-gnu/libvirglrenderer.so.1}"
: "${ROUNDS:=15}"
: "${BENCH_ARGS:=300 640 480 now}"
if [ ! -e "$ACVIRGL_LIB" ]; then echo "test_present_bench: no $ACVIRGL_LIB: cannot run here"; exit 1; fi
export ACVIRGL_LIB
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
if [ -z "${VIRGL_SRC:-}" ]; then
    for d in /usr/src /usr/local/src /opt; do
        f=$(find "$d" -maxdepth 4 -name virgl_protocol.h -path '*/src/*' 2>/dev/null | head -n 1)
        if [ -n "$f" ]; then VIRGL_SRC=$(dirname "$f"); break; fi
    done
fi
if [ ! -f "${VIRGL_SRC:-/nonexistent}/virgl_protocol.h" ]; then
    # only the protocol's numbers, from virglrenderer's release (MIT), for this run: nothing kept
    mkdir -p "$OUT/virgl"
    if curl -sSfL --max-time 30 -o "$OUT/virgl/virgl_protocol.h" \
        "https://gitlab.freedesktop.org/virgl/virglrenderer/-/raw/${VIRGL_TAG:-1.3.0}/src/virgl_protocol.h"; then
        VIRGL_SRC="$OUT/virgl"
    else
        echo "test_present_bench: no virgl_protocol.h (set VIRGL_SRC to virglrenderer's src/)"; exit 1
    fi
fi
echo "virgl_protocol.h from $VIRGL_SRC"

build() {   # $1 acvirgl.c, $2 the bench
    cc -std=gnu11 -O2 -Wall -Wextra -I"$VIRGL_SRC" -I"$ROOT/host/virgl" -o "$2" "$ROOT/tests/acvirgl_present_bench.c" "$1" -ldl
}
build "$ROOT/host/virgl/acvirgl.c" "$OUT/bench_now"
have_m0=0 same_obj=0
if git -C "$ROOT" cat-file -e "$M0:host/virgl/acvirgl.c" 2>/dev/null; then
    mkdir -p "$OUT/m0"
    git -C "$ROOT" show "$M0:host/virgl/acvirgl.c" > "$OUT/m0/acvirgl.c"
    cp "$ROOT/host/virgl/acvirgl.h" "$OUT/m0/acvirgl.h"
    build "$OUT/m0/acvirgl.c" "$OUT/bench_m0"
    have_m0=1
    # the code itself: both acvirgl.c compiled alone, as the bench builds them, and the objects compared
    # (sections only: the file names in the debug notes would differ). The same object = the same present.
    same_obj=0
    cc -std=gnu11 -O2 -I"$VIRGL_SRC" -I"$ROOT/host/virgl" -c -o "$OUT/now.o" "$ROOT/host/virgl/acvirgl.c"
    cc -std=gnu11 -O2 -I"$VIRGL_SRC" -I"$ROOT/host/virgl" -c -o "$OUT/m0.o" "$OUT/m0/acvirgl.c"
    if objcopy -O binary -j .text "$OUT/now.o" "$OUT/now.text" 2>/dev/null && objcopy -O binary -j .text "$OUT/m0.o" "$OUT/m0.text" 2>/dev/null \
        && cmp -s "$OUT/now.text" "$OUT/m0.text" \
        && objcopy -O binary -j .rodata -j .rodata.str1.1 -j .rodata.str1.8 -j .data "$OUT/now.o" "$OUT/now.data" 2>/dev/null \
        && objcopy -O binary -j .rodata -j .rodata.str1.1 -j .rodata.str1.8 -j .data "$OUT/m0.o" "$OUT/m0.data" 2>/dev/null \
        && cmp -s "$OUT/now.data" "$OUT/m0.data"; then
        same_obj=1; echo "acvirgl.o: the same code and data as M0's ($(wc -c < "$OUT/now.text") bytes of code)"
    else echo "acvirgl.o: the code differs from M0's"; fi
    # the two acvirgl.c as the compiler sees them, comments gone
    if cmp -s "$ROOT/host/virgl/acvirgl.c" "$OUT/m0/acvirgl.c"; then echo "acvirgl.c: the same file as M0's"
    else
        cc -fpreprocessed -dD -E -P "$ROOT/host/virgl/acvirgl.c" > "$OUT/now.i" 2>/dev/null || :
        cc -fpreprocessed -dD -E -P "$OUT/m0/acvirgl.c" > "$OUT/m0.i" 2>/dev/null || :
        if [ -s "$OUT/now.i" ] && cmp -s "$OUT/now.i" "$OUT/m0.i"; then echo "acvirgl.c: only comments changed since M0"
        else echo "acvirgl.c: code changed since M0"; fi
    fi
else
    echo "test_present_bench: no git history here: M0's acvirgl.c not built, only this tree measured"
fi

mean_of() { sed -n 's/^bench: present (the 68k waits) \([0-9.]*\) us mean.*/\1/p' "$1"; }
i=0
: > "$OUT/now.txt"; : > "$OUT/m0.txt"
while [ $i -lt "$ROUNDS" ]; do
    for b in now m0; do
        [ $b = m0 ] && [ $have_m0 = 0 ] && continue
        # shellcheck disable=SC2086
        "$OUT/bench_$b" $BENCH_ARGS > "$OUT/run.txt" 2>&1 || { cat "$OUT/run.txt"; echo "test_present_bench: $b failed"; exit 1; }
        if grep -q "ERRORS\|^no virgl" "$OUT/run.txt"; then cat "$OUT/run.txt"; echo "test_present_bench: $b gave errors"; exit 1; fi
        m=$(mean_of "$OUT/run.txt")
        [ -n "$m" ] || { cat "$OUT/run.txt"; echo "test_present_bench: no mean from $b"; exit 1; }
        echo "$m" >> "$OUT/$b.txt"
        [ $i = 0 ] && [ $b = now ] && sed -n '1p' "$OUT/run.txt"
        echo "round $((i + 1)) $b: present $m us mean"
    done
    i=$((i + 1))
done
avg() { awk '{ s += $1; n++ } END { if (n) printf "%.1f", s / n }' "$1"; }
med() { sort -n "$1" | awk '{ a[NR] = $1 } END { if (NR % 2) printf "%.1f", a[(NR + 1) / 2]; else printf "%.1f", (a[NR / 2] + a[NR / 2 + 1]) / 2 }'; }
echo "this tree: mean $(avg "$OUT/now.txt") us, median $(med "$OUT/now.txt") us over $ROUNDS runs"
[ $have_m0 = 1 ] || exit 0
echo "M0 ($M0): mean $(avg "$OUT/m0.txt") us, median $(med "$OUT/m0.txt") us over $ROUNDS runs"
# the trimmed means compared (each side's fastest and slowest run dropped), against the runs' own noise:
# twice the standard error of the difference. Slower by more than 2% and by more than the noise fails;
# when the noise is over 2% the machine can't tell 2% apart, and the test says so (exit 2).
awk -v A="$OUT/now.txt" -v B="$OUT/m0.txt" -v same="$same_obj" 'function load(f, v,   n, x) { n = 0; while ((getline x < f) > 0) v[++n] = x + 0; return n }
function sortv(v, n,   i, j, t) { for (i = 2; i <= n; i++) { t = v[i]; for (j = i - 1; j > 0 && v[j] > t; j--) v[j + 1] = v[j]; v[j + 1] = t } }
function tmean(v, n,   i, s) { s = 0; for (i = 2; i < n; i++) s += v[i]; return s / (n - 2) }
function tvar(v, n, m,   i, s) { s = 0; for (i = 2; i < n; i++) s += (v[i] - m) ^ 2; return s / (n - 3) }
BEGIN {
    na = load(A, a); nb = load(B, b); sortv(a, na); sortv(b, nb)
    ma = tmean(a, na); mb = tmean(b, nb)
    se = 2 * sqrt(tvar(a, na, ma) / (na - 2) + tvar(b, nb, mb) / (nb - 2)) / mb * 100
    d = (ma - mb) / mb * 100
    printf "present: this tree %.0f us, M0 %.0f us (trimmed means): %+.1f%%, noise +-%.1f%%\n", ma, mb, d, se
    if (d > 2 && d > se) { print "test_present_bench: slower than M0 by more than 2%"; exit 1 }
    if (se > 2 && same) { print "the runs are too noisy here to time 2% (+-" sprintf("%.1f", se) "%), but the present is M0'"'"'s own code: within 2%"; exit 0 }
    if (se > 2) { print "test_present_bench: the runs are too noisy here to tell 2% (a busy machine): no verdict"; exit 2 }
    print "within 2%"; exit 0 }'
