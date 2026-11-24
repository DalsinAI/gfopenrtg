#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# include/gfopengpu/pipe_vg.h, the pipe's numbers for its front ends: it stands on its own, names no virgl command
# (those are library/pipe/virgl_cmd.h's), its numbers are the ones virgl_cmd.h gives the pipe, and Warp3D's and
# MiniGL's virgl files take them from it, not from virgl_cmd.h. Needs gcc-multilib.
#   tests/pipe/test_pipe_vg.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
H="$ROOT/include/gfopengpu/pipe_vg.h"
fail=0
ok=0
check() { if [ "$1" = 0 ]; then ok=$((ok + 1)); else echo "FAIL: $2"; fail=$((fail + 1)); fi; }

# 1. no command in the front ends' header (comments aside)
sed -e 's|/\*.*\*/||g' "$H" | grep -E -v '^[ \t]*\*' > "$OUT/pipe_vg.code"
if grep -E -q '\b(VG_CMD0|VG_CCMD_[A-Z_]+|VG_OBJ_[A-Z_]+|VG_W|VGC_[A-Z_]+|vgc_(bind|destroy|viewport|scissor|framebuffer|clear|vertex_buffer|index_buffer|draw|constants|shader|bind_shader|sampler_views|sampler_states))\b' "$OUT/pipe_vg.code"; then r=1; else r=0; fi
check $r "pipe_vg.h names a virgl command"

# 2. it compiles alone, and its numbers are the ones virgl_cmd.h gives the pipe
cat > "$OUT/test_pipe_vg.c" <<'C'
#include <gfopengpu/pipe_vg.h>
#ifdef VG_CMD0
#error "pipe_vg.h defines VG_CMD0"
#endif
static const ULONG nums[] = {
    VG_FMT_B8G8R8A8_UNORM, VG_FMT_B5G6R5_UNORM, VG_FMT_Z16_UNORM, VG_FMT_D24S8, VG_FMT_Z24X8_UNORM, VG_FMT_R16_UINT,
    VG_BIND_DEPTH_STENCIL, VG_BIND_RENDER_TARGET, VG_BIND_SAMPLER_VIEW, VG_BIND_VERTEX_BUFFER, VG_BIND_INDEX_BUFFER,
    VG_TARGET_2D, VG_CLEAR_COLOR0, VG_PRIM_TRIANGLE_FAN, VG_BF_INV_CONST_ALPHA, VG_MIP_NONE, VG_CAPS_WORDS, VG_BLK_WORDS(12)
};
const ULONG *pipe_vg_nums(void);
ULONG pipe_vg_count(void);
const ULONG *pipe_vg_nums(void) { return nums; }
ULONG pipe_vg_count(void) { return sizeof nums / sizeof nums[0]; }
C
cat > "$OUT/test_pipe_vg_main.c" <<C
#include <stdio.h>
#include "$ROOT/library/pipe/virgl_cmd.h"
const ULONG *pipe_vg_nums(void);
ULONG pipe_vg_count(void);
int main(void)
{
    static const ULONG want[] = {
        1, 7, 16, 20, 21, 185, 1, 2, 8, 16, 32, 2, 4, 6, 0x18, 2, 256, 14
    };
    static const ULONG cmd[] = {
        VG_FMT_B8G8R8A8_UNORM, VG_FMT_B5G6R5_UNORM, VG_FMT_Z16_UNORM, VG_FMT_D24S8, VG_FMT_Z24X8_UNORM, VG_FMT_R16_UINT,
        VG_BIND_DEPTH_STENCIL, VG_BIND_RENDER_TARGET, VG_BIND_SAMPLER_VIEW, VG_BIND_VERTEX_BUFFER, VG_BIND_INDEX_BUFFER,
        VG_TARGET_2D, VG_CLEAR_COLOR0, VG_PRIM_TRIANGLE_FAN, VG_BF_INV_CONST_ALPHA, VG_MIP_NONE, VG_CAPS_WORDS, VG_BLK_WORDS(12)
    };
    const ULONG *got = pipe_vg_nums();
    ULONG i, n = pipe_vg_count(), bad = 0, blk[4], a[2] = { 5, 6 };
    if (n != sizeof want / sizeof want[0]) { printf("FAIL: %lu numbers\n", (unsigned long)n); return 1; }
    for (i = 0; i < n; i++)
        if (got[i] != want[i] || cmd[i] != want[i]) { printf("FAIL: number %lu\n", (unsigned long)i); bad++; }
    vgc_blk_cmd(blk, ACV_PRESENT, 2, a);
    if (vgc_word(blk, 0) != ACV_HDR(ACV_PRESENT, 4) || vgc_result(blk, 0) != 0 || vgc_word(blk, 3) != 6) { printf("FAIL: block\n"); bad++; }
    printf("pipe_vg: %lu numbers, %s\n", (unsigned long)n, bad ? "FAILED" : "the same");
    return bad != 0;
}
C
if cc -m32 -std=gnu99 -DVG_HOST -I"$ROOT/tests/host-include" -I"$ROOT/include" -O1 -Wall -Wextra -Werror \
    -o "$OUT/test_pipe_vg" "$OUT/test_pipe_vg.c" "$OUT/test_pipe_vg_main.c"; then
    if "$OUT/test_pipe_vg"; then r=0; else r=1; fi
else r=1; fi
check $r "pipe_vg.h numbers against virgl_cmd.h"

# 3. the front ends take the numbers from pipe_vg.h, not from virgl_cmd.h
for f in library/warp3d/w3d_vg.c library/minigl/mgl_vg.h; do
    if grep -E -q '#[ \t]*include[ \t]*[<"][^">]*virgl_cmd[.]h' "$ROOT/$f"; then r=1; else r=0; fi
    check $r "$f includes virgl_cmd.h"
    if grep -E -q '#[ \t]*include[ \t]*"[^"]*gfopengpu/pipe_vg[.]h"' "$ROOT/$f"; then r=0; else r=1; fi
    check $r "$f takes pipe_vg.h"
done

echo "test_pipe_vg: $ok passed, $fail failed"
[ "$fail" = 0 ]
