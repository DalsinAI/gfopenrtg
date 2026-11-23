/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OGPU_Query's capability answers: discovery once per boot, then answers from the cache (ogpu_caps.h; the ops'
 * meanings, and every PARTIAL's, in docs/design/OGPU-Query.md).
 *
 * The blocks are the ones the pipe's screen sends (library/pipe/pipe_screen.c) without the context: ACV_HELLO and
 * ACV_CAPSET_INFO for set 2 in one block, ACV_CAPSET_INFO for set 1 when the host has no set 2, then ACV_CAPS.
 * The host already takes a HELLO from every library that opens (MiniGL and Warp3D each send one today), so one
 * more from gfopengpu.library changes nothing for it.
 *
 * The caps are virgl's struct virgl_caps_v2, word for word: the format masks at 1 (sampler), 17 (render),
 * 33 (depth and stencil), each 16 words with a format's bit at word + format / 32, bit format % 32; the bool set
 * at 65; prim_mask at 72; max_vertex_attribs at 89; capability_bits at 98; max_texture_2d_size at 121. */
#include "ogpu_caps.h"
#include "../../include/gfopengpu/caps.h"
#include "../../include/gfopengpu/stream3d.h"
#include "../../include/gfopengpu/virgl.h"

/* A request block's words are in the Amiga's order (virgl.h); a PC test (VG_HOST) keeps them so on a little-endian
 * machine. Written here rather than taken from virgl_cmd.h, which is the shared pipe's own (DESIGN.md section 13's
 * virgl-cmd rule): gfopengpu.library sends only these three request blocks, never a command stream. */
#ifdef VG_HOST
#define BW_GET(p)    __builtin_bswap32(*(const ULONG *)(p))
#define BW_PUT(p, v) (*(ULONG *)(p) = __builtin_bswap32((ULONG)(v)))
#else
#define BW_GET(p)    (*(const ULONG *)(p))
#define BW_PUT(p, v) (*(ULONG *)(p) = (ULONG)(v))
#endif
static void blk_cmd(ULONG *p, ULONG cmd, ULONG n, const ULONG *args)
{
    ULONG i;
    BW_PUT(p, ACV_HDR(cmd, n + 2));
    BW_PUT(p + 1, 0);
    for (i = 0; i < n; i++) BW_PUT(p + 2 + i, args[i]);
}
static LONG blk_result(const ULONG *b, ULONG at) { return (LONG)BW_GET(b + at + 1); }
static ULONG blk_word(const ULONG *b, ULONG at) { return BW_GET(b + at); }

/* virgl's word numbers in caps set 2 */
#define CW_SAMPLER      1
#define CW_RENDER       17
#define CW_DEPTH        33
#define CW_MAX_ATTRIBS  89
#define CW_MAX_TEX_2D   121

/* virgl's formats (VIRGL_FORMAT_*, as virglrenderer's virgl_hw.h) */
#define VF_B8G8R8A8     1
#define VF_B8G8R8X8     2
#define VF_A8R8G8B8     3
#define VF_X8R8G8B8     4
#define VF_B5G5R5A1     5
#define VF_B4G4R4A4     6
#define VF_B5G6R5       7
#define VF_L8           9
#define VF_A8           10
#define VF_L8A8         12
#define VF_Z16          16
#define VF_Z32          17
#define VF_Z24S8        19
#define VF_Z24X8        21
#define VF_S8           23
#define VF_R8G8B8A8     67

/* the 68k core's sizes: o3_texture takes up to 4096 a side, but a full mipmap chain (OGPU_TEX_LEVELS 12) stops
 * at 2048, and a front end can't know beforehand which it will make */
#define CPU_MAX_TEX     2048
#define CPU_TEX_UNITS   OGPU_TEX_UNITS

void ogpu_caps_init(struct ogpu_caps *c, int cpu3d)
{
    ULONG i;
    c->looked = c->virgl = c->set = 0;
    c->cpu3d = cpu3d ? 1 : 0;
    c->host_flags = c->words = c->discoveries = 0;
    for (i = 0; i < OGPU_CAPS_WORDS; i++) c->caps[i] = 0;
}

void ogpu_caps_reset(struct ogpu_caps *c)
{
    c->looked = c->virgl = c->set = 0;
    c->host_flags = c->words = 0;
}

