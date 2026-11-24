#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Every one of the repository's own tests, as pull request CI runs them (LV-16: a PR is green only when its
# repository's tests ran). The tests are found, not listed: tests/run*.sh, tests/pipe_m0.sh, every
# tests/<dir>/test_*.sh and every tests/.../test_*.py, so a new test is in CI the day it is added.
# A test that needs what CI's machine has not got (a stove, an Amiga, a library) is named in
# tests/ci/skip.txt with why; a test may also exit 77 to say it skipped itself.
#   tests/ci/test_all.sh [-list]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SKIP=$HERE/skip.txt
cd "$ROOT" || exit 2

found() {
    for f in tests/run.sh tests/run_*.sh tests/pipe_m0.sh; do [ -f "$f" ] && echo "$f"; done
    find tests -mindepth 2 -name 'test_*.sh' ! -path 'tests/ci/*' | sort
    find tests -mindepth 1 -name 'test_*.py' | sort
}
skipped() { [ -f "$SKIP" ] && grep -v '^#' "$SKIP" | awk '{print $1}' | grep -qx "$1"; }

if [ "${1:-}" = "-list" ]; then found; exit 0; fi

pass=0 fail=0 skip=0 failed=""
LOG=${OUT:-$ROOT/build/tests}/ci
mkdir -p "$LOG"
for t in $(found); do
    if skipped "$t"; then echo "SKIP $t ($(grep "^$t " "$SKIP" | cut -d' ' -f2-))"; skip=$((skip + 1)); continue; fi
    l=$LOG/$(echo "$t" | tr '/' '_').log
    case "$t" in
        *.py) d=$(dirname "$t"); m=$(basename "$t" .py)
              if python3 -c 'import pytest' 2>/dev/null; then (cd "$d" && python3 -m pytest -q "$m.py") >"$l" 2>&1
              else (cd "$d" && python3 -m unittest -v "$m") >"$l" 2>&1; fi ;;
        *)    sh "$t" >"$l" 2>&1 ;;
    esac
    rc=$?
    if [ $rc -eq 0 ]; then echo "PASS $t"; pass=$((pass + 1))
    elif [ $rc -eq 77 ]; then echo "SKIP $t (skipped itself)"; skip=$((skip + 1))
    else echo "FAIL $t (exit $rc), its last lines:"; tail -n 30 "$l" | sed 's/^/    /'; fail=$((fail + 1)); failed="$failed $t"; fi
done
echo "REPO_TESTS pass=$pass fail=$fail skip=$skip"
[ $fail -eq 0 ] || { echo "failed:$failed"; exit 1; }
