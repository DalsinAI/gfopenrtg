/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library: the drawing area and the batches. Every draw call adds
 * stream v1.2 commands (stream3d.h) to the context's batch; vertices and
 * small data go in the batch's data area. A flush puts the drawing area's
 * SURFACE in front, then runs the batch:
 *  - W3D_OpenGPU: OGPU_Submit to gfopengpu.library, which hands it to its back
 *    end (ACRTG.gpu: the host on AmiCradle), and OGPU_Wait;
 *  - W3D_CPU: the 3D core linked in here, on this 68k;
 *  - virgl: the host's graphics chip draws the batch (w3d_vg.c, through
 *    gfopengpu.library's OGPU_OP_VIRGL). The frame lives on the host while
 *    the program holds the hardware lock; at every sync point (W3D_Flush,
 *    W3D_UnLockHardware, W3D_WaitIdle, W3D_FlushFrame, a new drawing area,
 *    every draw when the program draws unlocked and direct) the host writes
 *    it straight into the drawing area (ACV_PRESENT, the bitmap locked), so
 *    the program's own 2D between 3D batches lands where Warp3D's rules put
 *    it, and the next batch takes the area's pixels up again. Z and stencil
 *    stay on the host until the program reads or writes them (w3d_zsync).
 * A batch carries all the state it draws with, as GFOpenGPU's back ends keep
 * nothing between batches.
 *
 * On the virgl route (docs/design/OpenGPU-Shared-Pipe-Plan.md, stage M7) the draw calls are calls to the route
 * (w3dvg_clip, w3dvg_triangles ...), which makes the shared pipe's calls from them; no batch is recorded
 * (x->live). Only the vertices are kept, in the data area, until the batch ends. The drawing area's bitmap is
 * locked only around a send that reads or writes it (w3d_area_lock, asked for by the route), never between two of
 * the program's calls. A drawing area the host can't take (not 32-bit ARGB or 16-bit 565) is drawn by the 3D core
 * from a recorded batch, as on the CPU route. */
#include <exec/memory.h>
#include <graphics/rastport.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>

#include "w3d_internal.h"
#include "../../include/gfopengpu/gfopengpu.h"
#include "../../include/proto/gfopengpu.h"

/* ---- the drawing area ------------------------------------------------------------- */

/* W3D_FMT_ for a CyberGraphX pixel format. */
static ULONG w3dfmt_of(ULONG pixfmt)
{
    switch (pixfmt) {
    case PIXFMT_LUT8: return W3D_FMT_CLUT;
    case PIXFMT_RGB15: return W3D_FMT_R5G5B5;
    case PIXFMT_BGR15: return W3D_FMT_B5G5R5;
    case PIXFMT_RGB15PC: return W3D_FMT_R5G5B5PC;
    case PIXFMT_BGR15PC: return W3D_FMT_B5G5R5PC;
    case PIXFMT_RGB16: return W3D_FMT_R5G6B5;
    case PIXFMT_BGR16: return W3D_FMT_B5G6R5;
    case PIXFMT_RGB16PC: return W3D_FMT_R5G6B5PC;
    case PIXFMT_BGR16PC: return W3D_FMT_B5G6R5PC;
    case PIXFMT_RGB24: return W3D_FMT_R8G8B8;
    case PIXFMT_BGR24: return W3D_FMT_B8G8R8;
    case PIXFMT_ARGB32: return W3D_FMT_A8R8G8B8;
    case PIXFMT_BGRA32: return W3D_FMT_B8G8R8A8;
    case PIXFMT_RGBA32: return W3D_FMT_R8G8B8A8;
    }
    return 0;
}

ULONG W3D_GetDestFmt_all(void)
{
    return W3D_FMT_R5G5B5 | W3D_FMT_B5G5R5 | W3D_FMT_R5G5B5PC | W3D_FMT_B5G5R5PC | W3D_FMT_R5G6B5
         | W3D_FMT_B5G6R5 | W3D_FMT_R5G6B5PC | W3D_FMT_B5G6R5PC | W3D_FMT_R8G8B8 | W3D_FMT_B8G8R8
         | W3D_FMT_A8R8G8B8 | W3D_FMT_A8B8G8R8 | W3D_FMT_R8G8B8A8 | W3D_FMT_B8G8R8A8;
}

