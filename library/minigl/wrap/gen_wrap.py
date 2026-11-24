#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""minigl.library as a face of GL.module: the generated parts.

  gen_wrap.py OUT_DIR        write the files below
  gen_wrap.py --list         print each entry and what becomes of it (for the tests)

The table's order and types are the specification's (docs/design/MiniGL-Compatible-Spec.md, section 8, read by
gen_api.py's own reader): nothing here can move an entry. Each entry is one of:

  forward   a GL call GL.module has under the same name: a thunk (mglw_thunks.s) puts its slot x 4 in D0 and the
            size of its GL arguments in longs in D1 and goes to mglw_call (mglw_call.s), which leaves the context
            out and calls the module's function with A4 on the session's copy of Mesa's data.
  bool      the same, for a call that answers a GLboolean: MiniGL's is a long, the module's a byte in D0, so the
            answer is cut to 0 or 1 (mglw_call_bool).
  hand      a C function under the library's own name for it (GLColorMask, MGLCreateContext, GLUSphere): MiniGL's
            own calls (mgl*), GLU and GLUT, in the library's kept files (../mgl_display.c, ../mgl_choose.c,
            ../mgl_glu.c, ../mgl_glut.c), and in mglw_gl.c the GL calls whose arguments differ from OpenGL's (MiniGL's 4-byte GLboolean in arrays and
            arguments, its paletted textures, its two glRotatefEXT calls, glGetString).

Writes into OUT_DIR:
  mglw_thunks.s   the forward and bool entries
  mglw_table.c    the dispatch table (the hand entries by their C names) and each slot's GL.module export
  mglw_slots.h    MGLW_SLOT_<name> for the hand-written files, MGLW_SLOTS
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import gen_api  # noqa: E402  (the spec's reader)

TWO_LONGS = {"GLdouble", "GLclampd"}

# GL calls written by hand, and why.
HAND_GL = {
    "glGetBooleanv": "the answers are MiniGL's 4-byte GLboolean",
    "glAreTexturesResident": "the answers are MiniGL's 4-byte GLboolean",
    "glEdgeFlagv": "the flag read is MiniGL's 4-byte GLboolean",
    "glEdgeFlagPointer": "the array's elements are MiniGL's 4-byte GLboolean",
    "glEdgeFlag": "a GLboolean argument: any non-zero long is true",
    "glColorMask": "GLboolean arguments: any non-zero long is true",
    "glDepthMask": "a GLboolean argument: any non-zero long is true",
    "glColorTable": "paletted textures: GL.module has none, the palette is applied here",
    "glTexImage2D": "paletted textures (GL_COLOR_INDEX, GL_COLOR_INDEX8_EXT) are expanded here",
    "glTexSubImage2D": "paletted textures are expanded here",
    "glRotatefEXT": "MiniGL's own call: a rotation about one axis",
    "glRotatefEXTs": "MiniGL's own call: a rotation from a sine and a cosine",
    "glGetString": "MiniGL's own strings (GL_VERSION is 1.1, spec 9.1)",
    "glLockArrays": "glLockArraysEXT in the module",
    "glUnlockArrays": "glUnlockArraysEXT in the module",
    "glEnable": "MiniGL's own capabilities (MGL_ tokens, GL_POLYGON_OFFSET, the shared palette) stay here",
    "glDisable": "MiniGL's own capabilities stay here",
    "glHint": "MiniGL's own hint targets are taken and dropped",
    "glColorPointer": "MGL_UBYTE_BGRA is the module's GL_BGRA size",
    "glPixelStorei": "the unpack settings are kept here: textures are unpacked here (paletted and MGL_ types)",
    "glGetError": "an error from an mgl* call is reported first",
    "glIsEnabled": "MiniGL's own capabilities are answered here; the answer is a long",
}
BOOL_GL = {"glIsTexture", "glIsList"}
# The module's name where it isn't the entry's.
MODULE_NAME = {
    "glActiveTextureARB": "glActiveTexture",
    "glClientActiveTextureARB": "glClientActiveTexture",
    "glMultiTexCoord2fARB": "glMultiTexCoord2f",
    "glMultiTexCoord2fvARB": "glMultiTexCoord2fv",
}
# GL.module calls the hand-written files make that no forward entry binds.
EXTRA = ["glLockArraysEXT", "glUnlockArraysEXT", "glMultMatrixf", "glRotatef", "glGetBooleanv",
         "glAreTexturesResident", "glEdgeFlag", "glEdgeFlagPointer", "glColorMask", "glDepthMask", "glTexImage2D",
         "glTexSubImage2D", "glGetString", "glDisable", "glEnable", "glIsEnabled", "glHint", "glColorPointer",
         "glPixelStorei", "glGetError", "glPolygonOffset", "glPushAttrib", "glPopAttrib", "glWindowPos2i",
         "glDrawPixels"]


