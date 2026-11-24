#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# The GFOpenGPU kit's present headers (GF-66 M8): gfopenrtg/gfopenrtg.h with ORTG_Present (0.30, MEDIA 0.30,
# RETAINED 0.31), clib/gfopenrtg_protos.h and gfopengpu/pipe_vg.h stand on their own; the inline calls' and the
# prototypes' LVOs are the SFD's; the stats' names are ORTG_STAT_COUNT; tools/make_sdk.py ships them; and
# sdk/AutoDocs-GFOpenGPU.md documents every ORTG_SURF_, ORTG_PRESENT_ and present stat. Compiles with -m32 and links nothing.
#   tests/sdk/test_sdk_present.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
fail=0
ok=0
check() { if [ "$1" = 0 ]; then ok=$((ok + 1)); else echo "FAIL: $2"; fail=$((fail + 1)); fi; }
H="$ROOT/include/gfopenrtg/gfopenrtg.h"
SFD="$ROOT/library/gfopenrtg_lib.sfd"
DOC="$ROOT/sdk/AutoDocs-GFOpenGPU.md"
INC="-I$HERE/include -I$ROOT/include"
CC32="cc -m32 -std=gnu99 -ffreestanding -Wall -Wextra -Werror -c"   # compile only: no 32-bit C library needed

# 1. each header compiles alone, as a kit user includes it
for h in gfopenrtg/gfopenrtg.h clib/gfopenrtg_protos.h gfopengpu/pipe_vg.h; do
    printf '#include <%s>\nint sdk_present_unit;\n' "$h" > "$OUT/sdk_alone.c"
    if $CC32 -DVG_HOST $INC -o "$OUT/sdk_alone.o" "$OUT/sdk_alone.c" 2>"$OUT/sdk_alone.err"
    then r=0; else r=1; cat "$OUT/sdk_alone.err"; fi
    check $r "$h does not compile alone"
done

# 2. the struct, the kinds, the flags, the stats and the revisions, as gfopenrtg.library 0.31 reads them
cat > "$OUT/test_sdk_present.c" <<'C'
#include <stddef.h>
#include <gfopenrtg/gfopenrtg.h>
static const char *const names[] = ORTG_STAT_NAMES;
#define T(c) _Static_assert(c, #c)
T(sizeof names / sizeof names[0] == ORTG_STAT_COUNT);
T(ORTG_STAT_COUNT == 25);
T(ORTG_STAT_PRESENT_READBACK == 22 && ORTG_STAT_PRESENT_MEMORY == 23 && ORTG_STAT_PRESENT_MEDIA == 24);
T(ORTG_STAT_VIRGL == 20);
T(ORTG_SURF_VIRGL == 1 && ORTG_SURF_MEMORY == 2 && ORTG_SURF_MEDIA == 3 && ORTG_SURF_RETAINED == 4);
T(ORTG_PRESENT_FLIP_Y == 1 && ORTG_PRESENT_VSYNC == 2 && ORTG_PRESENT_PREPARE == 4 && ORTG_PRESENT_ALPHA == 8);
T(ORTG_PRESENT_REVISION == 30 && ORTG_RETAINED_REVISION == 31);
/* the layout gfopenrtg.library reads, 32-bit as on the Amiga */
T(offsetof(struct ORTG_PresentInfo, size) == 0 && offsetof(struct ORTG_PresentInfo, kind) == 4);
T(offsetof(struct ORTG_PresentInfo, handle) == 12 && offsetof(struct ORTG_PresentInfo, memory) == 16);
T(offsetof(struct ORTG_PresentInfo, width) == 28 && offsetof(struct ORTG_PresentInfo, rp) == 32);
T(offsetof(struct ORTG_PresentInfo, flags) == 52 && offsetof(struct ORTG_PresentInfo, token) == 64);
T(sizeof (struct ORTG_PresentInfo) == 68);
const char *sdk_present_name(int i);
const char *sdk_present_name(int i) { return names[i]; }
C
if $CC32 $INC -o "$OUT/test_sdk_present.o" "$OUT/test_sdk_present.c"; then r=0; else r=1; fi
check $r "gfopenrtg.h's present numbers or layout"

