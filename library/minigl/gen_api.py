#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# minigl.library's interface files, made from the clean-room specification
# (docs/design/MiniGL-Compatible-Spec.md): its section 8 table is the one
# source of the dispatch table's order and types. Nothing else is read for
# the table. See PROVENANCE.md.
#
#   gen_api.py                  write the files below from the spec
#   gen_api.py --tokens DIR     also remake include/mgl/gltokens.h from the
#                               Khronos gl.h and glext.h in DIR (Mesa's copies)
#   gen_api.py --check          exit 1 when a written file differs from what
#                               the spec makes now (for the tests)
#   gen_api.py --expect FILE    write the spec's table as a C table of
#                               (entry, offset, name) for the ABI test
#
# Writes:
#   include/libraries/minigl_dispatch.h   the table's layout
#   include/mgl/minigl_calls.h            the calls a program makes, through the table
#   library/minigl/mgl_entries.h          the library's functions behind the entries
#   library/minigl/mgl_table.c            the table the library hands out
#   library/minigl/mgl_static.c           libmgl.a: every call as a function
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
SPEC = os.path.join(ROOT, "docs", "design", "MiniGL-Compatible-Spec.md")

HEAD = ("/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).\n"
        " * SPDX-License-Identifier: MIT\n"
        " *\n"
        " * MADE BY library/minigl/gen_api.py FROM docs/design/MiniGL-Compatible-Spec.md\n"
        " * (section 8). Do not edit: change the generator or the spec.\n")

# ---- the spec ---------------------------------------------------------------------------------

ROW = re.compile(r"^\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*`(\w+)`\s*\|\s*`(.+)`\s*\|\s*$")


def split_top(s):
    """s split at the commas outside parentheses."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def parse_sig(sig):
    """'void * (int, int)' -> ('void *', ['int', 'int'])."""
    i = sig.index("(")
    ret = sig[:i].strip()
    depth = 0
    for j in range(i, len(sig)):
        if sig[j] == "(":
            depth += 1
        elif sig[j] == ")":
            depth -= 1
            if depth == 0:
                break
    args = split_top(sig[i + 1:j])
    if args == ["void"]:
        args = []
    return ret, args


def read_spec(path=SPEC):
    entries = []
    in8 = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("## 8."):
                in8 = True
                continue
            if in8 and line.startswith("## ") and not line.startswith("## 8."):
                break
            if in8 and line.startswith("### "):
                break
            m = ROW.match(line) if in8 else None
            if m:
                n, off, name, sig = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)
                ret, args = parse_sig(sig)
                entries.append({"n": n, "off": off, "name": name, "ret": ret, "args": args, "sig": sig})
    if len(entries) != 205:
        sys.exit("gen_api: the spec's section 8 has %d entries, not 205" % len(entries))
    for k, e in enumerate(entries):
        if e["n"] != k + 1 or e["off"] != 20 + 4 * k:
            sys.exit("gen_api: entry %d (%s) is out of order or at offset %d" % (e["n"], e["name"], e["off"]))
    return entries


# ---- names and declarations -------------------------------------------------------------------

def internal_name(name):
    """The library's function for an entry: GL, GLU, GLUT or MGL, then the rest."""
    for pre, rep in (("glut", "GLUT"), ("glu", "GLU"), ("gl", "GL"), ("mgl", "MGL")):
        if name.startswith(pre):
            return rep + name[len(pre):]
    raise SystemExit("gen_api: no prefix in " + name)


def decl(t, name):
    """A parameter of type t called name."""
    if "(*)" in t:
        return t.replace("(*)", "(*%s)" % name, 1)
    if t.endswith("*"):
        return t + name
    return t + " " + name


def params(types, names):
    return ", ".join(decl(t, n) for t, n in zip(types, names)) or "void"


def is_void(ret):
    return ret == "void"


def takes_context(e):
    return bool(e["args"]) and e["args"][0] == "GLcontext"


CREATE = {91, 92, 152, 153}     # the client stores a non-NULL result as the current context
DELETE = 93                     # the client passes the current context and clears it first
GUARDED = {81, 82}              # called by the client only with a current context


