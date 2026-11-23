#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (LICENSE).
#
# The single-owner rules of docs/design/OpenGPU-Shared-Pipe.md (section 6, the ownership matrix), checked
# mechanically. Stage M1 of docs/design/OpenGPU-Shared-Pipe-Plan.md.
#
#   sh tests/ownership_check.sh                 check the tree against tests/ownership.debt
#   sh tests/ownership_check.sh --list          print what breaks a rule today, as debt lines
#   sh tests/ownership_check.sh --base REF      also fail when the debt list gained a line since REF
#   sh tests/ownership_check.sh --root DIR      check another tree (the self-test does)
#
# It fails (exit 1) when:
#   - a file breaks a rule and isn't on the debt list: a new violation;
#   - a debt line no longer breaks its rule: the line must go, so the list is always exactly today's debt;
#   - with --base, the debt list has a line that REF's list hasn't: debt is paid down, never added to.
#
# A rule is broken by a file, not by a line: one debt line covers the file for that rule. Lines that are only a
# comment don't count, and a patch file's "+" and "-" are looked through.
#
# Whose a file is:
#   the pipe      library/pipe/                                   (from M2)
#   gfopengpu     library/opengpu/, library/ogfx/                 (OGFX is built into gfopengpu.library)
#   OpenRTG       the files directly in library/, and compat/     (openrtg_lib.c, screens.c, pixels.c, ...)
# Not looked at: host/ (it is the host), any tests drawer, docs/, measurements/, tools/, team6/, and the two
# headers that define the protocols themselves (include/gfopengpu/virgl.h, stream3d.h and their kin in include/).
#
# The rules:
#   host-header     only gfopengpu and OpenRTG include acvirgl.h, virglrenderer.h or the ACRTG host header
#   virgl-cmd       only the pipe includes virgl_cmd.h (Mesa's driver in GL.module uses Mesa's own header)
#   present-export  only OpenRTG's function table has a present (OGPU_RetainedPresent stays, as a wrapper)
#   acv-present     only the pipe and OpenRTG write ACV_PRESENT
#   stream3d        only the pipe and gfopengpu write the stream's 3D commands: its opcodes by name, or the
#                   builder's calls (ogpu_triangles and its kin). Asking whether one is done isn't writing
#   state-cache     no state cache, fixed-function shader text or vertex ring outside the pipe
#   host-caps       only the pipe, gfopengpu and OpenRTG send ACV_HELLO, ACV_CAPSET_INFO or ACV_CAPS
#   board-open      only gfopengpu and OpenRTG look a board up (FindConfigDev)
#   mesa-present    GL.module's frames reach the screen through OpenRTG only: no WritePixelArray, ogpu_pixels or
#                   LockBitMapTags in gla_present_os3.c (from M8; on the debt list until then)

set -u
ROOT=.
DEBT=
BASE=
LIST=0
while [ $# -gt 0 ]; do
    case "$1" in
        --root) ROOT=$2; shift 2 ;;
        --debt) DEBT=$2; shift 2 ;;
        --base) BASE=$2; shift 2 ;;
        --list) LIST=1; shift ;;
        *) echo "ownership_check: unknown argument $1" >&2; exit 2 ;;
    esac
done
cd "$ROOT" || exit 2
[ -n "$DEBT" ] || DEBT=tests/ownership.debt

TMP=$(mktemp -d) || exit 2
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

# The files that are looked at.
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then git ls-files; else find . -type f | sed 's|^\./||'; fi |
    grep -E '\.(c|h|cc|cpp|S|s|patch|diff|sfd)$' |
    grep -v -E '^(host|tests|docs|measurements|tools|team6|include)/|(^|/)tests/' |
    sort > "$TMP/files"

# rule NAME FILE-FILTER(ERE on the path, "" for all) ALLOWED(ERE on the path) SKIP-LINE(ERE, "" none) PATTERN(ERE)
# prints "NAME path" for every file with a code line matching PATTERN whose path isn't ALLOWED.
rule() {
    name=$1; only=$2; allowed=$3; skip=$4; pattern=$5
    if [ -n "$only" ]; then grep -E "$only" "$TMP/files"; else cat "$TMP/files"; fi |
        grep -v -E "$allowed" > "$TMP/scan" || true
    [ -s "$TMP/scan" ] || return 0
    # shellcheck disable=SC2046
    awk -v name="$name" -v skip="$skip" -v pattern="$pattern" '
        FNR == 1 { hit = 0; patch = (FILENAME ~ /\.(patch|diff)$/) }
        hit { next }
        {
            line = $0
            if (patch) sub(/^[+-]/, "", line)
            if (line ~ /^[ \t]*(\/\*|\*|\/\/)/) next
            if (skip != "" && line ~ skip) next
            if (line ~ pattern) { print name " " FILENAME; hit = 1 }
        }' $(cat "$TMP/scan")
}