static int bytes_of(ULONG w3dfmt)
{
    if (w3dfmt & (W3D_FMT_A8R8G8B8 | W3D_FMT_A8B8G8R8 | W3D_FMT_R8G8B8A8 | W3D_FMT_B8G8R8A8)) return 4;
    if (w3dfmt & (W3D_FMT_R8G8B8 | W3D_FMT_B8G8R8)) return 3;
    if (w3dfmt & W3D_FMT_CLUT) return 1;
    return 2;
}

/* A W3D_Bitmap in a format the core does not draw into: the core draws in an
 * ARGB32 copy, and these move pixels between the two. */
static void wbm_convert(struct w3dctx *x, W3D_Bitmap *wbm, int to_wbm)
{
    W3D_Context *c = x->ctx;
    ULONG f = wbm->format;
    int bpp = bytes_of(f), i, j;
    for (j = 0; j < c->height; j++) {
        UBYTE *p = (UBYTE *)wbm->dest + (j + c->yoffset) * wbm->bprow;
        ULONG *q = (ULONG *)((UBYTE *)x->off + j * x->offbpr);
        for (i = 0; i < c->width; i++, p += bpp, q++) {
            ULONG v = *q, r = (v >> 16) & 255, g = (v >> 8) & 255, b = v & 255, a = v >> 24, w;
            if (to_wbm) {
                switch (f) {
                case W3D_FMT_R5G5B5: case W3D_FMT_R5G5B5PC: w = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); break;
                case W3D_FMT_B5G5R5: case W3D_FMT_B5G5R5PC: w = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3); break;
                case W3D_FMT_R5G6B5PC: w = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); break;
                case W3D_FMT_B5G6R5: case W3D_FMT_B5G6R5PC: w = ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3); break;
                case W3D_FMT_R8G8B8: p[0] = (UBYTE)r; p[1] = (UBYTE)g; p[2] = (UBYTE)b; continue;
                case W3D_FMT_B8G8R8: p[0] = (UBYTE)b; p[1] = (UBYTE)g; p[2] = (UBYTE)r; continue;
                case W3D_FMT_A8B8G8R8: p[0] = (UBYTE)a; p[1] = (UBYTE)b; p[2] = (UBYTE)g; p[3] = (UBYTE)r; continue;
                case W3D_FMT_R8G8B8A8: p[0] = (UBYTE)r; p[1] = (UBYTE)g; p[2] = (UBYTE)b; p[3] = (UBYTE)a; continue;
                case W3D_FMT_B8G8R8A8: p[0] = (UBYTE)b; p[1] = (UBYTE)g; p[2] = (UBYTE)r; p[3] = (UBYTE)a; continue;
                default: continue;
                }
                if (f & (W3D_FMT_R5G5B5PC | W3D_FMT_B5G5R5PC | W3D_FMT_R5G6B5PC | W3D_FMT_B5G6R5PC)) {
                    p[0] = (UBYTE)w; p[1] = (UBYTE)(w >> 8);
                } else { p[0] = (UBYTE)(w >> 8); p[1] = (UBYTE)w; }
            } else {
                ULONG pc = (f & (W3D_FMT_R5G5B5PC | W3D_FMT_B5G5R5PC | W3D_FMT_R5G6B5PC | W3D_FMT_B5G6R5PC)) != 0;
                w = bpp == 2 ? (pc ? p[0] | (p[1] << 8) : (p[0] << 8) | p[1]) : 0;
                a = 255;
                switch (f) {
                case W3D_FMT_R5G5B5: case W3D_FMT_R5G5B5PC: r = (w >> 10) & 31; g = (w >> 5) & 31; b = w & 31; r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2; break;
                case W3D_FMT_B5G5R5: case W3D_FMT_B5G5R5PC: b = (w >> 10) & 31; g = (w >> 5) & 31; r = w & 31; r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2; break;
                case W3D_FMT_R5G6B5PC: r = (w >> 11) & 31; g = (w >> 5) & 63; b = w & 31; r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2; break;
                case W3D_FMT_B5G6R5: case W3D_FMT_B5G6R5PC: b = (w >> 11) & 31; g = (w >> 5) & 63; r = w & 31; r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2; break;
                case W3D_FMT_R8G8B8: r = p[0]; g = p[1]; b = p[2]; break;
                case W3D_FMT_B8G8R8: b = p[0]; g = p[1]; r = p[2]; break;
                case W3D_FMT_A8B8G8R8: a = p[0]; b = p[1]; g = p[2]; r = p[3]; break;
                case W3D_FMT_R8G8B8A8: r = p[0]; g = p[1]; b = p[2]; a = p[3]; break;
                case W3D_FMT_B8G8R8A8: b = p[0]; g = p[1]; r = p[2]; a = p[3]; break;
                }
                *q = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
}

