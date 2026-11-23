#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (LICENSE).
# tests/ownership_check.sh against small trees made here: what it must catch, and what it must let through.
#   sh tests/test_ownership_check.sh
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CHECK="$HERE/ownership_check.sh"
T=$(mktemp -d) || exit 2
trap 'rm -rf "$T"' EXIT HUP INT TERM
fails=0
checks=0

put() { mkdir -p "$T/tree/$(dirname "$1")"; printf '%s\n' "$2" > "$T/tree/$1"; }
fresh() { rm -rf "$T/tree"; mkdir -p "$T/tree/tests"; : > "$T/tree/tests/ownership.debt"; }
# expect NAME EXIT [TEXT-IN-OUTPUT]
expect() {
    checks=$((checks + 1))
    out=$(sh "$CHECK" --root "$T/tree" 2>&1); rc=$?
    if [ "$rc" != "$2" ]; then fails=$((fails + 1)); echo "FAIL $1: exit $rc, wanted $2"; echo "$out" | sed 's/^/     /'; return; fi
    if [ $# -ge 3 ] && ! echo "$out" | grep -q -F -- "$3"; then fails=$((fails + 1)); echo "FAIL $1: output lacks: $3"; echo "$out" | sed 's/^/     /'; fi
}

# 1. what each owner may do
fresh
put library/pipe/pipe_block.c '#include "virgl_cmd.h"
static void f(void) { p[0] = ACV_PRESENT; q = ACV_HELLO; ogpu_triangles(b, 0, 0, 0); vg_cache_find(); }
static struct vg_cache c;'
put library/opengpu/ogpu_core.c '#include "host/virgl/acvirgl.h"
case OGPU_OP_TRIANGLES: cd = FindConfigDev(NULL, 1, 2); x = ACV_CAPS;'
put library/ogfx/ogfx_lib.c 'st->amicradle = FindConfigDev(NULL, M, -1) != NULL;'
put library/screens.c 'w[0] = ACV_PRESENT_SCALE; cd = FindConfigDev(cd, m, p); a = ACV_CAPSET_INFO;'
put compat/cybergraphics_lib.c 'cd = FindConfigDev(NULL, 1, 2);'
put library/gfopenrtg_lib.sfd 'LONG ORTG_Present(struct ORTG_PresentInfo *info)(a0)'
put library/opengpu/gfopengpu_lib.sfd 'LONG OGPU_RetainedPresent(struct OGPURetained *rs)(a0)'
put library/minigl/mgl_lib.c 'ULONG a = OGPU_Query(OGPU_OP_TRIANGLES, OGPU_FMT_ARGB32);
#if defined(OGPU_OP_TRIANGLES)
#endif
/* ACV_PRESENT and ACV_HELLO are the pipe business now */
 * the old vg_cache went in M4
// FindConfigDev is not ours to call'
put host/virgl/acvirgl.c '#include "virglrenderer.h"
x = ACV_HELLO;'
put tests/test_x.c '#include "host/virgl/acvirgl.h"
#include "virgl_cmd.h"'
put library/modules/gl/tests/test_gla.c '#include "host/virgl/acvirgl.h"'
put include/gfopengpu/virgl.h '#define ACV_PRESENT 7
#define ACV_HELLO 1'
put docs/design/note.md 'ACV_PRESENT FindConfigDev vg_cache'
expect "the owners' own code, comments, queries, host/, tests and include/ pass" 0 "none new, none stale"

# 2. the gate of M1: a fresh include of virgl_cmd.h in library/egl fails
put library/egl/egl.c '#include "../../include/gfopengpu/virgl_cmd.h"'
expect "virgl_cmd.h outside the pipe" 1 "virgl-cmd library/egl/egl.c"

# 3. one of every rule, each named with its file
fresh
put library/egl/a.c '#include <virglrenderer.h>'
put library/warp3d/b.sfd 'LONG W3D_PresentFrame(struct W3DContext *c)(a0)'
put library/ogles3/c.c '    blk[n++] = ACV_PRESENT_FLIP_Y;'
put library/tinygl/d.c '    ogpu_render3d(&b, args);'
put library/tinygl/d2.c '    case OGPU_OP_CLEAR3D:'
put library/egl/e.c 'static const char *egl_fs_text(ULONG key);'
put library/egl/e2.c 'static UBYTE ring[VG_RING];'
put library/ogles3/f.c '    req[0] = ACV_CAPSET_INFO;'
put library/warp3d/g.c '    cd = FindConfigDev(NULL, 0xDA15, 9);'
put library/modules/gl/gla/os3/gla_present_os3.c '    WritePixelArray(p, 0, 0, s, rp, x, y, w, h, RECTFMT_ARGB);'
put library/modules/sdl2/patches/sdl2/0001-x.patch '+    ogpu_triangles(&data->batch, v, n, 20, 0, 0, 0, slot, flags);'
for want in "host-header library/egl/a.c" "present-export library/warp3d/b.sfd" "acv-present library/ogles3/c.c" \
            "stream3d library/tinygl/d.c" "stream3d library/tinygl/d2.c" "state-cache library/egl/e.c" \
            "state-cache library/egl/e2.c" "host-caps library/ogles3/f.c" "board-open library/warp3d/g.c" \
            "mesa-present library/modules/gl/gla/os3/gla_present_os3.c" \
            "stream3d library/modules/sdl2/patches/sdl2/0001-x.patch"; do
    expect "caught: $want" 1 "$want"
done

# 4. the same tree with every line on the debt list passes, and says how many
sh "$CHECK" --root "$T/tree" --list > "$T/tree/tests/ownership.debt"
expect "all of it on the debt list" 0 "11 debt lines"

# 5. a debt line whose violation is gone must be deleted
rm "$T/tree/library/warp3d/g.c"
expect "a stale debt line" 1 "no longer break their rule"
expect "the stale line is named" 1 "board-open library/warp3d/g.c"

# 6. comments and blank lines in the debt list are read past
fresh
put library/egl/a.c '#include <virglrenderer.h>'
printf '# why\n\nhost-header   library/egl/a.c    # until M2\n' > "$T/tree/tests/ownership.debt"
expect "a debt line with a comment" 0 "1 debt lines"

# 7. --base: the debt list may lose lines, never gain one
if command -v git >/dev/null 2>&1; then
    checks=$((checks + 2))
    ( cd "$T/tree" && git init -q . && git add -A && git -c user.name=t -c user.email=t@t commit -q -m base ) >/dev/null 2>&1
    put library/egl/b.c '#include "virgl_cmd.h"'
    printf 'host-header library/egl/a.c\nvirgl-cmd library/egl/b.c\n' > "$T/tree/tests/ownership.debt"
    ( cd "$T/tree" && git add -A && git -c user.name=t -c user.email=t@t commit -q -m more ) >/dev/null 2>&1
    out=$(sh "$CHECK" --root "$T/tree" --base HEAD~1 2>&1); rc=$?
    if [ "$rc" != 1 ] || ! echo "$out" | grep -q -F "gained these lines"; then fails=$((fails + 1)); echo "FAIL --base: a line added to the debt list wasn't refused (exit $rc)"; echo "$out" | sed 's/^/     /'; fi
    out=$(sh "$CHECK" --root "$T/tree" --base HEAD 2>&1); rc=$?
    if [ "$rc" != 0 ]; then fails=$((fails + 1)); echo "FAIL --base: an unchanged debt list was refused (exit $rc)"; echo "$out" | sed 's/^/     /'; fi
fi

echo "test_ownership_check: $checks checks, $fails failed"
[ "$fails" = 0 ]
