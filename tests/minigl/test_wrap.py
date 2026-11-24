#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""The checks of tests/minigl/test_wrap.sh.

  test_wrap.py ROOT OUT     OUT holds gen_wrap.py's files and its --list

  - the table has the spec's 205 entries in the spec's order (the spec read here, not through gen_api.py);
  - each forwarded entry's thunk gives its own slot and the size of its GL arguments (a double is two longs,
    the context is left out), and the two GLboolean answers go through mglw_call_bool;
  - every entry made by hand has one function in the wrapper's files, under the library's name for it;
  - every GL.module call the wrapper's files make has a slot, and no two slots bind one export;
  - the fields mglw_call.s reads are where mglw.h says they are.
"""
import os
import re
import sys

root, out = sys.argv[1], sys.argv[2]
M = os.path.join(root, "library", "minigl")
fails = checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL: " + what)


def read(*p):
    with open(os.path.join(*p)) as f:
        return f.read()


# The spec's section 8, read here: "| n | offset | `name` | `type (args)` |" rows.
spec = []
in8 = False
for line in read(root, "docs", "design", "MiniGL-Compatible-Spec.md").splitlines():
    if line.startswith("## "):
        in8 = line.startswith("## 8")
    m = re.match(r"\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*`(\w+)`\s*\|\s*`([^`]+)`\s*\|", line) if in8 else None
    if m:
        spec.append((int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)))
check(len(spec) == 205, "the spec's section 8 has 205 entries (read %d)" % len(spec))

rows = [l.split(None, 3) for l in read(out, "list.txt").splitlines()]
check(len(rows) == 205, "gen_wrap lists 205 entries (%d)" % len(rows))
for (n, off, name, proto), r in zip(spec, rows):
    check(int(r[0]) == n and r[1] == name, "entry %d is %s (gen_wrap: %s %s)" % (n, name, r[0], r[1]))
kinds = {r[1]: r[2] for r in rows}
check(sorted(set(kinds.values())) == ["bool", "forward", "hand"], "three kinds of entry")
for n in ("glIsTexture", "glIsList"):
    check(kinds.get(n) == "bool", "%s answers a GLboolean: through mglw_call_bool" % n)
for name, k in kinds.items():
    if not name.startswith("gl") or name.startswith("glu"):
        check(k == "hand", "%s is the library's own: by hand" % name)

# The table: 205 entries in order, after the five header fields.
table = read(out, "mglw_table.c")
body = table[table.index("const MGLDispatchTable mglw_table"):table.index("};")]
ents = re.findall(r"^\s+\(void \*\)(\w+), /\* (\d+) (\w+) \*/$", body, re.M)
check(len(ents) == 205, "mglw_table has 205 entries (%d)" % len(ents))
for i, (fn, n, name) in enumerate(ents):
    check(int(n) == i + 1 and name == spec[i][2], "table entry %d is %s" % (i + 1, spec[i][2]))
    if kinds[name] == "hand":
        check(not fn.startswith("mglw_e"), "%s is called by name" % name)
    else:
        check(fn == "mglw_e%d" % (i + 1), "%s goes through its thunk" % name)

# Slots and thunks.
slots = dict((m.group(1), int(m.group(2))) for m in re.finditer(r"#define MGLW_SLOT_(\w+) (\d+)", read(out, "mglw_slots.h")))
nslots = int(re.search(r"#define MGLW_SLOTS (\d+)", read(out, "mglw_slots.h")).group(1))
check(sorted(slots.values()) == list(range(nslots)), "slots are 0..%d, each once" % (nslots - 1))
names = re.findall(r'^\s+"(\w+)",$', table[table.index("mglw_gl_names"):], re.M)
check(len(names) == nslots and len(set(names)) == nslots, "one export name per slot, none twice")
for i, nm in enumerate(names):
    check(slots.get(nm) == i, "slot %d is %s in both files" % (i, nm))
thunks = re.findall(r"_mglw_e(\d+):\t\| (\w+)\n\tmove\.l\t#(\d+),%d0\n\tmoveq\t#(\d+),%d1\n\tjmp\t(\w+)\n", read(out, "mglw_thunks.s"))
check(len(thunks) == sum(1 for k in kinds.values() if k != "hand"), "a thunk for every forwarded entry")
rename = {"glActiveTextureARB": "glActiveTexture", "glClientActiveTextureARB": "glClientActiveTexture",
          "glMultiTexCoord2fARB": "glMultiTexCoord2f", "glMultiTexCoord2fvARB": "glMultiTexCoord2fv"}
seen = set()
for n, name, d0, d1, to in thunks:
    proto = spec[int(n) - 1][3]
    args = [a.strip() for a in proto[proto.index("(") + 1:proto.rindex(")")].split(",")]
    check(args and args[0].startswith("GLcontext"), "%s takes the context first" % name)
    want = sum(2 if re.match(r"(const\s+)?GL(double|clampd)\b(?!\s*\*)", a) else 1 for a in args[1:])
    check(int(d1) == want, "%s: %s longs of arguments (thunk says %s)" % (name, want, d1))
    check(int(d0) == 4 * slots.get(rename.get(name, name), -1), "%s: its own slot" % name)
    check(to == ("_mglw_call_bool" if kinds[name] == "bool" else "_mglw_call"), "%s: the right call routine" % name)
    check(d0 not in seen, "%s: no other entry's slot" % name)
    seen.add(d0)

# The wrapper's files: MiniGL's kept ones and wrap/'s.
kept = ["mgl_display.c", "mgl_choose.c", "mgl_glu.c", "mgl_glut.c", "mgl_math.c", "mgl_unpack.c"]
wrap = ["mglw_session.c", "mglw_seam.c", "mglw_gl.c"]
src = {f: read(M, f) for f in kept}
src.update({f: read(M, "wrap", f) for f in wrap})
build = read(M, "build.sh")
wline = re.search(r'^WRAP="([^"]+)"', build, re.M)
check(wline is not None, "build.sh names the wrapper's files")
if wline:
    built = set(wline.group(1).split())
    for f in kept + wrap:
        check(f[:-2] in built, "build.sh builds %s into the wrapper" % f)
    for f in ("mglw_table", "mglw_thunks", "mglw_call"):
        check(f in built, "build.sh builds %s into the wrapper" % f)


def internal(name):
    for pre, to in (("glut", "GLUT"), ("glu", "GLU"), ("gl", "GL"), ("mgl", "MGL")):
        if name.startswith(pre):
            return to + name[len(pre):]
    return name


for name, k in kinds.items():
    if k != "hand":
        continue
    fn = internal(name)
    pat = re.compile(r"^[A-Za-z_][^\n(]*[ *]%s\(.*$" % re.escape(fn), re.M)
    defs = [f for f, s in src.items() if any(not m.group(0).rstrip().endswith(");") for m in pat.finditer(s))]
    check(len(defs) == 1, "%s has one function %s in the wrapper's files (%s)" % (name, fn, ", ".join(defs) or "none"))
    check(re.search(r"\b%s\b" % re.escape(fn), table) is not None, "the table calls %s" % fn)

for f in wrap:
    for m in re.finditer(r"\b(?:GL|CALL0?|MGLW_F)\(\s*c\s*,\s*(gl\w+)", src[f]):
        check(m.group(1) in slots, "%s calls %s: it has a slot" % (f, m.group(1)))

# mglw_call.s reads a session's first six fields by offset: they are longs or pointers, in this order.
head = read(M, "wrap", "mglw.h")
sess = head[head.index("struct mglw_session {"):]
fields = re.findall(r"^\s+[\w ]+?\*?\*?(\w+);\s+/\*\s+(\d+):", sess, re.M)
check([f for f, o in fields] == ["a4", "fn", "sss", "task", "depth", "current"], "the session's first six fields")
check([int(o) for f, o in fields] == [0, 4, 8, 12, 16, 20], "at offsets 0 to 20")
asm = read(M, "wrap", "mglw_call.s")
check("_mglw_call:" in asm and "_mglw_call_bool:" in asm and "_mglw_invoke:" in asm and "_mglw_missing:" in asm,
      "mglw_call.s has the four routines")

print("test_wrap: %d checks, %d failed" % (checks, fails))
sys.exit(1 if fails else 0)
