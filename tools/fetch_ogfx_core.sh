#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GFOpenGfx's drawing core (DalsinAI/gfopengfx library/core) at a pinned
# commit, checked by sha256, into OUT/ogfx-core: the one copy of the core
# lives in gfopengfx, and gfopengpu.library builds it from there.
#   tools/fetch_ogfx_core.sh OUT_DIR
# OGFX_CORE_DIR=<a gfopengfx checkout> uses that instead of fetching (its
# files must still match the pin).
set -eu
OUT=${1:?usage: fetch_ogfx_core.sh OUT_DIR}
PIN=da2cdd46056ca6e13bdd0a7cd64a61f13163cfd3
REPO=${OGFX_CORE_REPO:-https://github.com/DalsinAI/gfopengfx.git}
SUMS='14a7b52f10c1c678bf79f634e91f3af4577ee7cf9ac91ce46862936b302eb4a8  gfx_core.c
57117320dd3b0233d9d3b98b627ef37c699399818e170301cccf8762bb4da661  gfx_core.h
984576d3e8acbb6ff31ebe26b8a61d5a4901b877e8636c51910e63b5f7624cc7  gfx_rp.c
5af25eb2d67450720739501cf5ec63d75197d78119a54f62c07759b8be1843dc  gfx_rp.h'
DST="$OUT/ogfx-core"
mkdir -p "$DST"
if (cd "$DST" && echo "$SUMS" | sha256sum -c --quiet) >/dev/null 2>&1; then exit 0; fi
if [ -n "${OGFX_CORE_DIR:-}" ]; then
    SRC="$OGFX_CORE_DIR/library/core"
else
    TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
    git -C "$TMP" init -q
    git -C "$TMP" fetch -q --depth 1 "$REPO" "$PIN"
    git -C "$TMP" checkout -q FETCH_HEAD
    SRC="$TMP/library/core"
fi
for f in gfx_core.c gfx_core.h gfx_rp.c gfx_rp.h; do cp "$SRC/$f" "$DST/$f"; done
(cd "$DST" && echo "$SUMS" | sha256sum -c --quiet) || { echo "fetch_ogfx_core.sh: $DST does not match gfopengfx $PIN" >&2; exit 1; }
echo "GFOpenGfx core: gfopengfx $PIN"