int w3d_open_area(struct w3dctx *x, struct BitMap *bm, W3D_Bitmap *wbm, int yoffset)
{
    W3D_Context *c = x->ctx;
    ULONG fmt;
    w3d_sync(x);                            /* the frame into the old area first */
    w3d_close_area(x);
    c->yoffset = yoffset;
    if (wbm) {
        c->w3dbitmap = W3D_TRUE;
        c->drawregion = (struct BitMap *)wbm;
        c->width = wbm->width; c->height = wbm->height;
        c->bprow = wbm->bprow;
        fmt = wbm->format;
    } else {
        if (!bm || !CyberGfxBase || !GetCyberMapAttr(bm, CYBRMATTR_ISCYBERGFX)) return W3D_ILLEGALBITMAP;
        c->w3dbitmap = W3D_FALSE;
        c->drawregion = bm;
        c->width = (int)GetCyberMapAttr(bm, CYBRMATTR_WIDTH);
        c->height = (int)GetCyberMapAttr(bm, CYBRMATTR_HEIGHT);
        c->bprow = (int)GetCyberMapAttr(bm, CYBRMATTR_XMOD);
        fmt = w3dfmt_of(GetCyberMapAttr(bm, CYBRMATTR_PIXFMT));
    }
    if (c->state & W3D_DOUBLEHEIGHT) c->height /= 2;
    if (!fmt || fmt == W3D_FMT_CLUT) return W3D_UNSUPPORTEDFMT;
    c->format = fmt;
    c->depth = bytes_of(fmt) * 8 == 16 && (fmt & (W3D_FMT_R5G5B5 | W3D_FMT_B5G5R5 | W3D_FMT_R5G5B5PC | W3D_FMT_B5G5R5PC)) ? 15 : bytes_of(fmt) * 8;
    c->chunky = W3D_FALSE;
    c->destalpha = (fmt & (W3D_FMT_A8R8G8B8 | W3D_FMT_A8B8G8R8 | W3D_FMT_R8G8B8A8 | W3D_FMT_B8G8R8A8)) ? W3D_TRUE : W3D_FALSE;
    x->direct = fmt == W3D_FMT_A8R8G8B8 || fmt == W3D_FMT_R5G6B5;
    x->fmt = fmt == W3D_FMT_R5G6B5 ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32;
    /* w3d_sync above put the old area's frame into it. Where that area couldn't be locked the host still holds the
     * frame; the route can't draw into this new area, so the frame is of an area that has gone and is let go of.
     * Nothing else leaves the route with a frame held: stopping the route (w3d_vg_stop) clears it too. */
    if (!x->direct && x->vg_hold) { x->vg_hold = 0; w3d_trace("virgl: a held frame dropped (the drawing area changed)"); }
    if (!x->direct) {
        x->offbpr = (ULONG)c->width * 4;
        x->off = w3d_alloc(x->offbpr * (ULONG)c->height);
        if (!x->off) return W3D_NOMEMORY;
    }
    c->scissor.left = 0; c->scissor.top = 0; c->scissor.width = c->width; c->scissor.height = c->height;
    x->clip_dirty = 1;
    return W3D_SUCCESS;
}

void w3d_close_area(struct w3dctx *x)
{
    if (x->off) { w3d_free(x->off); x->off = 0; }
}

/* ---- running a batch ------------------------------------------------------------------- */

static ogpu_u8 *cpu_map(void *user, ogpu_u32 address, ogpu_u32 length)
{
    (void)user; (void)length;
    return (ogpu_u8 *)address;
}