def kind_of(e):
    n = e["name"]
    if not n.startswith("gl") or n.startswith("glu"):
        return "hand"
    if n in HAND_GL:
        return "hand"
    if n in BOOL_GL:
        return "bool"
    return "forward"


def longs(e):
    """The GL arguments in longs, the context left out."""
    n = 0
    for t in e["args"]:
        if t == "GLcontext":
            continue
        n += 2 if t.replace("const", "").strip() in TWO_LONGS else 1
    return n


def plan(entries):
    """(entry, kind, slot or None, module name or None) for every entry, and the slots' module names."""
    rows, names = [], []
    for e in entries:
        k = kind_of(e)
        if k == "hand":
            rows.append((e, k, None, None))
            continue
        if not gen_api.takes_context(e):
            sys.exit("gen_wrap: %s is forwarded but takes no context" % e["name"])
        m = MODULE_NAME.get(e["name"], e["name"])
        rows.append((e, k, len(names), m))
        names.append(m)
    for m in EXTRA:
        if m not in names:
            names.append(m)
    return rows, names


def main():
    entries = gen_api.read_spec()
    rows, names = plan(entries)
    if len(sys.argv) > 1 and sys.argv[1] == "--list":
        for e, k, slot, m in rows:
            print("%d %s %s %s" % (e["n"], e["name"], k, m if m else HAND_GL.get(e["name"], "-")))
        return
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "mglw_thunks.s"), "w") as s:
        s.write("| Generated by library/minigl/wrap/gen_wrap.py: do not edit.\n"
                "| One entry per forwarded GL call: D0 its slot x 4, D1 its GL arguments in longs.\n\n\t.text\n")
        for e, k, slot, m in rows:
            if k == "hand":
                continue
            s.write("\t.globl\t_mglw_e%d\n_mglw_e%d:\t| %s\n\tmove.l\t#%d,%%d0\n\tmoveq\t#%d,%%d1\n\tjmp\t%s\n"
                    % (e["n"], e["n"], e["name"], 4 * slot, longs(e), "_mglw_call_bool" if k == "bool" else "_mglw_call"))
    with open(os.path.join(out, "mglw_slots.h"), "w") as h:
        h.write("/* Generated by library/minigl/wrap/gen_wrap.py: do not edit. GL.module's calls, by slot. */\n")
        for i, m in enumerate(names):
            h.write("#define MGLW_SLOT_%s %d\n" % (m, i))
        h.write("#define MGLW_SLOTS %d\n" % len(names))
    with open(os.path.join(out, "mglw_table.c"), "w") as c:
        c.write("/* Generated by library/minigl/wrap/gen_wrap.py: do not edit. */\n#include \"mglw.h\"\n\n")
        for e, k, slot, m in rows:
            if k != "hand":
                c.write("extern void mglw_e%d(void);\n" % e["n"])
        c.write("\n/* The hand-written entries that take a context do nothing for a NULL one (spec 4.3). */\n")
        for e, k, slot, m in rows:
            if k != "hand" or not gen_api.takes_context(e):
                continue
            an = ["a%d" % i for i in range(len(e["args"]))]
            call = "%s(%s)" % (gen_api.internal_name(e["name"]), ", ".join(an))
            body = ("if (a0) %s;" % call) if gen_api.is_void(e["ret"]) else ("return a0 ? %s : 0;" % call)
            c.write("static %s T_%s(%s) { %s }\n" % (e["ret"], e["name"], gen_api.params(e["args"], an), body))
        c.write("\nconst MGLDispatchTable mglw_table = {\n"
                "    MGL_DISPATCH_ABI,\n    MGL_DISPATCH_SIZE,\n    MGL_BACKEND_AMICRADLE,\n    0,\n    &mgl_current,\n")
        for e, k, slot, m in rows:
            if k != "hand":
                fn = "(void *)mglw_e%d" % e["n"]
            elif gen_api.takes_context(e):
                fn = "(void *)T_" + e["name"]
            else:
                fn = "(void *)" + gen_api.internal_name(e["name"])
            c.write("    %s, /* %d %s */\n" % (fn, e["n"], e["name"]))
        c.write("};\n\n/* The GL.module export each slot is bound to. */\nconst char *const mglw_gl_names[MGLW_SLOTS] = {\n")
        for m in names:
            c.write("    \"%s\",\n" % m)
        c.write("};\n")


if __name__ == "__main__":
    main()