void ogpu_caps_discover(struct ogpu_caps *c, int have_virgl, ogpu_caps_run_fn run, void *ctx, ULONG caps_at)
{
    ULONG a[4], *b = c->blk, ri = 3, n, set = 2;
    if (c->looked) return;
    c->looked = 1;
    if (!have_virgl || !run) return;
    c->discoveries++;

    /* 1. HELLO and caps set 2's size */
    a[0] = 0;
    blk_cmd(b, ACV_HELLO, 1, a);
    a[0] = 2; a[1] = 0; a[2] = 0;
    blk_cmd(b + 3, ACV_CAPSET_INFO, 3, a);
    if (run(ctx, b, 8 * 4)) return;
    if (blk_result(b, 0) != ACV_PROTOCOL) return;
    c->virgl = 1;
    c->host_flags = blk_word(b, 2);

    /* 2. no set 2: set 1 */
    if (!blk_word(b, ri + 3)) {
        set = 1;
        a[0] = 1; a[1] = 0; a[2] = 0;
        blk_cmd(b, ACV_CAPSET_INFO, 3, a);
        if (run(ctx, b, 5 * 4)) return;
        ri = 0;
        if (!blk_word(b, ri + 3)) return;               /* no caps at all: virgl, but nothing to answer from */
    }

    /* 3. the caps, into the struct */
    n = blk_word(b, ri + 4);
    if (n > sizeof c->caps) n = sizeof c->caps;
    a[0] = set; a[1] = blk_word(b, ri + 3); a[2] = caps_at; a[3] = n;
    blk_cmd(b, ACV_CAPS, 4, a);
    if (run(ctx, b, 6 * 4) || blk_result(b, 0) <= 0) return;
    n = (ULONG)blk_result(b, 0) / 4;
    c->words = n > OGPU_CAPS_WORDS ? OGPU_CAPS_WORDS : n;
    c->set = (UBYTE)set;
}

/* ---- answers -------------------------------------------------------------------------------------------------- */

static ULONG cw(const struct ogpu_caps *c, ULONG at)
{
    return at < c->words ? BW_GET(&c->caps[at]) : 0;
}

/* a virgl format's bit in the mask at `at` */
static int has_fmt(const struct ogpu_caps *c, ULONG at, ULONG vf)
{
    return vf && vf < 16 * 32 && ((cw(c, at + vf / 32) >> (vf % 32)) & 1);
}

/* Each OGPU format: the virgl format that holds it as it is, and the one it is converted to when that one's
 * missing (a PARTIAL: the pipe converts on upload). The pipe's byte swap is not a conversion: every block goes in
 * the Amiga's order and the host turns it round. */
struct fmt_map { UBYTE fmt, native, convert; };
static const struct fmt_map colour[] = {
    { OGPU_FMT_ARGB32,    VF_B8G8R8A8, 0 },
    { OGPU_FMT_BGRA32,    VF_A8R8G8B8, VF_B8G8R8A8 },
    { OGPU_FMT_XRGB32,    VF_B8G8R8X8, VF_B8G8R8A8 },
    { OGPU_FMT_RGBA32,    VF_R8G8B8A8, VF_B8G8R8A8 },
    { OGPU_FMT_ABGR32,    0,           VF_B8G8R8A8 },
    { OGPU_FMT_RGB565,    VF_B5G6R5,   VF_B8G8R8A8 },
    { OGPU_FMT_RGB565PC,  VF_B5G6R5,   VF_B8G8R8A8 },
    { OGPU_FMT_RGB555,    VF_B5G5R5A1, VF_B8G8R8A8 },
    { OGPU_FMT_RGB555PC,  VF_B5G5R5A1, VF_B8G8R8A8 },
    { OGPU_FMT_RGBA5551,  VF_B5G5R5A1, VF_B8G8R8A8 },
    { OGPU_FMT_RGBA4444,  VF_B4G4R4A4, VF_B8G8R8A8 },
    { OGPU_FMT_A8,        VF_A8,       VF_B8G8R8A8 },
    { OGPU_FMT_L8,        VF_L8,       VF_B8G8R8A8 },
    { OGPU_FMT_LA88,      VF_L8A8,     VF_B8G8R8A8 },
    { OGPU_FMT_CLUT8,     0,           VF_B8G8R8A8 },  /* through its palette */
    { OGPU_FMT_INDEX8,    0,           VF_B8G8R8A8 },
    { 0, 0, 0 }
};
static const struct fmt_map depth[] = {
    { OGPU_FMT_Z16,       VF_Z16,      VF_Z24X8 },
    { OGPU_FMT_Z24,       VF_Z24X8,    VF_Z24S8 },
    { OGPU_FMT_Z32,       VF_Z32,      VF_Z24X8 },     /* 8 bits fewer: a PARTIAL */
    { OGPU_FMT_Z24S8,     VF_Z24S8,    0 },
    { OGPU_FMT_S8,        VF_S8,       VF_Z24S8 },     /* inside a Z24S8 */
    { 0, 0, 0 }
};