/* The drawing area's memory for one batch: locked (CyberGraphX) or the copy. */
static int lock_area(struct w3dctx *x)
{
    W3D_Context *c = x->ctx;
    x->bmlock = 0;
    if (c->w3dbitmap) {
        W3D_Bitmap *wbm = (W3D_Bitmap *)c->drawregion;
        if (x->direct) { x->base = (UBYTE *)wbm->dest + c->yoffset * wbm->bprow; x->bpr = (ULONG)wbm->bprow; }
        else { wbm_convert(x, wbm, 0); x->base = (UBYTE *)x->off; x->bpr = x->offbpr; }
        return 1;
    }
    if (x->direct) {
        ULONG base = 0, bpr = 0;
        x->bmlock = LockBitMapTags(c->drawregion, LBMI_BASEADDRESS, (ULONG)&base, LBMI_BYTESPERROW, (ULONG)&bpr, TAG_DONE);
        if (!x->bmlock) return 0;
        x->base = (UBYTE *)base + c->yoffset * bpr;
        x->bpr = bpr;
        c->drawmem = x->base;
    } else {
        struct RastPort rp;
        InitRastPort(&rp);
        rp.BitMap = c->drawregion;
        ReadPixelArray(x->off, 0, 0, x->offbpr, &rp, 0, c->yoffset, c->width, c->height, RECTFMT_ARGB);
        x->base = (UBYTE *)x->off; x->bpr = x->offbpr;
    }
    return 1;
}

static void unlock_area(struct w3dctx *x)
{
    W3D_Context *c = x->ctx;
    if (x->bmlock) { UnLockBitMap(x->bmlock); x->bmlock = 0; }
    if (!x->direct) {
        if (c->w3dbitmap) wbm_convert(x, (W3D_Bitmap *)c->drawregion, 1);
        else {
            struct RastPort rp;
            InitRastPort(&rp);
            rp.BitMap = c->drawregion;
            WritePixelArray(x->off, 0, 0, x->offbpr, &rp, 0, c->yoffset, c->width, c->height, RECTFMT_ARGB);
        }
    }
}

/* The lock for the route's sends (w3dvg_os.area_lock): held from here to w3d_area_unlock, which the route calls
 * once the block has run. */
int w3d_area_lock(struct w3dctx *x, ULONG *address, ULONG *bpr)
{
    if (!lock_area(x)) return 0;
    *address = (ULONG)x->base;
    *bpr = x->bpr;
    return 1;
}

void w3d_area_unlock(struct w3dctx *x) { unlock_area(x); }

/* The draws of a new batch go straight to the route: the graphics card draws them, into an area it can take.
 * Decided when a batch starts; it holds until that batch has gone. */
static int live_now(struct w3dctx *x)
{
    return x->route == ROUTE_VIRGL && x->vg && x->direct;
}

/* What a batch starts with, as calls: the drawing area (which one; its memory is asked for when a send
 * needs it) and the depth and stencil buffers. */
static void live_start(struct w3dctx *x, int data)
{
    W3D_Context *c = x->ctx;
    w3dvg_begin(x->vg, data ? (ULONG)x->vmem : 0, data ? VERT_BYTES : 0);
    w3dvg_area(x->vg, (ULONG)c->drawregion + (ULONG)c->yoffset, 0, c->width, c->height, (ULONG)x->fmt);
    if (x->zbuf) w3dvg_depth(x->vg, OGPU_KIND_DEPTH, (ULONG)x->zbuf, x->zbpr, c->width, c->height, (ULONG)x->zfmt);
    if (x->sbuf) w3dvg_depth(x->vg, OGPU_KIND_STENCIL, (ULONG)x->sbuf, (ULONG)c->width, c->width, c->height, OGPU_FMT_S8);
}

/* The end of a batch of direct calls: what the pipe holds goes to the host. Nothing is kept to draw again, so when
 * the host fails the block the batch's draws are lost for this frame, and the 68k draws from the next one on. */