# 3. every SFD call: the inline macro and the prototype at the SFD's LVO
if python3 - "$SFD" "$ROOT/include/inline/gfopenrtg.h" "$ROOT/include/clib/gfopenrtg_protos.h" <<'PY'
import re, sys
sfd, inl, clib = (open(f).read() for f in sys.argv[1:])
bias = int(re.search(r"^==bias (\d+)", sfd, re.M).group(1))
calls = re.findall(r"^\S.*?\b(ORTG_\w+)\(.*\)\(.*\)$", sfd, re.M)
bad = 0
for i, name in enumerate(calls):
    lvo = bias + 6 * i
    m = re.search(r"#define %s\(.*\\\n\s*LP\d+(?:NR)?\((0x[0-9a-f]+)," % name, inl)
    if not m or int(m.group(1), 16) != lvo:
        print("FAIL: inline %s not at -%d" % (name, lvo)); bad += 1
    if not re.search(r"\b%s\(.*\);(\s*/\* -%d \*/)?" % (name, lvo), clib) or \
       not re.search(r"(/\* -%d \*/\n[^\n]*\b%s\(|\b%s\([^\n]*/\* -%d \*/)" % (lvo, name, name, lvo), clib):
        print("FAIL: clib %s not at -%d" % (name, lvo)); bad += 1
if "ORTG_Present" not in calls or calls.index("ORTG_Present") * 6 + bias != 0x96 or calls[-1] != "ORTG_PresentDone":
    print("FAIL: ORTG_Present not at 0x96 or ORTG_PresentDone not last"); bad += 1
if not re.search(r"gfopenrtg_lib\.sfd 0\.31 ", sfd):
    print("FAIL: the SFD's id is not 0.31"); bad += 1
print("sdk present: %d calls" % len(calls))
sys.exit(1 if bad else 0)
PY
then r=0; else r=1; fi
check $r "the LVOs of the inline calls or the prototypes"

# 4. the kit ships them, and not the pipe's command header
if python3 - "$ROOT" <<'PY'
import re, sys, pathlib
root = pathlib.Path(sys.argv[1])
src = (root / "tools" / "make_sdk.py").read_text()
own = eval(re.search(r"^OWN_INCLUDE = (\(.*\))$", src, re.M).group(1))
bad = 0
for h in ("gfopenrtg/gfopenrtg.h", "clib/gfopenrtg_protos.h", "proto/gfopenrtg.h", "inline/gfopenrtg.h", "gfopengpu/pipe_vg.h"):
    if h.split("/")[0] not in own or not (root / "include" / h).is_file():
        print("FAIL: the kit does not ship", h); bad += 1
if (root / "include" / "gfopengpu" / "virgl_cmd.h").exists():
    print("FAIL: virgl_cmd.h is under include/, so the kit ships it"); bad += 1
if "gfopenrtg_lib.sfd" not in src:
    print("FAIL: the kit does not ship gfopenrtg_lib.sfd"); bad += 1
sys.exit(1 if bad else 0)
PY
then r=0; else r=1; fi
check $r "make_sdk.py's headers"

# 5. AutoDocs documents each kind, flag, present stat and call, and pipe_vg.h
code=$(sed -e 's|/\*.*\*/||g' "$H")
for n in $(printf '%s\n' "$code" | grep -oE '\bORTG_(SURF|PRESENT)_[A-Z_]+\b|\bORTG_STAT_PRESENT_[A-Z]+\b' | sort -u) \
    ORTG_Present ORTG_PresentDone ORTG_STAT_COUNT clib/gfopenrtg_protos.h gfopengpu/pipe_vg.h; do
    if grep -q -- "$n" "$DOC"; then r=0; else r=1; fi
    check $r "AutoDocs does not name $n"
done

echo "test_sdk_present: $ok passed, $fail failed"
[ "$fail" = 0 ]