static int from_mask(const struct ogpu_caps *c, ULONG at, const struct fmt_map *m, ULONG format)
{
    for (; m->fmt; m++) {
        if (m->fmt != format) continue;
        if (has_fmt(c, at, m->native)) return OGPU_FULL;
        if (has_fmt(c, at, m->convert)) return OGPU_PARTIAL;
        return OGPU_NONE;
    }
    return OGPU_NONE;
}

/* the virgl back end, with caps */
static int virgl_answer(const struct ogpu_caps *c, ULONG op, ULONG format)
{
    int gles = (c->host_flags & ACV_HOST_GLES) != 0;
    switch (op) {
    case OGPU_OP_REAL_GPU:      return (c->host_flags & ACV_HOST_GPU) ? OGPU_FULL : OGPU_NONE;
    case OGPU_OP_LLVM_PIPE:     return (c->host_flags & ACV_HOST_GPU) ? OGPU_NONE : OGPU_FULL;
    case OGPU_OP_VIRGL_PRESENT: return (c->host_flags & ACV_HOST_PRESENT) ? OGPU_FULL : OGPU_NONE;
    case OGPU_OP_VIRGL_GLES:    return gles ? OGPU_FULL : OGPU_NONE;
    case OGPU_OP_CPU_CORE:      return OGPU_NONE;
    }
    if (!c->set) return OGPU_NONE;                       /* virgl, but no caps came back: nothing is promised */
    switch (op) {
    case OGPU_OP_TEXTURE_2D:    return from_mask(c, CW_SAMPLER, colour, format);
    case OGPU_OP_MIPMAP:        return from_mask(c, CW_SAMPLER, colour, format);
    case OGPU_OP_RENDER_TARGET: return from_mask(c, CW_RENDER, colour, format);
    case OGPU_OP_DEPTH_STENCIL: return from_mask(c, CW_DEPTH, depth, format);
    case OGPU_OP_BUFFER:        return OGPU_FULL;
    /* fixed function: the pipe's own TGSI (virgl has none of its own) */
    case OGPU_OP_FF_TEXTURE_ENV:
    case OGPU_OP_FF_FOG:
    case OGPU_OP_FF_MULTITEXTURE:
    case OGPU_OP_FF_LIGHTING:
    case OGPU_OP_FF_TEXGEN:
    case OGPU_OP_FF_ALPHA_TEST:
    case OGPU_OP_FF_SPECULAR:   return OGPU_FULL;
    case OGPU_OP_FF_CHROMA:     return OGPU_PARTIAL;     /* a discard against the texel: one colour key, not a range */
    case OGPU_OP_BLEND:
    case OGPU_OP_DEPTH_TEST:
    case OGPU_OP_STENCIL_TEST:
    case OGPU_OP_SCISSOR:
    case OGPU_OP_TGSI:          return OGPU_FULL;
    case OGPU_OP_LOGIC_OP:      return gles ? OGPU_NONE : OGPU_FULL;   /* GLES has no glLogicOp */
    }
    return OGPU_NONE;                                    /* presentation: OpenRTG's, from M8 */
}

/* ogpu3d_supports(OGPU_OP_TRIANGLES, format), kept here so this file needs no 3D core to build */
static int ogpu3d_answer_target(ULONG format)
{
    return format == OGPU_FMT_ARGB32 || format == OGPU_FMT_RGB565 ? OGPU_FULL
         : format == OGPU_FMT_CLUT8 || format == OGPU_FMT_XRGB32 || format == OGPU_FMT_BGRA32
           || format == OGPU_FMT_RGBA32 || format == OGPU_FMT_ABGR32 ? OGPU_PARTIAL : OGPU_NONE;
}

/* the 68k core (ogpu_3d.c): its pixel path is stipple, texture, chroma test, environment, specular, fog, alpha
 * test, stencil, Z, blend or logic op (that file's header) */