static ULONG flush_live(struct w3dctx *x, int present)
{
    int shown = 0;
    ULONG r = W3D_SUCCESS;
    LONG e;
    if (!x->s3.body) {
        if (!(present && x->vg_hold)) return W3D_SUCCESS;
        live_start(x, 0);                       /* nothing new, but the host's frame isn't in the area yet */
    }
    w3d_trace("flush (direct) %ld bytes", (long)x->s3.vused);
    x->s3.body = 0;
    x->live = 0;
    x->sent_tex = 0;
    e = w3dvg_end(x->vg, present, &shown);
    x->vg_hold = !shown;
    if (present && !shown) r = W3D_NOTVISIBLE;  /* the area couldn't be locked: the frame waits on the host */
    x->s3o->reset(&x->s3);
    if (e > 0) {                                /* drawn, but not all of it */
        x->vg_errors += (ULONG)e;
        w3d_trace("virgl: %ld commands not drawn (%ld so far)", (long)e, (long)x->vg_errors);
    } else if (e < 0) {
        w3d_trace("virgl: the host failed a block; its draws are lost, and the CPU draws from here");
        x->vg_errors++;
        w3dvg_read_z(x->vg);
        w3d_vg_stop(x);
        x->route = ROUTE_CPU;
    }
    return r;
}

static ULONG flush(struct w3dctx *x, int present)
{
    W3D_Context *c = x->ctx;
    struct OGPUPipeS3DBuf area;
    long words;
    UBYTE *stream;
    ULONG r = W3D_SUCCESS;
    if (x->live || (!x->s3.body && live_now(x))) return flush_live(x, present);
    if (!x->s3.body) return W3D_SUCCESS;
    w3d_trace("flush %ld words %ld bytes", (long)x->s3.b.words, (long)x->s3.vused);
    x->sent_tex = 0;
    if (x->s3.b.overflow) r = W3D_QUEUEFAILED;
    if (!lock_area(x)) { x->s3.body = 0; x->s3.list_at = -1; x->s3o->reset(&x->s3); return W3D_NOTVISIBLE; }
    /* SURFACE and TARGET in front of the commands */
    area.address = (unsigned long)x->base; area.bpr = x->bpr; area.w = c->width; area.h = c->height; area.format = x->fmt;
    words = x->s3o->finish(&x->s3, &area, &stream);
    if (x->route == ROUTE_GPU && OpenGPUBase) {
        ULONG fence = 0;
        LONG e = OGPU_Submit(stream, (ULONG)words, &fence);
        if (e == OGPU_OK) e = OGPU_Wait(fence);
        if (e != OGPU_OK && e != OGPU_ERR_BADOP) r = W3D_WARNING;
    } else {
        ogpu_core_init(&x->core);
        x->core.map = cpu_map;
        x->core.fence = 0;
        x->core.ext = 0;
        x->core.user = 0;
        ogpu3d_init(&x->d3, &x->core);
        if (x->vg) w3dvg_read_z(x->vg);     /* the host's Z, before the core reads it */
        ogpu3d_run(&x->d3, stream, words);
        if (x->vg) w3dvg_stale(x->vg);      /* the area and Z changed behind the host */
    }
    unlock_area(x);
    x->s3o->reset(&x->s3);
    return r;
}

/* A batch run because it is full, or a texture changes under it: with the
 * hardware locked the program draws no 2D, so the frame may stay on the host. */
ULONG w3d_flush(struct w3dctx *x) { return flush(x, !x->ctx->HWlocked); }

/* A sync point: the frame in the drawing area afterwards. */
ULONG w3d_sync(struct w3dctx *x) { return flush(x, 1); }

void w3d_maybe_flush(struct w3dctx *x)
{
    W3D_Context *c = x->ctx;
    if (!(c->state & W3D_INDIRECT) && !c->HWlocked) w3d_sync(x);
}

/* Before the CPU reads or writes Z or stencil (W3D_ReadZSpan ...): what is
 * queued drawn, and the host's buffers in memory; after a write the host
 * takes memory's up again before its next depth test. */
void w3d_zsync(struct w3dctx *x, int writes)
{
    w3d_flush(x);
    if (!x->vg) return;
    w3dvg_read_z(x->vg);
    if (writes) w3dvg_stale_z(x->vg);
}

/* ---- building ---------------------------------------------------------------------------- */

UBYTE *w3d_data(struct w3dctx *x, ULONG bytes)
{
    return x->s3o->data(&x->s3, bytes);
}