def write(path, text, check):
    full = os.path.join(ROOT, path)
    if check:
        old = open(full, encoding="utf-8").read() if os.path.exists(full) else None
        if old != text:
            print("gen_api: %s is not what the spec makes" % path)
            return False
        return True
    with open(full, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote " + path)
    return True


# ---- include/libraries/minigl_dispatch.h --------------------------------------------------------

def gen_dispatch(entries):
    o = [HEAD + " *\n"
         " * The table minigl.library hands out (its function at -30): five longwords,\n"
         " * then one function pointer for each entry, in the order of the spec's\n"
         " * section 8. All fields are 32 bits, big-endian on the Amiga, no padding.\n"
         " * Entries are only ever appended; the ABI version stays 5. */\n"
         "#ifndef LIBRARIES_MINIGL_DISPATCH_H\n#define LIBRARIES_MINIGL_DISPATCH_H\n\n"
         "#include <mgl/mgltypes.h>\n\n"
         "struct Window;\nstruct BitMap;\n\n"
         "#define MGL_DISPATCH_ABI        5       /* equal, or a program refuses the table */\n"
         "#define MGL_DISPATCH_ENTRIES    %d\n" % len(entries) +
         "#define MGL_DISPATCH_SIZE       %d     /* header and entries, in bytes */\n" % (20 + 4 * len(entries)) +
         "#define MGL_DISPATCH_SIZE_290   836     /* what a program built with 29.0 headers asks for at least */\n"
         "#define MGL_BACKEND_AMICRADLE   (1UL << 16)   /* our flag; the published bits 0-2 are left clear */\n\n"
         "typedef struct MGLDispatchTable {\n"
         "    unsigned long abi_version;          /* 0: MGL_DISPATCH_ABI */\n"
         "    unsigned long table_size;           /* 4: bytes, at least what the program knows */\n"
         "    unsigned long backend_flags;        /* 8 */\n"
         "    unsigned long reserved;             /* 12: 0 */\n"
         "    GLcontext *current_context;         /* 16: read by every call, written by the client */\n"]
    for e in entries:
        o.append("    %s (*%s)(%s);%s/* %d: %d */\n" % (e["ret"], e["name"], ", ".join(e["args"]) or "void",
                                                     " ", e["n"], e["off"]))
    o.append("} MGLDispatchTable;\n\n#endif\n")
    return "".join(o)


# ---- include/mgl/minigl_calls.h ---------------------------------------------------------------

# Calls with no entry of their own (spec 8.1): (return, name, params, body using the
# table's calls by their client names).
EXTRA = [
    ("void", "glEnable", "GLenum cap", "mglSetState(cap, GL_TRUE);"),
    ("void", "glDisable", "GLenum cap", "mglSetState(cap, GL_FALSE);"),
    ("void", "glColor3f", "GLfloat r, GLfloat g, GLfloat b", "glColor4f(r, g, b, 1.0f);"),
    ("void", "glColor3ub", "GLubyte r, GLubyte g, GLubyte b", "glColor4ub(r, g, b, 255);"),
    ("void", "glVertex2f", "GLfloat x, GLfloat y", "glVertex4f(x, y, 0.0f, 1.0f);"),
    ("void", "glVertex3f", "GLfloat x, GLfloat y, GLfloat z", "glVertex4f(x, y, z, 1.0f);"),
    ("void", "glTexEnvf", "GLenum target, GLenum pname, GLfloat param", "glTexEnvi(target, pname, (GLint)param);"),
    ("void", "glTexEnviv", "GLenum target, GLenum pname, const GLint *params",
     "glTexEnvi(target, pname, params[0]);"),
    ("void", "glTexParameterf", "GLenum target, GLenum pname, GLfloat param",
     "glTexParameteri(target, pname, (GLint)param);"),
    ("void", "glFogi", "GLenum pname, GLint param", "glFogf(pname, (GLfloat)param);"),
    ("void", "glColorTableEXT", "GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data",
     "glColorTable(target, internalformat, width, format, type, data);"),
]


def client_call(e):
    """(return, name, params, body) of the call a program makes for entry e."""
    n, ret, args = e["n"], e["ret"], e["args"]
    t = "MiniGLDispatch->" + e["name"]
    if n == DELETE:
        body = ("GLcontext *p = MiniGLDispatch->current_context; GLcontext old = *p;\n"
                "    if (old) { *p = 0; %s(old); }" % t)
        return ret, e["name"], "void", body
    if n in GUARDED:
        names = ["a%d" % i for i in range(len(args))]
        ps = ", ".join("GLdouble " + a for a in names)
        call = "%s(%s)" % (t, ", ".join("(GLfloat)" + a for a in names))
        return ret, e["name"], ps, "if (*MiniGLDispatch->current_context) %s;" % call
    if takes_context(e):
        types = args[1:]
        names = ["a%d" % i for i in range(len(types))]
        call = "%s(%s)" % (t, ", ".join(["*MiniGLDispatch->current_context"] + names))
    else:
        types = args
        names = ["a%d" % i for i in range(len(types))]
        call = "%s(%s)" % (t, ", ".join(names))
    ps = params(types, names)
    if n in CREATE:
        body = ("%s r = %s;\n    if (r) *MiniGLDispatch->current_context = (GLcontext)r;\n    return r;" % (ret, call))
    elif is_void(ret):
        body = call + ";"
    else:
        body = "return %s;" % call
    return ret, e["name"], ps, body


def gen_calls(entries):
    o = [HEAD + " *\n"
         " * The calls a program makes, each through the table (MiniGLDispatch) with\n"
         " * the current context (spec 4.3, 8.1). Included by <mgl/gl.h> three ways:\n"
         " *   MGL_CALLS_INLINE   static inline functions (<proto/minigl.h>)\n"
         " *   MGL_CALLS_DEFINE   functions (library/minigl/mgl_static.c, libmgl.a)\n"
         " *   neither            prototypes of libmgl.a's functions */\n"
         "#ifndef MGL_MINIGL_CALLS_H\n#define MGL_MINIGL_CALLS_H\n\n"
         "#include <libraries/minigl_dispatch.h>\n\n"
         "extern const MGLDispatchTable *MiniGLDispatch;\n\n"
         "#if defined(MGL_CALLS_INLINE)\n"
         "#define MGL_CALL static __inline__\n#define MGL_BODY(...) { __VA_ARGS__ }\n"
         "#elif defined(MGL_CALLS_DEFINE)\n"
         "#define MGL_CALL\n#define MGL_BODY(...) { __VA_ARGS__ }\n"
         "#else\n"
         "#define MGL_CALL extern\n#define MGL_BODY(...) ;\n"
         "#endif\n\n"
         "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"]

    def emit(ret, name, ps, body, note):
        o.append("/* %s */\nMGL_CALL %s %s(%s)\nMGL_BODY(\n    %s\n)\n" % (note, ret, name, ps, body))

    for e in entries:
        ret, name, ps, body = client_call(e)
        emit(ret, name, ps, body, "entry %d" % e["n"])
    o.append("\n/* ---- calls with no entry of their own (spec 8.1) ---- */\n\n")
    for ret, name, ps, body in EXTRA:
        emit(ret, name, ps, body, "no entry")
    o.append("\n#ifdef __cplusplus\n}\n#endif\n\n#undef MGL_CALL\n#undef MGL_BODY\n\n#endif\n")
    return "".join(o)


# ---- library/minigl/mgl_entries.h, mgl_table.c -------------------------------------------------

def gen_entries(entries):
    o = [HEAD + " *\n"
         " * The library's functions behind the table's entries, with the spec's\n"
         " * types (a definition that differs does not compile). */\n"
         "#ifndef MGL_ENTRIES_H\n#define MGL_ENTRIES_H\n\n"
         "struct Window;\nstruct BitMap;\n\n"]
    for e in entries:
        names = ["a%d" % i for i in range(len(e["args"]))]
        o.append("%s %s(%s);%s/* %d */\n" % (e["ret"], internal_name(e["name"]), params(e["args"], names), " ", e["n"]))
    o.append("\n#endif\n")
    return "".join(o)


def gen_table(entries):
    o = [HEAD + " *\n"
         " * The table the library hands out. An entry that takes a context goes\n"
         " * through a small function that does nothing for a NULL context (spec\n"
         " * 4.3: a program may call before it has one); the rest go straight to\n"
         " * the library's function. build.sh compiles this file in both halves\n"
         " * (FPU and soft), as mgl_table_fpu and mgl_table_soft; mgl_lib.c picks\n"
         " * one. */\n"
         "#include \"mgl_internal.h\"\n\n"]
    for e in entries:
        if not takes_context(e):
            continue
        names = ["a%d" % i for i in range(len(e["args"]))]
        fn = internal_name(e["name"])
        call = "%s(%s)" % (fn, ", ".join(names))
        if is_void(e["ret"]):
            body = "if (a0) %s;" % call
        else:
            body = "return a0 ? %s : 0;" % call
        o.append("static %s T_%s(%s) { %s }\n" % (e["ret"], e["name"], params(e["args"], names), body))
    o.append("\nconst MGLDispatchTable mgl_table = {\n"
             "    MGL_DISPATCH_ABI,\n    MGL_DISPATCH_SIZE,\n    MGL_BACKEND_AMICRADLE,\n    0,\n    &mgl_current,\n")
    for e in entries:
        fn = ("T_" + e["name"]) if takes_context(e) else internal_name(e["name"])
        o.append("    %s,%s/* %d %s */\n" % (fn, " ", e["n"], e["name"]))
    o.append("};\n\n"
             "/* What the C compiler laid out is what the spec says. */\n"
             "typedef char mgl_table_size_is_840[sizeof(MGLDispatchTable) == MGL_DISPATCH_SIZE ? 1 : -1];\n")
    return "".join(o)


def gen_static(entries):
    return (HEAD + " *\n"
            " * libmgl.a: every call a program makes, as a function, through the same\n"
            " * table: for programs written to call functions (MGLInit, MGLTerm). */\n"
            "#define MGL_CALLS_DEFINE\n"
            "#include <libraries/minigl.h>\n"
            "#include <mgl/gl.h>\n\n"
            "GLboolean MGLInit(void) { return MiniGLOpen() ? GL_TRUE : GL_FALSE; }\n"
            "void MGLTerm(void) { MiniGLClose(); }\n")


def gen_expect(entries):
    """The table's fields by name, with the offsets the compiler gives them: the
    ABI test reads the spec on its own and compares."""
    o = [HEAD + " *\n * The dispatch table's fields, for tests/test_minigl_abi.c. */\n",
         "#include <stddef.h>\n#include <libraries/minigl_dispatch.h>\n",
         "static const struct { const char *name; unsigned long off; } table_fields[] = {\n"]
    for e in entries:
        o.append("    { \"%s\", offsetof(MGLDispatchTable, %s) },\n" % (e["name"], e["name"]))
    o.append("};\n#define TABLE_FIELDS %d\n" % len(entries))
    return "".join(o)


# ---- include/mgl/gltokens.h from Khronos's headers ---------------------------------------------

DEFINE = re.compile(r"^#define\s+(GL_\w+)\s+(0x[0-9A-Fa-f]+|\d+)\s*$")

# Extensions whose tokens a program for this library may use (GL 1.1 plus spec 9.1).
GLEXT_BLOCKS = ("GL_EXT_paletted_texture", "GL_EXT_shared_texture_palette", "GL_EXT_compiled_vertex_array",
                "GL_EXT_bgra", "GL_EXT_blend_minmax", "GL_EXT_blend_subtract", "GL_EXT_blend_func_separate",
                "GL_EXT_blend_color", "GL_SGIS_texture_edge_clamp", "GL_EXT_packed_pixels", "GL_ARB_multitexture",
                "GL_EXT_texture_env_add", "GL_EXT_color_table", "GL_VERSION_1_4")


def gen_tokens(kdir):
    gl = os.path.join(kdir, "gl.h")
    ext = os.path.join(kdir, "glext.h")
    names, values = [], {}
    # gl.h: OpenGL 1.0 to 1.3, ARB_imaging and ARB_multitexture (all before Mesa's own extensions)
    with open(gl, encoding="utf-8") as f:
        for line in f:
            if "GL_MESA_" in line or "GL_ATI_" in line or "GL_OES_" in line:
                break
            m = DEFINE.match(line.strip())
            if m and m.group(1) not in values:
                values[m.group(1)] = m.group(2)
                names.append(m.group(1))
    block = None
    with open(ext, encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if s.startswith("#ifndef "):
                block = s.split()[1]
                continue
            if s.startswith("#endif /* ") and block and block in s:
                block = None
                continue
            m = DEFINE.match(s)
            if m and block in GLEXT_BLOCKS and m.group(1) not in values:
                values[m.group(1)] = m.group(2)
                names.append(m.group(1))
    names = [n for n in names if not n.startswith("GL_VERSION_") and not re.match(r"GL_(ARB|EXT|SGIS)_[a-z]", n)]
    o = ["/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).\n"
         " * SPDX-License-Identifier: MIT\n *\n"
         " * MADE BY library/minigl/gen_api.py --tokens FROM THE KHRONOS HEADERS: Mesa's\n"
         " * include/GL/gl.h (Copyright (C) 1999-2006 Brian Paul, (C) 2009 VMware;\n"
         " * MIT licence) and include/GL/glext.h (Copyright 2013-2020 The Khronos\n"
         " * Group Inc.; MIT). Their names and values only: OpenGL 1.0 to 1.3,\n"
         " * ARB_imaging, ARB_multitexture and the extensions of spec section 9.1.\n"
         " * MiniGL's own tokens are in <mgl/mgltokens.h>. */\n"
         "#ifndef MGL_GLTOKENS_H\n#define MGL_GLTOKENS_H\n\n"]
    for n in names:
        o.append("#define %-44s %s\n" % (n, values[n]))
    o.append("\n#endif\n")
    return "".join(o)


def main():
    argv = sys.argv[1:]
    entries = read_spec()
    if "--expect" in argv:
        out = argv[argv.index("--expect") + 1]
        with open(out, "w", encoding="utf-8") as f:
            f.write(gen_expect(entries))
        return 0
    check = "--check" in argv
    ok = True
    files = [
        ("include/libraries/minigl_dispatch.h", gen_dispatch(entries)),
        ("include/mgl/minigl_calls.h", gen_calls(entries)),
        ("library/minigl/mgl_entries.h", gen_entries(entries)),
        ("library/minigl/mgl_table.c", gen_table(entries)),
        ("library/minigl/mgl_static.c", gen_static(entries)),
    ]
    if "--tokens" in argv:
        files.append(("include/mgl/gltokens.h", gen_tokens(argv[argv.index("--tokens") + 1])))
    for path, text in files:
        ok = write(path, text, check) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