static int cpu_answer(const struct ogpu_caps *c, ULONG op, ULONG format)
{
    if (op == OGPU_OP_CPU_CORE) return c->cpu3d ? OGPU_FULL : OGPU_NONE;
    if (!c->cpu3d) return OGPU_NONE;
    switch (op) {
    case OGPU_OP_TEXTURE_2D:
    case OGPU_OP_MIPMAP:
        /* o3_texture takes these as they are; the rest are converted when the texture is made */
        if (format == OGPU_FMT_ARGB32 || format == OGPU_FMT_RGB565 || format == OGPU_FMT_A8
            || format == OGPU_FMT_INDEX8 || format == OGPU_FMT_CLUT8) return OGPU_FULL;
        return (format >= 1 && format <= 12) || (format >= OGPU_FMT_CAP_FIRST && format <= OGPU_FMT_LA88)
             ? OGPU_PARTIAL : OGPU_NONE;
    case OGPU_OP_RENDER_TARGET:
        return ogpu3d_answer_target(format);
    case OGPU_OP_DEPTH_STENCIL:
        return format == OGPU_FMT_Z16 || format == OGPU_FMT_Z32 || format == OGPU_FMT_S8 ? OGPU_FULL
             : format == OGPU_FMT_Z24 || format == OGPU_FMT_Z24S8 ? OGPU_PARTIAL   /* Z32, and S8 beside it */
             : OGPU_NONE;
    case OGPU_OP_BUFFER:
    case OGPU_OP_FF_TEXTURE_ENV:
    case OGPU_OP_FF_MULTITEXTURE:                       /* OGPU_TEX_UNITS units */
    case OGPU_OP_FF_FOG:
    case OGPU_OP_FF_ALPHA_TEST:
    case OGPU_OP_FF_CHROMA:
    case OGPU_OP_FF_SPECULAR:
    case OGPU_OP_BLEND:
    case OGPU_OP_LOGIC_OP:
    case OGPU_OP_DEPTH_TEST:
    case OGPU_OP_STENCIL_TEST:
    case OGPU_OP_SCISSOR:       return OGPU_FULL;
    }
    return OGPU_NONE;                                    /* no lighting, texgen or shaders: the front ends do those */
}

ULONG ogpu_caps_answer(const struct ogpu_caps *c, ULONG drv, ULONG op, ULONG format)
{
    int on_gpu = c->virgl && (c->host_flags & ACV_HOST_GPU);
    int a;
    switch (drv) {
    case OGPU_DRV_BEST:
        if (c->virgl) {
            a = virgl_answer(c, op, format);
            if (a > OGPU_NONE) return (ULONG)a | (1UL << 8) | (on_gpu ? OGPU_ONGPU_BIT : 0);
        }
        return (ULONG)cpu_answer(c, op, format) | (OGPU_BACKEND_CPU << 8);
    case OGPU_DRV_VIRGL:
    case OGPU_DRV_LLVMPIPE:
        if (!c->virgl || (drv == OGPU_DRV_VIRGL) != (on_gpu != 0)) return OGPU_NONE | (1UL << 8);
        a = virgl_answer(c, op, format);
        return (ULONG)a | (1UL << 8) | (a > OGPU_NONE && on_gpu ? OGPU_ONGPU_BIT : 0);
    case OGPU_DRV_CPU:
        return (ULONG)cpu_answer(c, op, format) | (OGPU_BACKEND_CPU << 8);
    }
    return OGPU_NONE | (1UL << 8);                       /* OGPU_DRV_ACRTG and unknown back ends */
}

ULONG ogpu_caps_limit(const struct ogpu_caps *c, ULONG op)
{
    ULONG v;
    if (c->virgl && c->set) {
        switch (op) {
        case OGPU_OP_MAX_TEXTURE_SIZE:
        case OGPU_OP_MAX_VIEWPORT_DIMS:
            return cw(c, CW_MAX_TEX_2D);                 /* 0 from a set 1 host: not known */
        case OGPU_OP_MAX_TEXTURE_UNITS:
            return 8;                                    /* the pipe's fixed-function shaders take at most 8 */
        case OGPU_OP_MAX_VERTEX_ATTRIBS:
            v = cw(c, CW_MAX_ATTRIBS);
            return v ? v : 16;                           /* set 1 doesn't say; GL 3's least */
        }
        return 0;
    }
    if (!c->cpu3d) return 0;
    switch (op) {
    case OGPU_OP_MAX_TEXTURE_SIZE:  return CPU_MAX_TEX;
    case OGPU_OP_MAX_TEXTURE_UNITS: return CPU_TEX_UNITS;
    }
    return 0;
}