static ULONG fogmode_stream(struct w3dctx *x)
{
    W3D_Context *c = x->ctx;
    if (x->fogmode_v5 >= 0) {
        switch (x->fogmode_v5) {
        case W3D_FOG_W_LINEAR: return OGPU_FOG_LINEAR;
        case W3D_FOG_W_EXP: return OGPU_FOG_EXP;
        case W3D_FOG_W_EXP_2: return OGPU_FOG_EXP2;
        }
        return OGPU_FOG_VERTEX;                 /* Z fog: worked out per vertex */
    }
    if (c->state & W3D_FOG_COORD) return OGPU_FOG_VERTEX;
    switch (x->fogmode) {
    case W3D_FOG_LINEAR: return OGPU_FOG_LINEAR;
    case W3D_FOG_EXP: return OGPU_FOG_EXP;
    case W3D_FOG_EXP_2: return OGPU_FOG_EXP2;
    case W3D_FOG_INTERPOLATED: return OGPU_FOG_VERTEX;
    }
    return 0;
}

static void send_render3d(struct w3dctx *x)
{
    W3D_Context *c = x->ctx;
    ULONG st = c->state, en = 0, a[OGPU_R3D_ARGS];
    struct w3dtex *t = x->sent_tex ? TEX(x->sent_tex) : 0;
    int i;
    LONG aref = w3d_f2l(x->aref * 255.0f);
    if ((st & W3D_PERSPECTIVE) && !W3DBase->no_persp) en |= OGPU_R3D_PERSPECTIVE;
    if (st & W3D_GOURAUD) en |= OGPU_R3D_GOURAUD;
    if ((st & W3D_ZBUFFER) && x->zbuf) en |= OGPU_R3D_ZTEST;
    if (st & W3D_ZBUFFERUPDATE) en |= OGPU_R3D_ZWRITE;
    if (st & W3D_BLENDING) en |= OGPU_R3D_BLEND;
    if ((st & W3D_FOGGING) && fogmode_stream(x) && !W3DBase->no_fog) en |= OGPU_R3D_FOG;
    if (st & W3D_ALPHATEST) en |= OGPU_R3D_ALPHATEST;
    if ((st & W3D_STENCILBUFFER) && x->sbuf) en |= OGPU_R3D_STENCIL;
    if (st & W3D_LOGICOP) en |= OGPU_R3D_LOGICOP;
    if ((st & W3D_CHROMATEST) && t && t->chroma_mode > W3D_CHROMATEST_NONE) en |= OGPU_R3D_CHROMA;
    if (st & W3D_SPECULAR) en |= OGPU_R3D_SPECULAR;
    if (st & W3D_DITHERING) en |= OGPU_R3D_DITHER;
    if ((st & W3D_POLYGON_STIPPLE) && x->stipple) en |= OGPU_R3D_STIPPLE;
    if (st & W3D_TEXMAPPING) en |= OGPU_R3D_TEXTURE;
    for (i = 0; i < OGPU_R3D_ARGS; i++) a[i] = 0;
    a[0] = en;
    a[1] = x->zfunc;
    a[2] = x->afunc | ((ULONG)(aref < 0 ? 0 : aref > 255 ? 255 : aref) << 16);
    a[3] = x->bsrc | (x->bdst << 16);
    a[4] = 0;
    a[5] = x->cmask | ((x->penmask & 255) << 8);
    a[6] = x->logicop;
    a[7] = fogmode_stream(x);
    {
        W3D_Color fc;
        fc.r = c->fog.fog_color.r; fc.g = c->fog.fog_color.g; fc.b = c->fog.fog_color.b; fc.a = 1.0f;
        a[8] = w3d_colour(&fc);
    }
    a[9] = w3d_fbits(c->fog.fog_start);
    a[10] = w3d_fbits(c->fog.fog_end);
    a[11] = w3d_fbits(c->fog.fog_density);
    a[12] = x->sfunc | ((x->sref & 255) << 8) | ((x->smask & 255) << 16);
    a[13] = x->sfail | (x->szfail << 8) | (x->szpass << 16) | ((x->swmask & 255) << 24);
    a[14] = t ? (ULONG)t->chroma_mode : W3D_CHROMATEST_NONE;
    a[15] = t ? t->chroma_lo : 0;
    a[16] = t ? t->chroma_hi : 0;
    a[17] = w3d_colour(&x->current);
    a[19] = (ULONG)x->stipple;
    if (x->live) w3dvg_render3d(x->vg, a);
    else x->s3o->render3d(&x->s3, (const unsigned long *)a);
    x->r3d_dirty = 0;
}

