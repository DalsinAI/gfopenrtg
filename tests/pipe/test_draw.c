/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Pipe version 7 (stage M7 of docs/design/OpenGPU-Shared-Pipe-Plan.md): the pipe writes the commands Warp3D.library
 * (w3d_vg.c) and minigl.library's own renderer (mgl_vg_enc.c, mgl_vg_pipe.c, mgl_vg_tex.c) wrote by hand, and this
 * test holds it to their words, one for one. The expected words below are written out as those files wrote them, not
 * with virgl_cmd.h's writers (which the pipe itself uses), so a writer that changed would show here too:
 *   surfaces (both front ends' CREATE_OBJECT SURFACE, 6 words) and the framebuffer;
 *   sampler views (CREATE_OBJECT SAMPLER_VIEW, 7 words, with and without VIRGL_CAP_TEXTURE_VIEW) and their renames;
 *   the units' SET_SAMPLER_VIEWS and BIND_SAMPLER_STATES: Warp3D's one unit, MiniGL's two, each only when changed;
 *   SET_VERTEX_BUFFERS, SET_INDEX_BUFFER (an indexed draw's offset, its start 0) and DRAW_VBO;
 *   SET_STENCIL_REF, SET_BLEND_COLOR, SET_POLYGON_STIPPLE, only when changed;
 *   CLEAR, and Warp3D's 16-bit area BLIT (copy);
 *   a resource let go of: its objects destroyed, its units empty; begin_frame and state_reset: all sent again.
 *
 * Built 32-bit with VG_HOST, under AddressSanitizer and UBSan, as the pipe's other tests.
 *   tests/pipe/test_draw.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/gfopengpu/pipe.h"
#include "../../library/pipe/virgl_cmd.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the pretend host: it keeps the last block's stream ---------------------------------------------------------- */

#define SMAX 4096
static ULONG S[SMAX]; static ULONG ns;          /* the last block's stream words, all its SUBMITs in order */
static ULONG caps_bits = 2;                     /* what the caps say (word 98) */

int ogpu_pipe_xport_ready(void) { return 1; }
void *ogpu_pipe_xport_alloc(ULONG bytes) { return calloc(1, bytes ? bytes : 4); }
void ogpu_pipe_xport_free(void *p) { free(p); }

LONG ogpu_pipe_xport_run(ULONG *blk, ULONG bytes)
{
    ULONG at = 0, words = bytes / 4, i;
    ns = 0;
    while (at < words) {
        ULONG hdr = VG_GETW(blk + at), cmd = ACV_HDR_CMD(hdr), n = ACV_HDR_WORDS(hdr);
        if (!n) break;
        VG_PUTW(blk + at + 1, 0);
        switch (cmd) {
        case ACV_HELLO: VG_PUTW(blk + at + 1, ACV_PROTOCOL); VG_PUTW(blk + at + 2, 0); break;
        case ACV_CAPSET_INFO: VG_PUTW(blk + at + 3, 2); VG_PUTW(blk + at + 4, 1024); break;
        case ACV_CAPS: {
            ULONG *c = (ULONG *)(unsigned long)VG_GETW(blk + at + 4);
            memset(c, 0, VG_GETW(blk + at + 5));
            VG_PUTW(c + 98, caps_bits);
            VG_PUTW(blk + at + 1, VG_GETW(blk + at + 5));
            break;
        }
        case ACV_SUBMIT: {
            const ULONG *s = (const ULONG *)(unsigned long)VG_GETW(blk + at + 3);
            ULONG nw = VG_GETW(blk + at + 4);
            for (i = 0; i < nw && ns < SMAX; i++) S[ns++] = VG_LE(s[i]);
            break;
        }
        default: break;
        }
        at += n;
    }
    return 0;
}

/* ---- helpers ----------------------------------------------------------------------------------------------------- */

static const struct OGPUPipeOps *ops;
static ULONG obk[16], ocaps[256];
static char oname[] = "test";

static OGPUPipeCtx *open_ctx(OGPUPipeScreen **sp)
{
    struct OGPUPipeOpen o;
    struct OGPUPipeInfo info;
    memset(&o, 0, sizeof o);
    o.size = sizeof o; o.emit_wanted = OGPU_EMIT_VIRGL; o.ctx_id = 0x1000; o.name = oname; o.name_len = 4;
    o.blk = obk; o.caps = ocaps; o.caps_bytes = sizeof ocaps;
    *sp = ops->screen_open(&o, &info);
    return *sp ? ops->ctx_create(*sp, 0) : 0;
}

static OGPUPipeRes *res(OGPUPipeCtx *x, ULONG fmt, ULONG bind, ULONG w, ULONG h, ULONG levels)
{
    struct OGPUPipeResDesc d;
    d.target = VG_TARGET_2D; d.format = fmt; d.bind = bind; d.width = w; d.height = h; d.levels = levels; d.flags = 0;
    return ops->res_create(x, &d);
}

/* The expected words, built as the front ends wrote them. */
static ULONG E[SMAX]; static ULONG ne;
static void e(ULONG w) { if (ne < SMAX) E[ne++] = w; }
static void e0(void) { ne = 0; }

/* The stream so far, sent, against E. */
static void same(OGPUPipeCtx *x, const char *what)
{
    ULONG i;
    ns = 0;
    ops->flush(x, 0);
    for (i = 0; i < ns && i < ne && S[i] == E[i]; i++) ;
    CHECK(ns == ne && i == ne, "%s: %lu words, %lu expected, first difference at %lu (%08lx, %08lx expected)", what,
          (unsigned long)ns, (unsigned long)ne, (unsigned long)i, (unsigned long)(i < ns ? S[i] : 0),
          (unsigned long)(i < ne ? E[i] : 0));
}

static ULONG fb(float f) { union { float f; ULONG u; } v; v.f = f; return v.u; }

/* w3d_vg.c's colour_for and depth_for, mgl_vg_pipe.c's open: a surface */
static void e_surface(ULONG h, ULONG res, ULONG fmt)
{
    e(VG_CCMD_CREATE_OBJECT | VG_OBJ_SURFACE << 8 | 5 << 16); e(h); e(res); e(fmt); e(0); e(0);
}
/* w3d_vg.c's set_framebuffer, mgl_vg_enc.c's vg_set_framebuffer */
static void e_fb(ULONG z, ULONG c) { e(VG_CCMD_SET_FRAMEBUFFER_STATE | 3 << 16); e(1); e(z); e(c); }
/* w3d_vg.c's tex_view, mgl_vg_tex.c's new_version */
static void e_view(ULONG h, ULONG res, ULONG levels, int caps)
{
    e(VG_CCMD_CREATE_OBJECT | VG_OBJ_SAMPLER_VIEW << 8 | 6 << 16); e(h); e(res);
    e(VG_FMT_B8G8R8A8_UNORM | (caps ? (ULONG)VG_TARGET_2D << 24 : 0));
    e(0); e((levels - 1) << 8); e(0 | 1 << 3 | 2 << 6 | 3 << 9);
}
static void e_destroy(ULONG obj, ULONG h) { e(VG_CCMD_DESTROY_OBJECT | obj << 8 | 1 << 16); e(h); }
static void e_views(ULONG n, const ULONG *h) { ULONG i; e(VG_CCMD_SET_SAMPLER_VIEWS | (n + 2) << 16); e(1); e(0); for (i = 0; i < n; i++) e(h[i]); }
static void e_samps(ULONG n, const ULONG *h) { ULONG i; e(VG_CCMD_BIND_SAMPLER_STATES | (n + 2) << 16); e(1); e(0); for (i = 0; i < n; i++) e(h[i]); }
static void e_vb(ULONG stride, ULONG off, ULONG h) { e(VG_CCMD_SET_VERTEX_BUFFERS | 3 << 16); e(stride); e(off); e(h); }
static void e_ib(ULONG h, ULONG off) { e(VG_CCMD_SET_INDEX_BUFFER | 3 << 16); e(h); e(2); e(off); }
static void e_draw(ULONG prim, ULONG start, ULONG count, int indexed, LONG bias)
{
    e(VG_CCMD_DRAW_VBO | 12 << 16); e(start); e(count); e(prim); e(indexed ? 1 : 0); e(1); e(indexed ? (ULONG)bias : 0);
    e(0); e(0); e(0); e(0); e(0xFFFFFFFFUL); e(0);
}

int main(void)
{
    OGPUPipeScreen *s;
    OGPUPipeCtx *x;
    OGPUPipeRes *colour, *depth, *t, *stage;
    OGPUPipeCSO *samp;
    struct OGPUSamplerKey sk;
    struct OGPUPipeArenas a;
    struct OGPUDrawBuf d;
    struct OGPUDrawInfo di;
    struct OGPUCopy cp;
    ULONG hv[2], hs[2], th, rows[32], i, sh;
    float rgba[4];

    ops = ogpu_pipe_ops(7);
    CHECK(ops && ops->version >= 7, "the table, version 7");
    CHECK(ogpu_pipe_ops(6) == ops, "a version 6 caller gets the same table");
    x = open_ctx(&s);
    CHECK(x != 0, "a context");
    if (!x) return 1;
    ops->flush(x, 0);
    ops->arenas(x, &a);

    /* ---- the framebuffer: the surfaces made at its first bind, then the framebuffer at every call ---- */
    colour = res(x, VG_FMT_B8G8R8A8_UNORM, VG_BIND_RENDER_TARGET | VG_BIND_SAMPLER_VIEW, 320, 240, 1);
    depth = res(x, VG_FMT_Z24X8_UNORM, VG_BIND_DEPTH_STENCIL, 320, 240, 1);
    ops->bind_framebuffer(x, colour, depth);
    e0(); e_surface(1, ops->res_handle(colour), VG_FMT_B8G8R8A8_UNORM); e_surface(2, ops->res_handle(depth), VG_FMT_Z24X8_UNORM);
    e_fb(2, 1);
    same(x, "the first framebuffer: two surfaces, then the framebuffer");
    ops->bind_framebuffer(x, colour, depth);
    e0(); e_fb(2, 1);
    same(x, "the framebuffer again: its four words only");
    ops->bind_framebuffer(x, colour, 0);
    e0(); e_fb(0, 1);
    same(x, "no depth buffer (Warp3D's)");

    /* ---- Warp3D's draw: one unit, its own vertex buffer, the index arena ---- */
    t = res(x, VG_FMT_B8G8R8A8_UNORM, VG_BIND_SAMPLER_VIEW, 64, 64, 7);
    th = ops->res_handle(t);
    sk.s0 = 0x1234; sk.levels = 7;
    samp = ops->sampler_create(x, &sk);
    sh = ops->cso_handle(samp);
    ops->flush(x, 0);                                   /* the sampler state's create, out of the way */
    stage = res(x, VG_FMT_B5G6R5_UNORM, VG_BIND_SAMPLER_VIEW, 320, 240, 1);
    ops->bind_texture(x, 0, t, samp);
    memset(&d, 0, sizeof d);
    d.size = sizeof d; d.prim = VG_PRIM_TRIANGLES; d.first = 4; d.count = 9; d.indexed = 16; d.stride = 24;
    d.vbuf = stage; d.vb_offset = 4096;                 /* (any buffer: Warp3D's ring of batch memory) */
    ops->draw_buf(x, &d);
    ops->arenas(x, &a);
    e0(); e_view(4, th, 7, 1); hv[0] = 4; e_views(1, hv); hs[0] = sh; e_samps(1, hs);
    e_vb(24, 4096, ops->res_handle(stage)); e_ib(a.ibuf, a.ib_base + 8); e_draw(VG_PRIM_TRIANGLES, 0, 9, 1, 0);
    same(x, "Warp3D's first draw: the view made, one unit's views and samplers, the buffers, the draw");
    ops->arenas(x, &a);
    d.first = 13;
    ops->draw_buf(x, &d);
    e0(); e_ib(a.ibuf, a.ib_base + 26); e_draw(VG_PRIM_TRIANGLES, 0, 9, 1, 0);
    same(x, "the next run: its index offset, the draw (nothing else changed)");
    ops->arenas(x, &a);
    ops->draw_buf(x, &d);
    e0(); e_draw(VG_PRIM_TRIANGLES, 0, 9, 1, 0);
    same(x, "the same again: the draw alone");

    /* ---- MiniGL's draw: two units, the arenas ---- */
    ops->bind_texture(x, 1, 0, 0);
    ops->arenas(x, &a);
    memset(&d, 0, sizeof d);
    d.size = sizeof d; d.prim = VG_PRIM_TRIANGLE_STRIP; d.first = 5; d.count = 6; d.stride = 36;
    ops->draw_buf(x, &d);
    e0(); hv[0] = 4; hv[1] = 0; e_views(2, hv); hs[0] = sh; hs[1] = 0; e_samps(2, hs);
    e_vb(36, a.vb_off, a.vbuf); e_draw(VG_PRIM_TRIANGLE_STRIP, 5, 6, 0, 0);
    same(x, "MiniGL's: both units (the second empty), the arena's vertex buffer, a draw from vertex 5");
    ops->arenas(x, &a);
    memset(&di, 0, sizeof di);
    di.prim = VG_PRIM_TRIANGLES; di.first = 10; di.count = 3; di.indexed = 16; di.bias = 7;
    ops->draw(x, &di);
    e0(); e_ib(a.ibuf, a.ib_base + 20); e_draw(VG_PRIM_TRIANGLES, 0, 3, 1, 7);
    same(x, "draw (version 1): the arenas, the last stride, indexed from index 10 with its bias");

    /* ---- a rename: the old view goes, a new one is made for the new version ---- */
    ops->res_used(x, t);
    CHECK(ops->res_write(x, t) == 1, "a write while the stream reads it renames it");
    ops->arenas(x, &a);
    di.indexed = 0; di.first = 0; di.bias = 0;
    ops->draw(x, &di);
    e0(); e_destroy(VG_OBJ_SAMPLER_VIEW, 4); e_view(5, ops->res_handle(t), 7, 1); hv[0] = 5; hv[1] = 0; e_views(2, hv);
    e_draw(VG_PRIM_TRIANGLES, 0, 3, 0, 0);
    same(x, "after a rename: the old view destroyed, the new one made and set; the samplers as they were");

    /* ---- the state Warp3D sent by hand: only when it changed ---- */
    ops->set_stencil_ref(x, 3, 3);
    ops->set_stencil_ref(x, 3, 3);
    rgba[0] = 1.0f; rgba[1] = 0.5f; rgba[2] = 0.25f; rgba[3] = 0.0f;
    ops->set_blend_colour(x, rgba);
    ops->set_blend_colour(x, rgba);
    for (i = 0; i < 32; i++) rows[i] = i & 1 ? 0xAAAAAAAAUL : 0x55555555UL;
    ops->set_stipple(x, rows);
    ops->set_stipple(x, rows);
    e0(); e(VG_CCMD_SET_STENCIL_REF | 1 << 16); e(3 | 3 << 8);
    e(VG_CCMD_SET_BLEND_COLOR | 4 << 16); e(fb(1.0f)); e(fb(0.5f)); e(fb(0.25f)); e(fb(0.0f));
    e(VG_CCMD_SET_POLYGON_STIPPLE | 32 << 16); for (i = 0; i < 32; i++) e(rows[i]);
    same(x, "stencil reference, blend colour and stipple, each once");
    rows[31] = 0;
    ops->set_stipple(x, rows);
    e0(); e(VG_CCMD_SET_POLYGON_STIPPLE | 32 << 16); for (i = 0; i < 32; i++) e(rows[i]);
    same(x, "a stipple with one row changed: sent again");

    /* ---- CLEAR, as w3d_vg.c's clear and depth_for and mgl_vg_enc.c's vg_clear wrote it ---- */
    ops->clear(x, VG_CLEAR_COLOR0 | VG_CLEAR_DEPTH, rgba, 1.0, 0);
    ops->clear(x, VG_CLEAR_DEPTH | VG_CLEAR_STENCIL, 0, 0.5, 0x80);
    e0(); e(VG_CCMD_CLEAR | 8 << 16); e(VG_CLEAR_COLOR0 | VG_CLEAR_DEPTH); e(fb(1.0f)); e(fb(0.5f)); e(fb(0.25f)); e(fb(0.0f));
    e(0); e(0x3FF00000UL); e(0);                        /* 1.0 as a little-endian qword, low word first */
    e(VG_CCMD_CLEAR | 8 << 16); e(VG_CLEAR_DEPTH | VG_CLEAR_STENCIL); e(0); e(0); e(0); e(0);
    e(0); e(0x3FE00000UL); e(0x80);
    same(x, "two clears");

    /* ---- copy: Warp3D's 16-bit area into the colour buffer (w3d_vg.c's load) ---- */
    memset(&cp, 0, sizeof cp);
    cp.size = sizeof cp; cp.dst = colour; cp.src = stage; cp.mask = OGPU_COPY_RGBA;
    cp.dst_box.w = cp.src_box.w = 320; cp.dst_box.h = cp.src_box.h = 240; cp.dst_box.d = cp.src_box.d = 1;
    ops->copy(x, &cp);
    e0(); e(VG_CCMD_BLIT | 21 << 16); e(0xF); e(0); e(0);
    e(ops->res_handle(colour)); e(0); e(VG_FMT_B8G8R8A8_UNORM); e(0); e(0); e(0); e(320); e(240); e(1);
    e(ops->res_handle(stage)); e(0); e(VG_FMT_B5G6R5_UNORM); e(0); e(0); e(0); e(320); e(240); e(1);
    same(x, "the BLIT, as Warp3D wrote it");

    /* ---- a texture let go of: its view destroyed; its unit left as the host has it until bound again ---- */
    ops->res_release(x, t);
    ops->arenas(x, &a);
    ops->draw(x, &di);
    e0(); e_destroy(VG_OBJ_SAMPLER_VIEW, 5); e_draw(VG_PRIM_TRIANGLES, 0, 3, 0, 0);
    same(x, "after its last release, unbound: the view destroyed, the units as they were (w3d_vg.c's untextured draw)");
    ops->bind_texture(x, 0, 0, 0);
    ops->bind_texture(x, 1, 0, 0);
    ops->draw(x, &di);
    e0(); hv[0] = hv[1] = 0; e_views(2, hv); hs[0] = hs[1] = 0; e_samps(2, hs); e_draw(VG_PRIM_TRIANGLES, 0, 3, 0, 0);
    same(x, "both units bound empty (MiniGL's next draw): both sent, the old view being gone");

    /* ---- begin_frame: everything the frame members sent goes again ---- */
    ops->begin_frame(x);
    ops->set_stencil_ref(x, 3, 3);
    ops->arenas(x, &a);
    ops->draw(x, &di);
    e0(); e(VG_CCMD_SET_STENCIL_REF | 1 << 16); e(3 | 3 << 8); e_views(2, hv); e_samps(2, hs); e_vb(36, a.vb_off, a.vbuf);
    e_draw(VG_PRIM_TRIANGLES, 0, 3, 0, 0);
    same(x, "after begin_frame: the stencil reference, units and vertex buffer again");

    /* ---- the render targets let go of: their surfaces destroyed ---- */
    ops->res_release(x, depth);
    ops->res_release(x, colour);
    e0(); e_destroy(VG_OBJ_SURFACE, 2); e_destroy(VG_OBJ_SURFACE, 1);
    same(x, "the surfaces destroyed with their resources");

    /* ---- no VIRGL_CAP_TEXTURE_VIEW: the view's format alone ---- */
    ops->res_release(x, stage);
    ops->ctx_destroy(x);
    ops->screen_close(s);
    caps_bits = 0;
    x = open_ctx(&s);
    CHECK(x != 0, "a second context");
    if (!x) return 1;
    ops->flush(x, 0);
    t = res(x, VG_FMT_B8G8R8A8_UNORM, VG_BIND_SAMPLER_VIEW, 8, 8, 1);
    ops->bind_texture(x, 0, t, 0);
    ops->arenas(x, &a);
    memset(&d, 0, sizeof d);
    d.size = sizeof d; d.prim = VG_PRIM_POINTS; d.count = 1; d.stride = 16;
    ops->draw_buf(x, &d);
    e0(); e_view(1, ops->res_handle(t), 1, 0); hv[0] = 1; e_views(1, hv); hs[0] = 0; e_samps(1, hs);
    e_vb(16, a.vb_off, a.vbuf); e_draw(VG_PRIM_POINTS, 0, 1, 0, 0);
    same(x, "no texture views on the host: B8G8R8A8 alone");
    ops->state_reset(x);
    ops->arenas(x, &a);
    ops->draw_buf(x, &d);
    e0(); hv[0] = 0; e_views(1, hv); e_samps(1, hs); e_vb(16, a.vb_off, a.vbuf); e_draw(VG_PRIM_POINTS, 0, 1, 0, 0);
    same(x, "after state_reset: the units empty, sent again with the vertex buffer");
    ops->res_release(x, t);
    ops->ctx_destroy(x);
    ops->screen_close(s);

    printf("%d checks, %d failed\n", checks, failures);
    return failures != 0;
}