W='[A-Za-z0-9_]'                 # a letter of a C name
B="(^|[^A-Za-z0-9_])"            # before a whole name
E="([^A-Za-z0-9_]|\$)"           # after a whole name
PIPE='^library/pipe/'
GPU='^library/(opengpu|ogfx)/'
RTG='^(library/[^/]+|compat/.*)$'
NONE='^$'

{
    rule host-header "" "$GPU|$RTG" "" '#[ \t]*include[ \t]*[<"][^">]*(acvirgl[.]h|virglrenderer[.]h|acrtg[.]h)[">]'
    rule virgl-cmd "" "$PIPE" "" '#[ \t]*include[ \t]*[<"][^">]*virgl_cmd[.]h[">]'
    rule present-export '[.]sfd$' '^library/gfopenrtg_lib[.]sfd$' 'OGPU_RetainedPresent' "${W}*Present${W}*[ \t]*[(]"
    rule acv-present "" "$PIPE|$RTG" "" "${B}ACV_PRESENT[A-Z_]*${E}"
    rule stream3d "" "$PIPE|$GPU" 'OGPU_Query|OGPUR_Can|defined[ \t]*[(]' \
        "${B}(OGPU_OP_(TRIANGLES|RENDER3D|TEXENV|TEXTURE|DEPTH|CLEAR3D)${E}|ogpu_(triangles|triangles3d|render3d|texenv|texture|depth|clear3d)[ \t]*[(])"
    rule state-cache "" "$PIPE" "" "${B}(vg_cache|[A-Za-z0-9]+_(vs|fs)_text|VG_RING|vg_ring)${E}"
    rule host-caps "" "$PIPE|$GPU|$RTG" "" "${B}ACV_(HELLO|CAPSET_INFO|CAPS)${E}"
    rule board-open "" "$GPU|$RTG" "" "${B}FindConfigDev${E}"
    rule mesa-present '^library/modules/gl/gla/os3/gla_present_os3[.]c$' "$NONE" "" "${B}(WritePixelArray|ogpu_pixels|LockBitMapTags)${E}"
} | sort -u > "$TMP/found"

if [ "$LIST" = 1 ]; then cat "$TMP/found"; exit 0; fi

# The debt list: "rule path", with "#" comments and blank lines.
debt_lines() { sed -e 's/#.*//' -e 's/[ \t]*$//' -e '/^$/d' | awk '{ print $1 " " $2 }' | sort -u; }
if [ -f "$DEBT" ]; then debt_lines < "$DEBT" > "$TMP/debt"; else : > "$TMP/debt"; fi

comm -23 "$TMP/found" "$TMP/debt" > "$TMP/new"
comm -13 "$TMP/found" "$TMP/debt" > "$TMP/stale"
: > "$TMP/added"
if [ -n "$BASE" ] && git cat-file -e "$BASE:tests/ownership.debt" 2>/dev/null; then
    git show "$BASE:tests/ownership.debt" | debt_lines > "$TMP/base"
    comm -23 "$TMP/debt" "$TMP/base" > "$TMP/added"
fi

bad=0
if [ -s "$TMP/new" ]; then
    bad=1
    echo "ownership_check: these break a single-owner rule and aren't on the debt list:"
    sed 's/^/  /' "$TMP/new"
    echo "  The owner of each function is in docs/design/OpenGPU-Shared-Pipe.md, section 6. Put the code with its"
    echo "  owner; the debt list ($DEBT) is for what was already there, and only shrinks."
fi
if [ -s "$TMP/stale" ]; then
    bad=1
    echo "ownership_check: these debt lines no longer break their rule. Delete them from $DEBT:"
    sed 's/^/  /' "$TMP/stale"
fi
if [ -s "$TMP/added" ]; then
    bad=1
    echo "ownership_check: the debt list gained these lines since $BASE. Debt is paid down, never added to:"
    sed 's/^/  /' "$TMP/added"
fi
if [ "$bad" = 0 ]; then
    echo "ownership_check: ok: $(wc -l < "$TMP/files" | tr -d ' ') files, $(wc -l < "$TMP/debt" | tr -d ' ') debt lines, none new, none stale"
fi
exit $bad