/* Drawing stays inside the scissor rectangle (the whole area unless the
 * program set one). */
void w3d_send_clip(struct w3dctx *x)
{
    W3D_Context *c = x->ctx;
    int l = c->scissor.left, t = c->scissor.top, r = l + c->scissor.width, b = t + c->scissor.height;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > c->width) r = c->width;
    if (b > c->height) b = c->height;
    if (r <= l || b <= t) { l = t = 0; r = b = 1; }          /* nothing: one pixel the program asked to lose */
    if (x->live) w3dvg_clip(x->vg, l, t, r - l, b - t);
    else x->s3o->clip(&x->s3, l, t, r - l, b - t);
    x->clip_dirty = 0;
}

static void send_texture(struct w3dctx *x, W3D_Texture *tex)
{
    if (!tex) {
        if (x->live) w3dvg_texture(x->vg, 0, 0, 1, 1, OGPU_FMT_ARGB32, 0, 1, 1, 1, 1, 0, 1, 0);
        else x->s3o->texture(&x->s3, 0, 0, 0, 1, 1, OGPU_FMT_ARGB32, 0, 0, 1, 1, 1, 1, 0, 1, 0);
    } else {
        struct w3dtex *t = TEX(tex);
        unsigned long lv[OGPU_TEX_LEVELS];
        int i, env = x->genv ? x->genv : t->env, min = t->min, mag = t->mag;
        ULONG envc = x->genv ? x->genvcolour : t->envcolour;
        for (i = 1; i < t->levels; i++) lv[i - 1] = (unsigned long)t->level[i];
        if (W3DBase->no_light && (env == W3D_MODULATE || env == W3D_DECAL || env == W3D_BLEND)) env = W3D_REPLACE;
        if (W3DBase->no_filter) {               /* nearest texels, the same levels */
            mag = W3D_NEAREST;
            min = min == W3D_LINEAR ? W3D_NEAREST : min == W3D_LINEAR_MIP_NEAREST ? W3D_NEAREST_MIP_NEAREST
                : min == W3D_LINEAR_MIP_LINEAR ? W3D_NEAREST_MIP_LINEAR : min;
        }
        if (x->live) {
            w3dvg_texenv(x->vg, (ULONG)env, envc);
            w3dvg_texture(x->vg, (ULONG)t->level[0], (ULONG)t->w * 4, t->w, t->h, OGPU_TEX_FORMAT(OGPU_FMT_ARGB32, t->base),
                          t->generation, (ULONG)min, (ULONG)mag, (ULONG)t->wrap_s, (ULONG)t->wrap_t, t->border,
                          (ULONG)t->levels, (const ULONG *)lv);
        } else {
            x->s3o->texenv(&x->s3, 0, env, envc);
            x->s3o->texture(&x->s3, 0, (unsigned long)t->level[0], (ULONG)t->w * 4, t->w, t->h,
                         OGPU_TEX_FORMAT(OGPU_FMT_ARGB32, t->base), 0, t->generation, min, mag,
                         t->wrap_s, t->wrap_t, t->border, t->levels, lv);
        }
        x->sent_gen = t->generation;
    }
    x->sent_tex = tex;
    x->tex_dirty = 0;
    x->s3.list_at = -1;
}

/* Room for `words` more commands and `vbytes` more data; flushes when not. */
int w3d_room(struct w3dctx *x, long words, ULONG vbytes)
{
    if (!x->s3o->room(&x->s3, words + 96, vbytes)) {
        w3d_flush(x);
        if (words + 96 > BATCH_WORDS - PREFIX_WORDS || vbytes > VERT_BYTES) return 0;
    }
    return 1;
}

void w3d_begin(struct w3dctx *x)
{
    struct OGPUPipeS3DBuf z, st;
    if (x->live || (!x->s3.body && live_now(x))) {
        if (!x->s3.body) {                      /* a new batch of direct calls: nothing is sent yet */
            x->live = 1;
            x->s3.body = 1;                     /* (the batch is open; its commands are the pipe's, not the emitter's) */
            live_start(x, 1);
            x->sent_tex = 0;
            x->clip_dirty = x->r3d_dirty = 1;
        }
        if (x->clip_dirty) w3d_send_clip(x);
        return;
    }
    z.address = (unsigned long)x->zbuf; z.bpr = x->zbpr; z.w = x->ctx->width; z.h = x->ctx->height; z.format = x->zfmt;
    st.address = (unsigned long)x->sbuf; st.bpr = (ULONG)x->ctx->width; st.w = x->ctx->width; st.h = x->ctx->height;
    st.format = OGPU_FMT_S8;
    if (x->s3o->begin(&x->s3, x->zbuf ? &z : 0, x->sbuf ? &st : 0)) {    /* a new batch: nothing is sent yet */
        x->sent_tex = 0;
        x->clip_dirty = x->r3d_dirty = 1;
    }
    if (x->clip_dirty) w3d_send_clip(x);
}

void w3d_select_texture(struct w3dctx *x, W3D_Texture *t)
{
    if (!(x->ctx->state & W3D_TEXMAPPING)) t = 0;
    w3d_begin(x);
    if (t != x->sent_tex || x->tex_dirty || (t && TEX(t)->generation != x->sent_gen)) {
        int chroma_changed = (x->sent_tex && TEX(x->sent_tex)->chroma_mode > 1) || (t && TEX(t)->chroma_mode > 1);
        send_texture(x, t);
        if (chroma_changed) x->r3d_dirty = 1;
    }
    if (x->r3d_dirty) { send_render3d(x); x->s3.list_at = -1; }
}

/* Room in the batch's data area for n vertices (0 when there is none even
 * after a flush). */
struct ovtx *w3d_vspace(struct w3dctx *x, ULONG n)
{
    ULONG bytes = n * sizeof(struct ovtx);
    if (!w3d_room(x, 16, bytes)) return 0;
    return (struct ovtx *)w3d_data(x, bytes);
}

/* n vertices already in the data area (w3d_vspace), as a list, strip or
 * fan (OGPU_LAY_). Consecutive lists with the same state become one
 * command. Call w3d_select_texture first: it sends the state. */
void w3d_emit(struct w3dctx *x, struct ovtx *d, ULONG n, int prim)
{
    if (x->live) {
        if (n) w3dvg_triangles(x->vg, (ULONG)d, n, sizeof(struct ovtx), x->sent_tex ? OGPU_TRI_UNIT0 : OGPU_TRI_NOTEX,
                               OVTX_LAYOUT | (ULONG)prim);
        return;
    }
    x->s3o->emit(&x->s3, (unsigned long)d, n, sizeof(struct ovtx), prim, x->sent_tex ? OGPU_TRI_UNIT0 : OGPU_TRI_NOTEX,
                 OVTX_LAYOUT);
}

/* The whole drawing area in one colour (ARGB), past the scissor. */
ULONG w3d_fill(struct w3dctx *x, ULONG argb)
{
    W3D_Context *c = x->ctx;
    ULONG raw = argb;
    if (!w3d_room(x, 16, 0)) return W3D_QUEUEFAILED;
    w3d_begin(x);
    if (x->fmt == OGPU_FMT_RGB565) raw = (((argb >> 19) & 31) << 11) | (((argb >> 10) & 63) << 5) | ((argb >> 3) & 31);
    if (x->live) {
        w3dvg_clip(x->vg, 0, 0, 0, 0);
        w3dvg_fill(x->vg, 0, 0, c->width, c->height, raw);
    } else {
        x->s3o->clip(&x->s3, 0, 0, 0, 0);
        x->s3o->fill(&x->s3, 0, 0, c->width, c->height, raw);
    }
    x->clip_dirty = 1;
    return W3D_SUCCESS;
}

void w3d_clear_buffer(struct w3dctx *x, int kind, ULONG value)
{
    W3D_Context *c = x->ctx;
    if (!w3d_room(x, 16, 0)) return;
    w3d_begin(x);
    if (x->live) {
        w3dvg_clip(x->vg, 0, 0, 0, 0);
        w3dvg_clear3d(x->vg, (ULONG)kind, 0, 0, c->width, c->height, value);
    } else {
        x->s3o->clip(&x->s3, 0, 0, 0, 0);
        x->s3o->clear3d(&x->s3, kind, 0, 0, c->width, c->height, value);
    }
    x->clip_dirty = 1;
}
