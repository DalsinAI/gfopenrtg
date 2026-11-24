/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's virgl route: a batch of OGPU stream v1.2 (what the CPU
 * route gives the 3D core) drawn through virglrenderer. See w3d_vg.h.
 *
 * A batch becomes one ACVirgl request block, in this order:
 *   1. resources made now and texture uploads (ACV_RES_CREATE, ACV_PUT), and
 *      the drawing area's pixels when the frame starts without filling it;
 *   2. the batch's vertex memory (one ACV_PUT, 32-bit units turned round),
 *      the vertices copied here (quads for clears, vertices from elsewhere),
 *      and the indices (16-bit units);
 *   3. the command stream (ACV_SUBMIT);
 *   4. resources let go of (ACV_RES_UNREF);
 *   5. ACV_PRESENT into the drawing area, when the program syncs.
 * The vertex and index buffers are rings: each block's data goes after the
 * last block's, never over it (virglrenderer writes buffers unsynchronized,
 * as minigl.library's virgl route found).
 *
 * The numbers of the virgl protocol are virglrenderer's (Mesa's MIT
 * virgl_protocol.h, virgl_hw.h, Gallium's p_defines.h), as
 * library/pipe/virgl_cmd.h carries them for MiniGL and Warp3D
 * alike; the encoder is the Team's own. */
#include "w3d_vg.h"
#include "../../include/gfopengpu/stream3d.h"

#include "../pipe/virgl_cmd.h"
#include "../../include/gfopengpu/pipe.h"
#include "w3d_vg_ffkey.h"

#define ADDR(p) ((ULONG)(p))

/* Warp3D's numbers (as the stream carries them) to Gallium's. */
static const UBYTE cmp_of[9] = { 7, 0, 1, 6, 3, 4, 5, 2, 7 };        /* W3D_Z_/W3D_A_: NEVER LESS GEQUAL LEQUAL GREATER NOTEQUAL EQUAL ALWAYS */
static const UBYTE scmp_of[9] = { 7, 0, 7, 1, 3, 2, 6, 4, 5 };       /* W3D_ST_: NEVER ALWAYS LESS LEQUAL EQUAL GEQUAL GREATER NOTEQUAL */
static const UBYTE sop_of[9] = { 0, 0, 1, 2, 3, 4, 7, 5, 6 };        /* KEEP ZERO REPLACE INCR DECR INVERT INCR_WRAP DECR_WRAP */
static const UBYTE logic_of[17] = { 12, 0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15 };
static const UBYTE factor_of[16] = {                                 /* W3D_ZERO .. W3D_ONE_MINUS_CONSTANT_ALPHA */
    0x01, 0x11, 0x01, 0x02, 0x05, 0x12, 0x15, 0x03, 0x13, 0x04, 0x14, 0x06, 0x07, 0x17, 0x08, 0x18
};

/* ---- sizes ---------------------------------------------------------------------------------------- */

#define CMD_WORDS   (32 * 1024)
#define PRE_WORDS   (4 * 1024)
#define POST_WORDS  512
#define BLK_SLACK   (64 + POST_WORDS)
#define VA_BYTES    (128 * 1024)        /* vertices copied here */
#define IA_COUNT    (64 * 1024)         /* 16-bit indices */
#define REGION_MAX  (256 * 1024)        /* a batch's own vertex memory, PUT as it is */
#define RING        4                   /* blocks a buffer holds */
#define DRAW_ROOM   2048                /* stream words a draw's state may need */
#define TEXTS       96                  /* textures kept on the host */
#define CACHE       32                  /* objects of one kind */
#define SHADERS     48
#define LAYOUTS     8
#define TEXT_BYTES  4096

struct res {
    ULONG handle, fmt, w, h, levels;
    struct res *next_dead;
    OGPUPipeRes *pr;                    /* the pipe's (stage M6); 0 on the old route */
};

struct tex {
    ULONG addr, w, h, levels, gen, base;
    struct res *res;
    ULONG view;
    ULONG used;                         /* a stamp, for letting the oldest go */
};

struct cache { ULONG key[CACHE][4]; ULONG handle[CACHE]; int n, next; };

/* What a draw is drawn with (the stream's state, or a clear's own). */
struct pipe {
    ULONG blend[2], dsa[4], rs, vs, fs, sref, bconst;
    ULONG stipple;                      /* its address, 0 none */
};

struct w3dvg {
    struct w3dvg_os os;
    const struct OGPUPipeOps *pipe;     /* the shared pipe (M2: its screen and transport) */
    OGPUPipeScreen *screen;
    /* stage M6: the pipe's context. Its block, resources, arenas, state objects and shaders are the pipe's; this
     * file still writes the stream's other commands (cmd_room) until M7. 0: the old route (ENV:Warp3D/PipeOld=1,
     * for one release), every block this file's own. */
    OGPUPipeCtx *px;
    ULONG seq;                          /* the pipe's stream number this file last saw */
    ULONG *pnva, *pnia;                 /* the pipe's arenas' use: written back after each draw */
    struct res *rbuf;                   /* the batches' vertex memory goes here (a ring), PUT from where it is */
    ULONG rb_off, rb_size, rb_at, rb_seq;   /* the ring's next place, its size; this block's place (+1 seq) */
    ULONG vbuf_h, ibuf_h;               /* the pipe's vertex and index buffers' handles, this block's */
    ULONG g_buf, b_vbuf;                /* the run's vertex buffer, the bound one */
    ULONG ctx_id, host_flags, caps_bits;
    int gles;
    /* the block being built */
    ULONG *blk, npre;                   /* creates and puts before the stream */
    ULONG *post, npost;
    ULONG *cmd, ncmd;
    UBYTE *va; ULONG nva;               /* vertices copied */
    UWORD *ia; ULONG nia;
    int open;                           /* a block is being built */
    ULONG region, region_len;           /* the batch's vertex memory, PUT at vb_off when used */
    int region_used;
    struct res *vbuf, *ibuf, *dead;
    ULONG vb_off, vb_size, ib_off, ib_size, vb_block, ib_block;
    /* the host's buffers */
    struct res *colour, *depth, *stage;
    ULONG surf_c, surf_z, zfmt;
    int have_frame;                     /* the colour buffer holds the drawing area's pixels */
    ULONG frame_addr;                   /* ... of this area */
    ULONG next_obj;
    /* the stream's state */
    ULONG slot[4][5];                   /* SURFACE: address, bpr, w, h, format */
    int target;
    ULONG r3d[OGPU_R3D_ARGS];
    ULONG env, envc;
    ULONG t_addr, t_bpr, t_w, t_h, t_fmt, t_gen, t_filter, t_wrap, t_border, t_levels, t_lv[OGPU_TEX_LEVELS];
    ULONG z_addr, z_fmt, z_bpr, z_w, z_h, s_addr, s_bpr, s_w, s_h;
    int tri;                            /* a TRIANGLES is being prepared: its area stops at the buffers */
    int have_z;                         /* the depth buffer holds memory's Z and stencil */
    ULONG *zstage, zstage_bytes;        /* Z and stencil packed for the host (24/8) */
    ULONG *cstage, cstage_bytes;        /* a padded drawing area's rows, packed */
    LONG cx, cy, cw, ch;
    /* what is bound on the host */
    ULONG b_blend, b_dsa, b_rs, b_vs, b_fs, b_ve, b_view, b_samp, b_sref, b_bconst, b_stipple;
    ULONG b_stride, b_vboff, b_iboff;
    LONG sc[4];
    int fb_dirty, vp_dirty, sc_dirty;
    ULONG vc[VC_N * 4], fc[FC_N * 4];
    int vc_ok, fc_ok;
    struct cache c_blend, c_dsa, c_rs, c_samp;
    ULONG vs_key[SHADERS], fs_key[SHADERS], vs_h[SHADERS], fs_h[SHADERS];
    int nvs, nfs;
    ULONG ve_key[LAYOUTS], ve_h[LAYOUTS];
    int nve;
    char *text;
    struct tex tex[TEXTS];
    ULONG stamp;
    /* the draw run being merged */
    int g_on;
    ULONG g_layout, g_stride, g_base, g_ifirst, g_tex;
    ULONG errors;
    /* direct calls (w3dvg_begin, stage M7 part 2): the batch's vertex memory fills as the draws come, so each run of
     * draws PUTs the part it reads when it ends, at one place in the ring for the whole batch */
    int grow;
    ULONG g_lo, g_hi;                   /* the run's part of the region (offsets); g_hi 0: none */
    ULONG region_top;                   /* the region's end as drawn from, this batch */
    ULONG declined;                     /* commands of this batch left out */
    int area_locked;                    /* the platform's lock on the drawing area is held (a load or a present) */
    int presented;                      /* the last block carried the present */
    int lost;                           /* a block of this batch didn't run (the host is gone): its draws are lost */
    struct w3dvg_stats st;
};

/* ---- small things ---------------------------------------------------------------------------------- */

static ULONG rd32(const UBYTE *p) { return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3]; }

static ULONG fbits(float f)
{
    union { float f; ULONG u; } x;
    x.f = f;
    return x.u;
}

static void say(struct w3dvg *v, const char *m) { if (v->os.log) v->os.log(v->os.user, m); }

static void *alloc(struct w3dvg *v, ULONG n) { return v->os.alloc(v->os.user, n); }
static void dealloc(struct w3dvg *v, void *p) { if (p) v->os.free(v->os.user, p); }

static ULONG new_obj(struct w3dvg *v)
{
    v->st.objects++;
    return v->px ? v->pipe->obj_new(v->px) : ++v->next_obj;   /* on the pipe: its one namespace */
}

static void blk_cmd(ULONG *p, ULONG cmd, ULONG n, const ULONG *args)
{
    ULONG i;
    VG_PUTW(p, ACV_HDR(cmd, n + 2));
    VG_PUTW(p + 1, 0);
    for (i = 0; i < n; i++) VG_PUTW(p + 2 + i, args[i]);
}
#define BLK_WORDS(n) ((n) + 2)
static LONG result(const ULONG *blk, ULONG at) { return (LONG)VG_GETW(blk + at + 1); }

static int run(struct w3dvg *v, ULONG *blk, ULONG words)
{
    v->st.blocks++;
    return v->pipe->run(v->screen, blk, words * 4) ? -1 : 0;
}

static void block_send(struct w3dvg *v, int present);
static void block_begin(struct w3dvg *v);
static void end_group(struct w3dvg *v);
static void px_sync(struct w3dvg *v);

/* Room in the stream; the block goes first when there is none. */
static ULONG *cmd(struct w3dvg *v, ULONG n)
{
    ULONG *p;
    if (v->px) {
        /* need() made the room a draw needs, so the pipe sends nothing here mid-run */
        if (!(p = v->pipe->cmd_room(v->px, n))) p = v->cmd;     /* larger than a stream: dropped (never, in Warp3D) */
        if (v->pipe->sent(v->px) != v->seq) px_sync(v);
        return p;
    }
    if (v->ncmd + n > CMD_WORDS) { end_group(v); block_send(v, 0); block_begin(v); }
    p = v->cmd + v->ncmd;
    v->ncmd += n;
    return p;
}

static ULONG *pre(struct w3dvg *v, ULONG n)
{
    ULONG *p;
    if (v->npre + n > PRE_WORDS) { end_group(v); block_send(v, 0); block_begin(v); }
    p = v->blk + v->npre;
    v->npre += n;
    return p;
}

/* ---- resources ------------------------------------------------------------------------------------- */

static struct res *res_create(struct w3dvg *v, ULONG target, ULONG fmt, ULONG bind, ULONG w, ULONG h, ULONG levels)
{
    struct res *r = alloc(v, sizeof *r);
    ULONG a[12];
    if (!r) return 0;
    if (v->px) {
        struct OGPUPipeResDesc d;
        d.target = target; d.format = fmt; d.bind = bind; d.width = w; d.height = h; d.levels = levels;
        d.flags = target == VG_TARGET_BUFFER ? OGPU_RES_STREAM : 0;
        if (!(r->pr = v->pipe->res_create(v->px, &d))) { dealloc(v, r); return 0; }
        r->handle = v->pipe->res_handle(r->pr);     /* never renamed: this file never marks one used */
        r->fmt = fmt; r->w = w; r->h = h; r->levels = levels;
        if (v->pipe->sent(v->px) != v->seq) px_sync(v);
        return r;
    }
    r->handle = ADDR(r);
    r->fmt = fmt; r->w = w; r->h = h; r->levels = levels;
    a[0] = r->handle; a[1] = target; a[2] = fmt; a[3] = bind;
    a[4] = w; a[5] = h; a[6] = 1; a[7] = 1; a[8] = levels ? levels - 1 : 0; a[9] = 0; a[10] = 0;
    a[11] = v->ctx_id;
    blk_cmd(pre(v, BLK_WORDS(12)), ACV_RES_CREATE, 12, a);
    return r;
}

/* Let go of after this block's stream (the draws in it still read it). */
static void res_release(struct w3dvg *v, struct res *r)
{
    ULONG a[1];
    if (!r) return;
    if (v->px) {
        v->pipe->res_release(v->px, r->pr);         /* the host lets go of it after the block that last used it */
        dealloc(v, r);
        if (v->pipe->sent(v->px) != v->seq) px_sync(v);
        return;
    }
    if (v->npost + BLK_WORDS(1) > POST_WORDS) {
        if (v->open) { end_group(v); block_send(v, 0); block_begin(v); }
    }
    a[0] = r->handle;
    blk_cmd(v->post + v->npost, ACV_RES_UNREF, 1, a);
    v->npost += BLK_WORDS(1);
    r->next_dead = v->dead;
    v->dead = r;
}

static void put(struct w3dvg *v, struct res *r, ULONG level, ULONG x, ULONG y, ULONG w, ULONG h,
                ULONG src, ULONG stride, ULONG bytes, ULONG swap)
{
    ULONG a[14];
    if (v->px) {
        struct OGPUBox b;
        b.x = (LONG)x; b.y = (LONG)y; b.z = 0; b.w = w; b.h = h; b.d = 1;
        v->pipe->res_update(v->px, r->pr, level, &b, (const void *)src, stride, swap);   /* the same bytes */
        v->st.puts++;
        v->st.put_bytes += bytes;
        if (v->pipe->sent(v->px) != v->seq) px_sync(v);
        return;
    }
    a[0] = r->handle; a[1] = v->ctx_id; a[2] = level; a[3] = stride; a[4] = 0;
    a[5] = x; a[6] = y; a[7] = 0; a[8] = w; a[9] = h; a[10] = 1;
    a[11] = src; a[12] = bytes; a[13] = swap;
    blk_cmd(pre(v, BLK_WORDS(14)), ACV_PUT, 14, a);
    v->st.puts++;
    v->st.put_bytes += bytes;
}

/* ---- the block ------------------------------------------------------------------------------------ */

/* ---- the pipe's route (stage M6) ------------------------------------------------------------------- */

/* The pipe's arenas, as they are now: where this file writes copied vertices and indices, and the buffers' handles
 * and this block's offsets in them. */
static void px_arenas(struct w3dvg *v)
{
    struct OGPUPipeArenas a;
    v->pipe->arenas(v->px, &a);
    v->va = a.va; v->pnva = a.nva; v->nva = *a.nva;
    v->ia = a.ia; v->pnia = a.nia; v->nia = *a.nia;
    v->vbuf_h = a.vbuf; v->vb_block = a.vb_off;
    v->ibuf_h = a.ibuf; v->ib_block = a.ib_base;
}

/* After anything that may have sent a block: a new stream, new places in the buffers, and the batch's vertex memory
 * PUT again (into the ring's next place) when the next block's draws read it. */
static void px_sync(struct w3dvg *v)
{
    ULONG s = v->pipe->sent(v->px);
    if (s != v->seq) {
        if (!v->grow) {
            if (v->rb_seq == v->seq + 1) v->rb_off += (v->region_len + 255) & ~255UL;
            v->rb_seq = 0;
        }
        v->st.blocks += s - v->seq;
        v->seq = s;
        if (v->g_on) { v->g_on = 0; v->g_lo = v->g_hi = 0; say(v, "virgl: a run of draws was cut by a block (dropped)"); }
        v->b_stride = v->b_vboff = v->b_iboff = 0xFFFFFFFFUL;
        v->b_vbuf = 0;
    }
    px_arenas(v);
}

/* What this file wrote into the arenas, the pipe's to send. */
static void px_commit(struct w3dvg *v)
{
    if (!v->px) return;
    *v->pnva = v->nva;
    *v->pnia = v->nia;
}

/* The ring the batches' vertex memory goes into has room for this batch's (else a new ring; the old one goes after
 * the block that last read it). */
static void px_region_room(struct w3dvg *v)
{
    ULONG need = (v->region_len + 255) & ~255UL;
    if (!need || (v->rbuf && v->rb_off + need <= v->rb_size)) return;
    {
        struct res *r = res_create(v, VG_TARGET_BUFFER, VG_FMT_R8_UNORM, VG_BIND_VERTEX_BUFFER, REGION_MAX * RING, 1, 1);
        if (r) { if (v->rbuf) res_release(v, v->rbuf); v->rbuf = r; v->rb_size = REGION_MAX * RING; }
        v->rb_off = 0;
    }
}

static ULONG px_present_prepare(void *user, ULONG ctx, ULONG handle, ULONG *p, ULONG room);
static void px_present_finish(void *user, const ULONG *blk, ULONG at);

/* A new block: room for it in the vertex and index rings (else new buffers). */
static void block_begin(struct w3dvg *v)
{
    ULONG need = ((v->region_len + 255) & ~255UL) + VA_BYTES;
    if (v->px) { px_sync(v); px_region_room(v); return; }
    if (v->open) {
        /* opened between batches (w3dvg_forget's unrefs) for a smaller
         * vertex region than this batch's: what it holds goes first, and the
         * block starts again where the region fits */
        if (!v->vbuf || v->vb_block + need <= v->vb_size) return;
        end_group(v);
        block_send(v, 0);
    }
    v->open = 1;
    v->region_used = 0;
    if (!v->vbuf || v->vb_off + need > v->vb_size) {
        struct res *r = res_create(v, VG_TARGET_BUFFER, VG_FMT_R8_UNORM, VG_BIND_VERTEX_BUFFER, need * RING, 1, 1);
        if (r) { if (v->vbuf) res_release(v, v->vbuf); v->vbuf = r; v->vb_size = need * RING; }
        v->vb_off = 0;
    }
    if (!v->ibuf || v->ib_off + IA_COUNT * 2 > v->ib_size) {
        struct res *r = res_create(v, VG_TARGET_BUFFER, VG_FMT_R8_UNORM, VG_BIND_INDEX_BUFFER, IA_COUNT * 2 * RING, 1, 1);
        if (r) { if (v->ibuf) res_release(v, v->ibuf); v->ibuf = r; v->ib_size = IA_COUNT * 2 * RING; }
        v->ib_off = 0;
    }
    v->vb_block = v->vb_off;
    v->ib_block = v->ib_off;
    v->b_stride = 0xFFFFFFFFUL;         /* the buffers' offsets moved: set again */
    v->b_iboff = 0xFFFFFFFFUL;
}

/* Where the copied vertices start in the vertex buffer, in this block. */
static ULONG va_at(struct w3dvg *v) { return v->px ? v->vb_block : v->vb_block + ((v->region_len + 255) & ~255UL); }

/* The drawing area's memory for a send that reads or writes it (a load, a present): the batch's SURFACE, or, with
 * direct calls, the platform's, locked from here until area_put. 0: not to be had now. */
static int area_get(struct w3dvg *v, ULONG *addr, ULONG *bpr)
{
    const ULONG *t = v->slot[v->target];
    if (!v->os.area_lock) { *addr = t[0]; *bpr = t[1]; return 1; }
    if (v->area_locked || !v->os.area_lock(v->os.user, addr, bpr)) return 0;
    v->area_locked = 1;
    return 1;
}

static void area_put(struct w3dvg *v)
{
    if (!v->area_locked) return;
    v->area_locked = 0;
    v->os.area_unlock(v->os.user);
}

static ULONG present_cmd(struct w3dvg *v, ULONG *p, ULONG addr, ULONG bpr)
{
    const ULONG *t = v->slot[v->target];
    ULONG a[12], fmt = t[4] == OGPU_FMT_RGB565 ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32;
    a[0] = v->ctx_id; a[1] = v->colour->handle; a[2] = 0; a[3] = 0;
    a[4] = addr; a[5] = bpr; a[6] = fmt;
    a[7] = (t[2] << 16) | (t[3] & 0xFFFF); a[8] = 0; a[9] = 1;
    a[10] = 0; a[11] = (t[2] << 16) | (t[3] & 0xFFFF);
    blk_cmd(p, ACV_PRESENT, 12, a);
    return BLK_WORDS(12);
}

static ULONG px_present_prepare(void *user, ULONG ctx, ULONG handle, ULONG *p, ULONG room)
{
    struct w3dvg *v = user;
    ULONG addr, bpr;
    (void)ctx; (void)handle;
    if (room < BLK_WORDS(12)) return 0;
    if (!area_get(v, &addr, &bpr)) return 0;    /* not visible now: the frame stays on the host */
    v->presented = 1;
    v->st.presents++;
    return present_cmd(v, p, addr, bpr);
}

static void px_present_finish(void *user, const ULONG *blk, ULONG at)
{
    struct w3dvg *v = user;
    if (blk && result(blk, at) < 0) { v->errors++; say(v, "virgl: the present failed"); }
    area_put(v);                        /* the block ran: the drawing area is the program's again */
}

static void block_send(struct w3dvg *v, int present)
{
    ULONG *b = v->blk, at = v->npre, a[14], i, submit_at = 0xFFFFFFFFUL, present_at = 0xFFFFFFFFUL;
    ULONG rl = (v->region_len + 255) & ~255UL;
    struct res *r;
    if (v->px) {
        int wanted = present && v->colour && (v->host_flags & ACV_HOST_PRESENT);
        px_commit(v);
        v->presented = 0;
        if (wanted) {
            struct OGPUPresentHook h;
            h.prepare = px_present_prepare; h.finish = px_present_finish; h.user = v;
            if (v->pipe->end_frame(v->px, &h, 0) < 0) v->lost = 1;  /* the arenas grow here when a block found them small */
        } else if (v->pipe->flush(v->px, 0) < 0) v->lost = 1;
        area_put(v);                    /* (a block that went without its hook's second call) */
        if (v->pipe->errors(v->px)) { v->errors += v->pipe->errors(v->px); say(v, "virgl: the host refused commands of a block"); }
        px_sync(v);
        /* the drawing area couldn't be had for the present: the frame is still the host's */
        if (present && !(wanted && v->os.area_lock && !v->presented)) v->have_frame = 0;
        return;
    }
    if (!v->open) return;
    if (v->region_used && v->region_len) {
        a[0] = v->vbuf->handle; a[1] = v->ctx_id; a[2] = 0; a[3] = 0; a[4] = 0;
        a[5] = v->vb_block; a[6] = 0; a[7] = 0; a[8] = v->region_len; a[9] = 1; a[10] = 1;
        a[11] = v->region; a[12] = v->region_len; a[13] = ACV_SWAP_32;
        blk_cmd(b + at, ACV_PUT, 14, a); at += BLK_WORDS(14);
        v->st.puts++; v->st.put_bytes += v->region_len;
    }
    if (v->nva) {
        a[0] = v->vbuf->handle; a[1] = v->ctx_id; a[2] = 0; a[3] = 0; a[4] = 0;
        a[5] = va_at(v); a[6] = 0; a[7] = 0; a[8] = v->nva; a[9] = 1; a[10] = 1;
        a[11] = ADDR(v->va); a[12] = v->nva; a[13] = ACV_SWAP_32;
        blk_cmd(b + at, ACV_PUT, 14, a); at += BLK_WORDS(14);
    }
    if (v->nia) {
        a[0] = v->ibuf->handle; a[1] = v->ctx_id; a[2] = 0; a[3] = 0; a[4] = 0;
        a[5] = v->ib_block; a[6] = 0; a[7] = 0; a[8] = v->nia * 2; a[9] = 1; a[10] = 1;
        a[11] = ADDR(v->ia); a[12] = v->nia * 2; a[13] = ACV_SWAP_16;
        blk_cmd(b + at, ACV_PUT, 14, a); at += BLK_WORDS(14);
    }
    if (v->ncmd) {
        a[0] = v->ctx_id; a[1] = ADDR(v->cmd); a[2] = v->ncmd;
        submit_at = at;
        blk_cmd(b + at, ACV_SUBMIT, 3, a); at += BLK_WORDS(3);
    }
    for (i = 0; i < v->npost; i++) b[at + i] = v->post[i];
    at += v->npost;
    if (present && v->colour && (v->host_flags & ACV_HOST_PRESENT)) {
        present_at = at;
        at += present_cmd(v, b + at, v->slot[v->target][0], v->slot[v->target][1]);
        v->st.presents++;
    }
    if (at) {
        if (run(v, b, at)) { say(v, "virgl: a block didn't run"); v->errors++; }
        else {
            for (i = 0; i < v->npre; i += ACV_HDR_WORDS(VG_GETW(b + i))) {
                if (!ACV_HDR_WORDS(VG_GETW(b + i))) break;
                if (result(b, i)) { v->errors++; say(v, "virgl: the host refused a command"); }
            }
            if (submit_at != 0xFFFFFFFFUL && result(b, submit_at)) { v->errors++; say(v, "virgl: the host refused the stream"); }
            if (present_at != 0xFFFFFFFFUL && result(b, present_at) < 0) { v->errors++; say(v, "virgl: the present failed"); }
        }
    }
    while ((r = v->dead) != 0) { v->dead = r->next_dead; dealloc(v, r); }
    v->vb_off = v->vb_block + (v->region_used ? rl : 0) + ((v->nva + 255) & ~255UL);
    if (!v->region_used && v->nva) v->vb_off = va_at(v) + ((v->nva + 255) & ~255UL);
    v->ib_off = v->ib_block + ((v->nia * 2 + 255) & ~255UL);
    v->npre = v->ncmd = v->nva = v->nia = v->npost = 0;
    v->open = 0;
    if (present) { v->have_frame = 0; }
}

/* ---- encoder ---------------------------------------------------------------------------------- */

static void bind(struct w3dvg *v, ULONG obj, ULONG h)
{
    vgc_bind(cmd(v, VGC_BIND_WORDS), obj, h);
}

static void destroy(struct w3dvg *v, ULONG obj, ULONG h)
{
    vgc_destroy(cmd(v, VGC_DESTROY_WORDS), obj, h);
}

static void set_framebuffer(struct w3dvg *v)
{
    vgc_framebuffer(cmd(v, VGC_FRAMEBUFFER_WORDS), v->depth ? v->surf_z : 0, v->surf_c);
}

static void set_viewport(struct w3dvg *v, ULONG w, ULONG h)
{
    /* clip -1..1 to the surface, y down (a top-down surface, as minigl.library's) */
    float v6[6];
    v6[0] = (float)w * 0.5f; v6[1] = -(float)h * 0.5f; v6[2] = 0.5f;
    v6[3] = (float)w * 0.5f; v6[4] = (float)h * 0.5f; v6[5] = 0.5f;
    if (v->px) { v->pipe->set_viewport(v->px, v6); px_sync(v); return; }
    vgc_viewport(cmd(v, VGC_VIEWPORT_WORDS), v6);
}

static void constants(struct w3dvg *v, int shader, const ULONG *k, int vec4s)
{
    vgc_constants(cmd(v, VGC_CONSTANTS_WORDS(vec4s)), shader, k, vec4s);
}

/* ---- the host's buffers ----------------------------------------------------------------------------- */

/* The colour buffer at the target's size (and its surface); 0 when it can't be. */
static int colour_for(struct w3dvg *v)
{
    const ULONG *t = v->slot[v->target];
    ULONG *p;
    if (v->colour && v->colour->w == t[2] && v->colour->h == t[3]) return 1;
    if (v->colour) { destroy(v, VG_OBJ_SURFACE, v->surf_c); res_release(v, v->colour); v->colour = 0; }
    if (v->stage) { res_release(v, v->stage); v->stage = 0; }
    if (!t[2] || !t[3]) return 0;
    v->colour = res_create(v, VG_TARGET_2D, VG_FMT_B8G8R8A8_UNORM, VG_BIND_RENDER_TARGET | VG_BIND_SAMPLER_VIEW, t[2], t[3], 1);
    if (!v->colour) return 0;
    v->surf_c = new_obj(v);
    p = cmd(v, 6);
    VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SURFACE, 5)); VG_W(p, v->surf_c); VG_W(p, v->colour->handle); VG_W(p, VG_FMT_B8G8R8A8_UNORM); VG_W(p, 0); VG_W(p, 0);
    v->have_frame = 0;
    v->fb_dirty = v->vp_dirty = v->sc_dirty = 1;
    v->vc_ok = 0;
    /* the depth buffer goes with it: made again at this size */
    if (v->depth) { destroy(v, VG_OBJ_SURFACE, v->surf_z); res_release(v, v->depth); v->depth = 0; }
    return 1;
}

/* A depth buffer of the format the bound buffers need. */
static void depth_for(struct w3dvg *v)
{
    ULONG fmt, *p;
    if (!v->z_addr && !v->s_addr) return;
    fmt = v->s_addr ? VG_FMT_D24S8 : v->z_fmt == OGPU_FMT_Z16 ? VG_FMT_Z16_UNORM : VG_FMT_Z24X8_UNORM;
    if (v->depth && (v->zfmt == fmt || (v->zfmt == VG_FMT_D24S8 && fmt != VG_FMT_Z16_UNORM))) return;
    v->have_z = 0;
    if (v->depth) { destroy(v, VG_OBJ_SURFACE, v->surf_z); res_release(v, v->depth); }
    v->depth = res_create(v, VG_TARGET_2D, fmt, VG_BIND_DEPTH_STENCIL, v->colour->w, v->colour->h, 1);
    v->zfmt = fmt;
    if (!v->depth) return;
    v->surf_z = new_obj(v);
    p = cmd(v, 6);
    VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SURFACE, 5)); VG_W(p, v->surf_z); VG_W(p, v->depth->handle); VG_W(p, fmt); VG_W(p, 0); VG_W(p, 0);
    /* cleared, as a new Warp3D Z buffer is (W3D_AllocZBuffer clears it) */
    vgc_framebuffer(cmd(v, VGC_FRAMEBUFFER_WORDS), v->surf_z, v->surf_c);
    { static const float none[4] = { 0, 0, 0, 0 };
      vgc_clear(cmd(v, VGC_CLEAR_WORDS), VG_CLEAR_DEPTH | VG_CLEAR_STENCIL, none, 1.0, 0); }
    v->fb_dirty = 1;
}

/* Memory the host must hold for a while (until the block has run). */
static ULONG *stage_mem(struct w3dvg *v, ULONG **m, ULONG *have, ULONG bytes)
{
    if (*have < bytes) {
        if (*m) v->os.free(v->os.user, *m);
        *m = v->os.alloc(v->os.user, bytes);
        *have = *m ? bytes : 0;
    }
    return *m;
}

/* A w x h picture at src (stride bytes a row) into a resource. The host
 * takes a transfer whose rows are packed (stride = w * bpp): padded rows are
 * packed into *m first. */
static void put_rows(struct w3dvg *v, struct res *r, ULONG w, ULONG h, ULONG src, ULONG stride, ULONG bpp,
                     ULONG swap, ULONG **m, ULONG *have)
{
    ULONG row = w * bpp, y, i;
    UBYTE *d;
    if (stride == row) { put(v, r, 0, 0, 0, w, h, src, row, row * h, swap); return; }
    if (!(d = (UBYTE *)stage_mem(v, m, have, row * h))) return;
    for (y = 0; y < h; y++) {
        const UBYTE *sp = (const UBYTE *)(src + y * stride);
        for (i = 0; i < row; i++) d[y * row + i] = sp[i];
    }
    put(v, r, 0, 0, 0, w, h, ADDR(d), row, row * h, swap);
}

/* The drawing area's pixels into the colour buffer (the frame's first draw). */
/* With the platform's lock held for a load, the block goes now, so the lock ends before the call returns. */
static void load_sent(struct w3dvg *v)
{
    if (!v->area_locked) return;
    end_group(v);
    px_commit(v);
    if (v->px) {
        if (v->pipe->flush(v->px, 0) < 0) v->lost = 1;
        if (v->pipe->errors(v->px)) v->errors += v->pipe->errors(v->px);
        px_sync(v);
    }
    area_put(v);
}

/* 0: the drawing area's memory isn't to be had now (nothing is loaded). */
static int load(struct w3dvg *v)
{
    const ULONG *t = v->slot[v->target];
    ULONG *p, addr, bpr;
    if (t[4] != OGPU_FMT_ARGB32 && t[4] != OGPU_FMT_RGB565) { v->have_frame = 1; v->frame_addr = t[0]; v->st.loads++; return 1; }
    if (t[4] == OGPU_FMT_RGB565 && !v->stage
        && !(v->stage = res_create(v, VG_TARGET_2D, VG_FMT_B5G6R5_UNORM, VG_BIND_SAMPLER_VIEW, t[2], t[3], 1))) {
        v->have_frame = 1; v->frame_addr = t[0]; v->st.loads++;
        return 1;
    }
    if (!area_get(v, &addr, &bpr)) return 0;
    v->have_frame = 1;
    v->frame_addr = t[0];
    v->st.loads++;
    if (t[4] == OGPU_FMT_ARGB32) {
        put_rows(v, v->colour, t[2], t[3], addr, bpr, 4, ACV_SWAP_32, &v->cstage, &v->cstage_bytes);
        load_sent(v);
        return 1;
    }
    put_rows(v, v->stage, t[2], t[3], addr, bpr, 2, ACV_SWAP_16, &v->cstage, &v->cstage_bytes);
    p = cmd(v, 22);
    VG_W(p, VG_CMD0(VG_CCMD_BLIT, 0, 21));
    VG_W(p, 0xF);                          /* RGBA, nearest, no scissor */
    VG_W(p, 0); VG_W(p, 0);
    VG_W(p, v->colour->handle); VG_W(p, 0); VG_W(p, VG_FMT_B8G8R8A8_UNORM); VG_W(p, 0); VG_W(p, 0); VG_W(p, 0); VG_W(p, t[2]); VG_W(p, t[3]); VG_W(p, 1);
    VG_W(p, v->stage->handle); VG_W(p, 0); VG_W(p, VG_FMT_B5G6R5_UNORM); VG_W(p, 0); VG_W(p, 0); VG_W(p, 0); VG_W(p, t[2]); VG_W(p, t[3]); VG_W(p, 1);
    load_sent(v);
    return 1;
}

/* Memory's Z (and stencil) into the depth buffer: the first draw or part
 * clear after the buffer was made or the CPU may have written it. Z16 alone
 * goes up as it is; otherwise the 68k packs each pixel as the host's 24/8
 * word (depth << 8 | stencil), the depth the top 24 bits of Z32 or Z16
 * widened. */
static void load_z(struct w3dvg *v)
{
    ULONG w = v->colour->w, h = v->colour->h, x, y, bytes = w * h * 4, zw = 0, zh = 0;
    v->have_z = 1;
    if (!v->depth || (!v->z_addr && !v->s_addr)) return;
    v->st.loads++;
    if (v->z_addr) { zw = v->z_w < w ? v->z_w : w; zh = v->z_h < h ? v->z_h : h; }
    if (v->zfmt == VG_FMT_Z16_UNORM) {
        /* the buffer's own size: beyond it nothing is drawn with Z */
        if (zw && zh) put_rows(v, v->depth, zw, zh, v->z_addr, v->z_bpr, 2, ACV_SWAP_16, &v->zstage, &v->zstage_bytes);
        return;
    }
    if (!stage_mem(v, &v->zstage, &v->zstage_bytes, bytes)) return;
    for (y = 0; y < h; y++) {
        const UBYTE *z = v->z_addr && y < zh ? (const UBYTE *)(v->z_addr + y * v->z_bpr) : 0;
        const UBYTE *st = v->s_addr && y < v->s_h ? (const UBYTE *)(v->s_addr + y * v->s_bpr) : 0;
        UBYTE *d = (UBYTE *)(v->zstage + y * w);
        for (x = 0; x < w; x++, d += 4) {
            ULONG zz = 0xFFFFFFUL;
            if (z && x < zw) zz = v->z_fmt == OGPU_FMT_Z16 ? ((ULONG)z[x * 2] << 16 | (ULONG)z[x * 2 + 1] << 8 | z[x * 2])
                                                 : ((ULONG)z[x * 4] << 16 | (ULONG)z[x * 4 + 1] << 8 | z[x * 4 + 2]);
            VG_A32(d, v->zfmt == VG_FMT_D24S8 ? zz << 8 | (st && x < v->s_w ? st[x] : 0) : zz);
        }
    }
    put(v, v->depth, 0, 0, 0, w, h, ADDR(v->zstage), w * 4, bytes, ACV_SWAP_32);
}

/* The depth buffer's contents known before a depth op: loaded, unless the
 * op covers all of it (a whole clear of a buffer with one part). */
static void depth_known(struct w3dvg *v, int covers_all)
{
    if (v->have_z || !v->depth) return;
    if (covers_all) v->have_z = 1;
    else load_z(v);
}

/* Before anything that draws colour: a target, and its pixels. */
static int ready(struct w3dvg *v, int fills_all)
{
    if (!colour_for(v)) return 0;
    depth_for(v);
    if (!v->have_frame || v->frame_addr != v->slot[v->target][0]) {
        if (fills_all) { v->have_frame = 1; v->frame_addr = v->slot[v->target][0]; }
        else if (!load(v)) return 0;
    }
    return 1;
}

/* ---- object caches ------------------------------------------------------------------------------- */

static ULONG cached(struct w3dvg *v, struct cache *c, ULONG obj, const ULONG *key, int n, ULONG words, ULONG **body)
{
    int i, j;
    ULONG h, *p;
    *body = 0;
    for (i = 0; i < c->n; i++) {
        for (j = 0; j < n && c->key[i][j] == key[j]; j++) ;
        if (j == n) return c->handle[i];
    }
    if (c->n < CACHE) i = c->n++;
    else { i = c->next; c->next = (c->next + 1) % CACHE; destroy(v, obj, c->handle[i]); }
    for (j = 0; j < n; j++) c->key[i][j] = key[j];
    h = new_obj(v);
    c->handle[i] = h;
    p = cmd(v, words + 1);
    VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, obj, words)); VG_W(p, h);
    *body = p;
    return h;
}

static ULONG shader(struct w3dvg *v, int type, ULONG key)
{
    ULONG *keys = type == VG_SHADER_VERTEX ? v->vs_key : v->fs_key, *hs = type == VG_SHADER_VERTEX ? v->vs_h : v->fs_h;
    int *n = type == VG_SHADER_VERTEX ? &v->nvs : &v->nfs, i;
    ULONG len, h;
    for (i = 0; i < *n; i++) if (keys[i] == key) return hs[i];
    len = type == VG_SHADER_VERTEX ? w3dvg_vs_text(key, v->text, TEXT_BYTES) : w3dvg_fs_text(key, v->text, TEXT_BYTES);
    if (v->os.log) say(v, v->text);
    h = new_obj(v);
    vgc_shader(cmd(v, VGC_SHADER_WORDS(len)), h, type, v->text, len);
    v->st.shaders++;
    if (*n == SHADERS) *n = 0;          /* the table starts again; the old variants stay on the host */
    keys[*n] = key; hs[*n] = h;
    (*n)++;
    return h;
}

/* The vertex elements for a layout (stream3d.h's OGPU_LAY_ bits) and stride. */
static ULONG elements(struct w3dvg *v, ULONG layout)
{
    ULONG key = layout & (OGPU_LAY_Z | OGPU_LAY_W | OGPU_LAY_SPEC), h, *p, off = 20;
    int i, n = 3;
    for (i = 0; i < v->nve; i++) if (v->ve_key[i] == key) return v->ve_h[i];
    if (key & OGPU_LAY_Z) n++;
    if (key & OGPU_LAY_W) n++;
    if (key & OGPU_LAY_SPEC) n++;
    h = new_obj(v);
    p = cmd(v, (ULONG)n * 4 + 2);
    VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_VERTEX_ELEMENTS, n * 4 + 1)); VG_W(p, h);
    VG_W(p, 0); VG_W(p, 0); VG_W(p, 0); VG_W(p, VG_FMT_R32G32_SSCALED);         /* x, y */
    VG_W(p, 8); VG_W(p, 0); VG_W(p, 0); VG_W(p, VG_FMT_B8G8R8A8_UNORM);               /* ARGB32 */
    VG_W(p, 12); VG_W(p, 0); VG_W(p, 0); VG_W(p, VG_FMT_R32G32_SSCALED);        /* u, v */
    if (key & OGPU_LAY_Z) { VG_W(p, off); VG_W(p, 0); VG_W(p, 0); VG_W(p, VG_FMT_R32_USCALED); off += 4; }
    if (key & OGPU_LAY_W) { VG_W(p, off); VG_W(p, 0); VG_W(p, 0); VG_W(p, VG_FMT_R32_FLOAT); off += 4; }
    if (key & OGPU_LAY_SPEC) { VG_W(p, off); VG_W(p, 0); VG_W(p, 0); VG_W(p, VG_FMT_B8G8R8A8_UNORM); }
    if (v->nve == LAYOUTS) v->nve = 0;
    v->ve_key[v->nve] = key; v->ve_h[v->nve] = h; v->nve++;
    return h;
}

/* ---- textures -------------------------------------------------------------------------------------- */

static struct tex *tex_find(struct w3dvg *v, ULONG addr)
{
    int i;
    for (i = 0; i < TEXTS; i++) if (v->tex[i].addr == addr && v->tex[i].res) return &v->tex[i];
    return 0;
}

static void tex_drop(struct w3dvg *v, struct tex *t)
{
    if (!t->res) return;
    if (v->b_view == t->view) v->b_view = 0xFFFFFFFFUL;
    destroy(v, VG_OBJ_SAMPLER_VIEW, t->view);
    res_release(v, t->res);
    t->res = 0; t->addr = 0;
}

/* The bound texture on the host, its texels up to date: its sampler view. */
static ULONG tex_view(struct w3dvg *v)
{
    struct tex *t = tex_find(v, v->t_addr);
    ULONG l, *p;
    if (t && (t->w != v->t_w || t->h != v->t_h || t->levels != v->t_levels)) { tex_drop(v, t); t = 0; }
    if (!t) {
        int i, old = 0;
        for (i = 0; i < TEXTS; i++) {
            if (!v->tex[i].res) { old = i; break; }
            if (v->tex[i].used < v->tex[old].used) old = i;
        }
        t = &v->tex[old];
        tex_drop(v, t);
        t->res = res_create(v, VG_TARGET_2D, VG_FMT_B8G8R8A8_UNORM, VG_BIND_SAMPLER_VIEW, v->t_w, v->t_h, v->t_levels);
        if (!t->res) return 0;
        t->addr = v->t_addr; t->w = v->t_w; t->h = v->t_h; t->levels = v->t_levels;
        t->gen = v->t_gen + 1;          /* not yet up */
        t->view = new_obj(v);
        p = cmd(v, 7);
        VG_W(p, VG_CMD0(VG_CCMD_CREATE_OBJECT, VG_OBJ_SAMPLER_VIEW, 6)); VG_W(p, t->view); VG_W(p, t->res->handle);
        VG_W(p, VG_FMT_B8G8R8A8_UNORM | ((v->caps_bits & 2) ? (ULONG)VG_TARGET_2D << 24 : 0));
        VG_W(p, 0); VG_W(p, (t->levels - 1) << 8); VG_W(p, 0 | 1 << 3 | 2 << 6 | 3 << 9);
    }
    if (t->gen != v->t_gen) {
        for (l = 0; l < t->levels; l++) {
            ULONG lw = v->t_w >> l, lh = v->t_h >> l, bpr = l ? 0 : v->t_bpr;
            if (!lw) lw = 1;
            if (!lh) lh = 1;
            if (!bpr) bpr = lw * 4;
            put(v, t->res, l, 0, 0, lw, lh, l ? v->t_lv[l] : v->t_addr, bpr, bpr * (lh - 1) + lw * 4, ACV_SWAP_32);
        }
        t->gen = v->t_gen;
    }
    t->used = ++v->stamp;
    return t->view;
}

static ULONG sampler(struct w3dvg *v)
{
    static const UBYTE wrap_of[4] = { 0, 0, 2, 3 };    /* REPEAT, CLAMP (to the edge), BORDER */
    ULONG min = v->t_filter & 255, mag = (v->t_filter >> 8) & 255, key[4], s0, *p, h, i, c[4];
    ULONG minf = min == 1 || min == 3 || min == 4 ? 0 : 1;
    ULONG mipf = v->t_levels <= 1 || min <= 2 ? 2 : min == 3 || min == 5 ? 0 : 1;
    s0 = wrap_of[(v->t_wrap & 255) & 3] | (ULONG)wrap_of[((v->t_wrap >> 8) & 255) & 3] << 3 | 2UL << 6
       | minf << 9 | mipf << 11 | (mag == 1 ? 0UL : 1UL) << 13;
    key[0] = s0; key[1] = v->t_levels; key[2] = v->t_border; key[3] = 0;
    if (v->px) {
        struct OGPUSamplerKey2 k;
        k.s0 = s0; k.levels = v->t_levels;
        k.border[0] = fbits((float)((v->t_border >> 16) & 255) / 255.0f);
        k.border[1] = fbits((float)((v->t_border >> 8) & 255) / 255.0f);
        k.border[2] = fbits((float)(v->t_border & 255) / 255.0f);
        k.border[3] = fbits((float)(v->t_border >> 24) / 255.0f);
        h = v->pipe->cso_handle(v->pipe->sampler_create2(v->px, &k));
        px_sync(v);
        return h;
    }
    h = cached(v, &v->c_samp, VG_OBJ_SAMPLER_STATE, key, 3, 9, &p);
    if (p) {
        c[0] = (v->t_border >> 16) & 255; c[1] = (v->t_border >> 8) & 255; c[2] = v->t_border & 255; c[3] = v->t_border >> 24;
        VG_W(p, s0); VG_W(p, fbits(0.0f)); VG_W(p, fbits(0.0f)); VG_W(p, fbits((float)(v->t_levels - 1)));
        for (i = 0; i < 4; i++) VG_W(p, fbits((float)c[i] / 255.0f));
    }
    return h;
}

/* ---- a draw's state --------------------------------------------------------------------------------- */

static int has_dst_alpha(struct w3dvg *v) { return v->slot[v->target][4] == OGPU_FMT_ARGB32; }

/* Without alpha in the target the core reads it as 255. */
static ULONG factor(struct w3dvg *v, ULONG f, int alpha_part)
{
    if (f > 15) f = 2;
    if (!has_dst_alpha(v)) {
        if (f == 9) f = 2;                          /* DST_ALPHA: one */
        else if (f == 10) f = 1;                    /* ONE_MINUS_DST_ALPHA: zero */
        else if (f == 11) f = alpha_part ? 2 : 1;   /* SRC_ALPHA_SATURATE: min(As, 0) */
    }
    return factor_of[f];
}

/* The pipeline the stream's state asks for. */
static void pipe_of(struct w3dvg *v, struct pipe *pp, ULONG layout, int textured)
{
    const ULONG *a = v->r3d;
    ULONG en = a[0], rt, cm = a[5] & 15, s0 = 0, fk = 0, vk = 0;
    int ztest = (en & OGPU_R3D_ZTEST) && v->z_addr && v->depth, stencil = (en & OGPU_R3D_STENCIL) && v->s_addr && v->depth;
    /* blend, or a logic op */
    rt = 0;
    if ((en & OGPU_R3D_BLEND) && !(en & OGPU_R3D_LOGICOP)) {
        ULONG bs = a[3] & 0xFFFF, bd = a[3] >> 16;
        rt = 1 | factor(v, bs, 0) << 4 | factor(v, bd, 0) << 9 | factor(v, bs, 1) << 17 | factor(v, bd, 1) << 22;
    }
    rt |= cm << 27;
    pp->blend[0] = rt;
    pp->blend[1] = (en & OGPU_R3D_LOGICOP) && a[6] >= 1 && a[6] <= 16 ? 0x100 | logic_of[a[6]] : 0;
    /* depth, alpha, stencil */
    if (ztest) s0 = 1 | ((en & OGPU_R3D_ZWRITE) ? 2 : 0) | (ULONG)cmp_of[a[1] & 7 ? (a[1] > 8 ? 8 : a[1]) : 8] << 2;
    if (en & OGPU_R3D_ALPHATEST) s0 |= 1UL << 8 | (ULONG)cmp_of[(a[2] & 0xFFFF) > 8 ? 8 : (a[2] & 0xFFFF)] << 9;
    pp->dsa[0] = s0;
    pp->dsa[1] = 0;
    if (stencil) {
        ULONG sf = a[12] & 255, sm = (a[12] >> 16) & 255, w = a[13];
        pp->dsa[1] = 1 | (ULONG)scmp_of[sf > 8 ? 8 : sf] << 1 | (ULONG)sop_of[(w & 255) > 8 ? 1 : w & 255] << 4
                   | (ULONG)sop_of[((w >> 16) & 255) > 8 ? 1 : (w >> 16) & 255] << 7
                   | (ULONG)sop_of[((w >> 8) & 255) > 8 ? 1 : (w >> 8) & 255] << 10
                   | sm << 13 | ((w >> 24) & 255) << 21;
    }
    pp->dsa[2] = (en & OGPU_R3D_ALPHATEST) ? fbits((float)((a[2] >> 16) & 255) / 255.0f) : 0;
    pp->dsa[3] = 0;
    pp->sref = stencil ? (a[12] >> 8) & 255 : 0;
    pp->bconst = a[4];
    /* the rasterizer: scissor always (the clip), stipple, pixel centres at .5 */
    pp->stipple = (en & OGPU_R3D_STIPPLE) && a[19] ? a[19] : 0;
    pp->rs = 1UL << 14 | 1UL << 29 | 1UL << 30 | (pp->stipple ? 1UL << 22 : 0);
    /* shaders */
    if (layout & OGPU_LAY_Z) vk |= VK_Z;
    if (layout & OGPU_LAY_W) vk |= VK_W;
    if (layout & OGPU_LAY_SPEC) vk |= VK_SPEC;
    if ((en & OGPU_R3D_PERSPECTIVE) && (layout & OGPU_LAY_W)) vk |= VK_PERSP;
    vk |= VK_LINEAR; fk |= FK_LINEAR;   /* GLES too: the shaders do it without noperspective */
    if (en & OGPU_R3D_GOURAUD) fk |= FK_GOURAUD;
    if (textured) {
        fk |= FK_ENV_SET(v->env) | FK_BASE_SET((v->t_fmt >> 8) & 7);
        if ((en & OGPU_R3D_CHROMA) && a[14] > 1) fk |= FK_CHROMA_SET(a[14] & 3);
    }
    if ((en & OGPU_R3D_SPECULAR) && (layout & OGPU_LAY_SPEC)) fk |= FK_SPEC;
    if ((en & OGPU_R3D_FOG) && a[7] >= OGPU_FOG_LINEAR && a[7] <= OGPU_FOG_VERTEX) {
        ULONG f = a[7];
        if (f == OGPU_FOG_VERTEX ? (layout & OGPU_LAY_SPEC) : (layout & OGPU_LAY_W)) {
            fk |= FK_FOG_SET(f);
            if (f == OGPU_FOG_LINEAR && a[9] == a[10]) fk |= FK_FOGEQ;
        }
    }
    pp->vs = vk; pp->fs = fk;
}

static void set_scissor(struct w3dvg *v)
{
    const ULONG *t = v->slot[v->target];
    LONG x0 = 0, y0 = 0, x1 = (LONG)t[2], y1 = (LONG)t[3];
    if (v->cw > 0 && v->ch > 0) {
        if (v->cx > x0) x0 = v->cx;
        if (v->cy > y0) y0 = v->cy;
        if (v->cx + v->cw < x1) x1 = v->cx + v->cw;
        if (v->cy + v->ch < y1) y1 = v->cy + v->ch;
    }
    /* the core's drawing area: a triangle stops at the depth and stencil
     * buffers it uses */
    if (v->tri) {
        if (v->z_addr && (v->r3d[0] & OGPU_R3D_ZTEST)) { if ((LONG)v->z_w < x1) x1 = (LONG)v->z_w; if ((LONG)v->z_h < y1) y1 = (LONG)v->z_h; }
        if (v->s_addr && (v->r3d[0] & OGPU_R3D_STENCIL)) { if ((LONG)v->s_w < x1) x1 = (LONG)v->s_w; if ((LONG)v->s_h < y1) y1 = (LONG)v->s_h; }
    }
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    if (v->px) {
        struct OGPUBox b;
        b.x = x0; b.y = y0; b.z = 0; b.w = (ULONG)(x1 - x0); b.h = (ULONG)(y1 - y0); b.d = 1;
        v->pipe->set_scissor(v->px, &b);        /* sent only when it changed */
        px_sync(v);
        return;
    }
    if (!v->sc_dirty && x0 == v->sc[0] && y0 == v->sc[1] && x1 == v->sc[2] && y1 == v->sc[3]) return;
    vgc_scissor(cmd(v, VGC_SCISSOR_WORDS), x0, y0, x1, y1);
    v->sc[0] = x0; v->sc[1] = y0; v->sc[2] = x1; v->sc[3] = y1;
    v->sc_dirty = 0;
}

static void send_if(struct w3dvg *v, int shader_type, ULONG *last, int *ok, const ULONG *k, int n)
{
    int i;
    if (v->px) {
        v->pipe->set_constants(v->px, shader_type == VG_SHADER_VERTEX ? OGPU_STAGE_VERTEX : OGPU_STAGE_FRAGMENT,
                               (const float *)k, (ULONG)n);   /* sent only when they changed */
        px_sync(v);
        return;
    }
    if (*ok) {
        for (i = 0; i < n * 4; i++) if (last[i] != k[i]) break;
        if (i == n * 4) return;
    }
    for (i = 0; i < n * 4; i++) last[i] = k[i];
    *ok = 1;
    constants(v, shader_type, k, n);
}

static ULONG unit(ULONG argb, int sh) { return fbits((float)((argb >> sh) & 255) / 255.0f); }
static void colour4(ULONG *d, ULONG argb) { d[0] = unit(argb, 16); d[1] = unit(argb, 8); d[2] = unit(argb, 0); d[3] = unit(argb, 24); }

/* Everything the draw needs, bound. */
static void prepare(struct w3dvg *v, const struct pipe *pp, ULONG layout, ULONG view, ULONG samp)
{
    const ULONG *t = v->slot[v->target];
    ULONG key[4], h, *p, k[FC_N * 4];
    int i;
    if (v->fb_dirty) { set_framebuffer(v); v->fb_dirty = 0; }
    if (v->vp_dirty) { set_viewport(v, t[2], t[3]); v->vp_dirty = 0; }
    set_scissor(v);
    if (v->px) {
        /* the pipe's state objects, made by key and bound only when they change (stage M6) */
        struct OGPUBlendKey2 bk;
        struct OGPUDsaKey2 dk;
        bk.rt0 = pp->blend[0]; bk.logic = pp->blend[1];
        v->pipe->bind_blend(v->px, v->pipe->blend_create2(v->px, &bk));
        dk.s0 = pp->dsa[0]; dk.alpha_ref = pp->dsa[2]; dk.stencil = pp->dsa[1];
        v->pipe->bind_dsa(v->px, v->pipe->dsa_create2(v->px, &dk));
        px_sync(v);
    } else {
    key[0] = pp->blend[0]; key[1] = pp->blend[1];
    h = cached(v, &v->c_blend, VG_OBJ_BLEND, key, 2, 11, &p);
    if (p) {
        VG_W(p, pp->blend[1] ? 2 : 0);     /* S0: the logic op on */
        VG_W(p, pp->blend[1] & 15);        /* S1: which */
        VG_W(p, pp->blend[0]);
        for (i = 1; i < 8; i++) VG_W(p, 0);
    }
    if (h != v->b_blend) { bind(v, VG_OBJ_BLEND, h); v->b_blend = h; }
    h = cached(v, &v->c_dsa, VG_OBJ_DSA, pp->dsa, 3, 5, &p);
    if (p) { VG_W(p, pp->dsa[0]); VG_W(p, pp->dsa[1]); VG_W(p, pp->dsa[1]); VG_W(p, pp->dsa[2]); }
    if (h != v->b_dsa) { bind(v, VG_OBJ_DSA, h); v->b_dsa = h; }
    }
    if ((pp->dsa[1] & 1) && pp->sref != v->b_sref) {
        p = cmd(v, 2); VG_W(p, VG_CMD0(VG_CCMD_SET_STENCIL_REF, 0, 1)); VG_W(p, pp->sref | pp->sref << 8); v->b_sref = pp->sref;
    }
    if ((pp->blend[0] & 1) && pp->bconst != v->b_bconst) {
        ULONG c[4];
        colour4(c, pp->bconst);
        p = cmd(v, 5); VG_W(p, VG_CMD0(VG_CCMD_SET_BLEND_COLOR, 0, 4)); VG_W(p, c[0]); VG_W(p, c[1]); VG_W(p, c[2]); VG_W(p, c[3]);
        v->b_bconst = pp->bconst;
    }
    if (pp->stipple && pp->stipple != v->b_stipple) {
        const UBYTE *s = (const UBYTE *)pp->stipple;
        /* rows top first, the first byte's high bit leftmost (ogpu_3d.c) */
        p = cmd(v, 33); VG_W(p, VG_CMD0(VG_CCMD_SET_POLYGON_STIPPLE, 0, 32));
        for (i = 0; i < 32; i++) VG_W(p, rd32(s + i * 4));
        v->b_stipple = pp->stipple;
    }
    if (v->px) {
        struct OGPURasterKey rk;
        struct OGPUFFKey fk;
        struct OGPULayout lay;
        ULONG lk = layout & (OGPU_LAY_Z | OGPU_LAY_W | OGPU_LAY_SPEC), off = 20;
        rk.s0 = pp->rs; rk.point_size = fbits(1.0f); rk.line_width = fbits(1.0f); rk.offset_units = rk.offset_scale = 0;
        v->pipe->bind_raster(v->px, v->pipe->raster_create(v->px, &rk));
        /* the one fixed-function generator (stage M5): Warp3D's keys as the pipe's */
        w3dvg_ff_key(pp->vs, pp->fs, &fk);
        v->pipe->bind_shader(v->px, v->pipe->ff_shader(v->px, &fk));
        /* the vertex's fields, as elements() made them */
        lay.key = lk; lay.n = 3;
        lay.e[0].offset = 0; lay.e[0].format = VG_FMT_R32G32_SSCALED;     /* x, y */
        lay.e[1].offset = 8; lay.e[1].format = VG_FMT_B8G8R8A8_UNORM;     /* ARGB32 */
        lay.e[2].offset = 12; lay.e[2].format = VG_FMT_R32G32_SSCALED;    /* u, v */
        if (lk & OGPU_LAY_Z) { lay.e[lay.n].offset = off; lay.e[lay.n++].format = VG_FMT_R32_USCALED; off += 4; }
        if (lk & OGPU_LAY_W) { lay.e[lay.n].offset = off; lay.e[lay.n++].format = VG_FMT_R32_FLOAT; off += 4; }
        if (lk & OGPU_LAY_SPEC) { lay.e[lay.n].offset = off; lay.e[lay.n++].format = VG_FMT_B8G8R8A8_UNORM; }
        v->pipe->bind_layout(v->px, v->pipe->layout_create(v->px, &lay));
        px_sync(v);
    } else {
    key[0] = pp->rs;
    h = cached(v, &v->c_rs, VG_OBJ_RASTERIZER, key, 1, 9, &p);
    if (p) { VG_W(p, pp->rs); VG_W(p, fbits(1.0f)); VG_W(p, 0); VG_W(p, 0); VG_W(p, fbits(1.0f)); VG_W(p, 0); VG_W(p, 0); VG_W(p, 0); }
    if (h != v->b_rs) { bind(v, VG_OBJ_RASTERIZER, h); v->b_rs = h; }
    h = shader(v, VG_SHADER_VERTEX, pp->vs);
    if (h != v->b_vs) { vgc_bind_shader(cmd(v, VGC_BIND_SHADER_WORDS), h, VG_SHADER_VERTEX); v->b_vs = h; }
    h = shader(v, VG_SHADER_FRAGMENT, pp->fs);
    if (h != v->b_fs) { vgc_bind_shader(cmd(v, VGC_BIND_SHADER_WORDS), h, VG_SHADER_FRAGMENT); v->b_fs = h; }
    h = elements(v, layout);
    if (h != v->b_ve) { bind(v, VG_OBJ_VERTEX_ELEMENTS, h); v->b_ve = h; }
    }
    if (view && view != v->b_view) {
        vgc_sampler_views(cmd(v, VGC_SAMPLERS_WORDS(1)), 1, &view); v->b_view = view;
    }
    if (samp && samp != v->b_samp) {
        vgc_sampler_states(cmd(v, VGC_SAMPLERS_WORDS(1)), 1, &samp); v->b_samp = samp;
    }
    /* constants */
    {
        ULONG c[VC_N * 4];
        c[0] = fbits(2.0f / ((float)t[2] * 65536.0f)); c[1] = fbits(-2.0f / ((float)t[3] * 65536.0f));
        c[2] = fbits(-1.0f); c[3] = fbits(1.0f);
        c[4] = fbits(1.0f / 65536.0f); c[5] = fbits(1.0f / 2147483648.0f); c[6] = fbits(-1.0f); c[7] = fbits(1.0f);
        c[8] = fbits(0.0f); c[9] = fbits(1.0f); c[10] = fbits(0.0f); c[11] = fbits(0.0f);
        send_if(v, VG_SHADER_VERTEX, v->vc, &v->vc_ok, c, VC_N);
    }
    {
        const ULONG *a = v->r3d;
        union { ULONG u; float f; } st, en, de;
        ULONG lo = a[15], hi = a[16];
        st.u = a[9]; en.u = a[10]; de.u = a[11];
        colour4(k + FC_CUR * 4, a[17]);
        colour4(k + FC_ENV * 4, v->envc);
        colour4(k + FC_FOGCOL * 4, a[8]);
        k[FC_FOG * 4] = a[9]; k[FC_FOG * 4 + 1] = a[10]; k[FC_FOG * 4 + 2] = a[11];
        k[FC_FOG * 4 + 3] = fbits(st.f != en.f ? 1.0f / (st.f - en.f) : 0.0f);
        (void)de;
        k[FC_CLO * 4] = fbits(((float)((lo >> 16) & 255) - 0.5f) / 255.0f);
        k[FC_CLO * 4 + 1] = fbits(((float)((lo >> 8) & 255) - 0.5f) / 255.0f);
        k[FC_CLO * 4 + 2] = fbits(((float)(lo & 255) - 0.5f) / 255.0f);
        k[FC_CLO * 4 + 3] = 0;
        k[FC_CHI * 4] = fbits(((float)((hi >> 16) & 255) + 0.5f) / 255.0f);
        k[FC_CHI * 4 + 1] = fbits(((float)((hi >> 8) & 255) + 0.5f) / 255.0f);
        k[FC_CHI * 4 + 2] = fbits(((float)(hi & 255) + 0.5f) / 255.0f);
        k[FC_CHI * 4 + 3] = 0;
        k[FC_CONST * 4] = fbits(0.0f); k[FC_CONST * 4 + 1] = fbits(1.0f); k[FC_CONST * 4 + 2] = fbits(0.5f);
        k[FC_CONST * 4 + 3] = fbits(1.44269504f);
        send_if(v, VG_SHADER_FRAGMENT, v->fc, &v->fc_ok, k, FC_N);
    }
}

/* ---- draws ------------------------------------------------------------------------------------------ */

static void end_group(struct w3dvg *v)
{
    ULONG n;
    if (!v->g_on) return;
    v->g_on = 0;
    n = v->nia - v->g_ifirst;
    if (v->g_hi) {
        /* direct calls: the part of the batch's vertex memory this run reads goes up now, before its draw */
        ULONG lo = v->g_lo, hi = v->g_hi;
        v->g_lo = v->g_hi = 0;
        if (n) {
            v->pipe->buf_put(v->px, v->rbuf->pr, v->rb_at + lo, (const void *)(v->region + lo), hi - lo, ACV_SWAP_32);
            v->st.puts++; v->st.put_bytes += hi - lo;
            if (hi > v->region_top) v->region_top = hi;
            if (v->pipe->sent(v->px) != v->seq) {   /* the PUT filled the block: the run's indices went with it */
                say(v, "virgl: a run of draws was cut by a block (dropped)");
                v->declined++;
                px_sync(v);
                return;
            }
        }
    }
    if (!n) return;
    if (v->b_iboff != v->ib_block + v->g_ifirst * 2) {
        vgc_index_buffer(cmd(v, VGC_INDEX_BUFFER_WORDS), v->px ? v->ibuf_h : v->ibuf->handle, 2, v->ib_block + v->g_ifirst * 2);
        v->b_iboff = v->ib_block + v->g_ifirst * 2;
    }
    vgc_draw(cmd(v, VGC_DRAW_WORDS), VG_PRIM_TRIANGLES, 0, n, 1, 0);
    v->st.draws++;
    v->st.triangles += n / 3;
}

/* Room for a draw of n vertices and m indices: the block goes first when not. */
static void need(struct w3dvg *v, ULONG vbytes, ULONG indices)
{
    if (v->px) {
        /* the pipe sends only between runs: the run is drawn first, then the block goes */
        if (v->pipe->room(v->px, vbytes, indices, DRAW_ROOM, 512)) return;
        end_group(v);
        px_commit(v);
        if (v->pipe->flush(v->px, 0) < 0) v->lost = 1;
        if (v->pipe->errors(v->px)) v->errors += v->pipe->errors(v->px);
        v->pipe->need(v->px, vbytes, indices, DRAW_ROOM);   /* an arena too small for the draw grows */
        px_sync(v);
        px_region_room(v);
        return;
    }
    if (v->nva + vbytes <= VA_BYTES && v->nia + indices <= IA_COUNT && v->ncmd + DRAW_ROOM <= CMD_WORDS
        && v->npre + 512 <= PRE_WORDS)
        return;
    end_group(v);
    block_send(v, 0);
    block_begin(v);
}

/* The vertex index of byte offset `off` in the vertex buffer within the
 * current run (a new run when it can't be, or the state differs). */
static int group_for(struct w3dvg *v, ULONG buf, ULONG off, ULONG layout, ULONG stride, ULONG tex, ULONG count, ULONG *first)
{
    ULONG rel;
    if (v->g_on && v->g_buf == buf && v->g_layout == layout && v->g_stride == stride && v->g_tex == tex && off >= v->g_base
        && !((off - v->g_base) % stride) && (rel = (off - v->g_base) / stride) + count <= 0xFFFF) {
        *first = rel;
        return 0;
    }
    end_group(v);
    v->g_on = 1;
    v->g_layout = layout; v->g_stride = stride; v->g_base = off; v->g_tex = tex; v->g_buf = buf;
    v->g_ifirst = v->nia;
    *first = 0;
    return 1;                           /* a new run: its state is bound now */
}

static void bind_vbuf(struct w3dvg *v)
{
    if (v->b_stride == v->g_stride && v->b_vboff == v->g_base && v->b_vbuf == v->g_buf) return;
    vgc_vertex_buffer(cmd(v, VGC_VERTEX_BUFFER_WORDS), v->g_stride, v->g_base, v->g_buf);
    v->b_stride = v->g_stride; v->b_vboff = v->g_base; v->b_vbuf = v->g_buf;
}

static int vsize_of(ULONG layout)
{
    int n = 20;
    if (layout & OGPU_LAY_Z) n += 4;
    if (layout & OGPU_LAY_W) n += 4;
    if (layout & OGPU_LAY_SPEC) n += 4;
    if (layout & OGPU_LAY_UV1) n += 8;
    return n;
}

/* Indices of one primitive (list, strip, fan of n vertices from `first`),
 * or of an index list, into the arena. */
static void indices(struct w3dvg *v, int prim, ULONG first, ULONG n, const UBYTE *idx, ULONG isize, ULONG *map_base)
{
    ULONG i, a, b, c;
    UWORD *q = v->ia + v->nia;
    (void)map_base;
#define IX(k) (first + (idx ? (isize == 1 ? (ULONG)idx[k] : isize == 2 ? ((ULONG)idx[(k) * 2] << 8 | idx[(k) * 2 + 1]) : rd32(idx + (k) * 4)) : (k)))
    if (prim == OGPU_LAY_LIST) {
        for (i = 0; i + 2 < n; i += 3) { VG_A16(q, IX(i)); VG_A16(q + 1, IX(i + 1)); VG_A16(q + 2, IX(i + 2)); q += 3; }
    } else if (prim == OGPU_LAY_STRIP) {
        for (i = 2; i < n; i++) {
            a = IX(i - 2); b = IX(i - 1); c = IX(i);
            VG_A16(q, a); VG_A16(q + 1, b); VG_A16(q + 2, c); q += 3;
        }
    } else {
        ULONG f0 = IX(0);
        for (i = 2; i < n; i++) { VG_A16(q, f0); VG_A16(q + 1, IX(i - 1)); VG_A16(q + 2, IX(i)); q += 3; }
    }
#undef IX
    v->nia = (ULONG)(q - v->ia);
    px_commit(v);
}

/* TRIANGLES, the 3D form. */
static LONG triangles(struct w3dvg *v, ULONG vaddr, ULONG vcount, ULONG stride, ULONG iaddr, ULONG icount, ULONG isize,
                      ULONG texw, ULONG layout)
{
    int prim = (int)(layout & OGPU_LAY_PRIM_MASK), textured;
    ULONG n = iaddr ? icount : vcount, tris, off, first, bytes, view = 0, samp = 0, maxv, buf, rlo = 0, rhi = 0;
    struct pipe pp;
    if (!vcount || n < 3) return 0;
    if (prim > OGPU_LAY_FAN || stride < (ULONG)vsize_of(layout) || (iaddr && isize != 1 && isize != 2 && isize != 4)) return -1;
    if (texw < 16) return -1;           /* a SURFACE slot as texture: not Warp3D's */
    if (iaddr) {
        /* as the core: an index past the vertices ends the command there,
         * the triangles before it drawn */
        const UBYTE *ip = (const UBYTE *)iaddr;
        ULONG i, vi;
        for (i = 0; i < n; i++) {
            vi = isize == 1 ? ip[i] : isize == 2 ? ((ULONG)ip[i * 2] << 8 | ip[i * 2 + 1]) : rd32(ip + i * 4);
            if (vi >= vcount) break;
        }
        n = i;
        if (n < 3) return 0;
    }
    if (!ready(v, 0)) return -1;
    if ((v->r3d[0] & (OGPU_R3D_ZTEST | OGPU_R3D_STENCIL))) depth_known(v, 0);
    tris = prim == OGPU_LAY_LIST ? n / 3 : n - 2;
    maxv = vcount;
    if (maxv > 0xFFFF) return -1;
    bytes = stride * (vcount - 1) + (ULONG)vsize_of(layout);
    textured = (v->r3d[0] & OGPU_R3D_TEXTURE) && texw == OGPU_TRI_UNIT0 && v->t_addr && v->env != OGPU_ENV_OFF && v->env;
    if (textured && (v->t_fmt & 255) != OGPU_FMT_ARGB32) return -1;
    need(v, ((bytes + 3) & ~3UL) + 64, tris * 3);
    /* where the vertices are: the batch's vertex memory, or copied */
    buf = v->px ? v->vbuf_h : v->vbuf->handle;
    if (((stride | vaddr) & 3) == 0 && v->region_len && vaddr >= v->region && vaddr + bytes <= v->region + v->region_len
        && (!v->px || v->rbuf)) {
        if (v->px && v->grow) {
            /* direct calls: the run PUTs its part when it ends (end_group), at the batch's place in the ring */
            rlo = vaddr - v->region;
            rhi = rlo + ((bytes + 3) & ~3UL);
            off = v->rb_at + rlo;
            buf = v->rbuf->handle;
        } else if (v->px) {
            /* the batch's vertex memory, PUT from where it is once a block, into the ring's next place */
            if (v->rb_seq != v->seq + 1) {
                px_region_room(v);
                v->pipe->buf_put(v->px, v->rbuf->pr, v->rb_off, (const void *)v->region, v->region_len, ACV_SWAP_32);
                v->st.puts++; v->st.put_bytes += v->region_len;
                v->rb_at = v->rb_off;
                v->rb_seq = v->seq + 1;
            }
            off = v->rb_at + (vaddr - v->region);
            buf = v->rbuf->handle;
        } else {
            v->region_used = 1;
            off = v->vb_block + (vaddr - v->region);
        }
    } else if (((stride | vaddr) & 3) == 0) {
        const UBYTE *s = (const UBYTE *)vaddr;
        UBYTE *d = v->va + v->nva;
        ULONG i;
        if (bytes > VA_BYTES) return -1;
        for (i = 0; i < bytes; i++) d[i] = s[i];
        off = va_at(v) + v->nva;
        v->nva += (bytes + 255) & ~255UL;
        px_commit(v);
    } else {
        /* The host turns the vertices round a 32-bit word at a time, so each
         * field must start on a word: an odd stride or address is packed at
         * the layout's own size first. */
        const UBYTE *s = (const UBYTE *)vaddr;
        ULONG vs = (ULONG)vsize_of(layout), i, k;
        UBYTE *d = v->va + v->nva;
        if (vs * vcount > VA_BYTES) return -1;
        for (i = 0; i < vcount; i++)
            for (k = 0; k < vs; k++) d[i * vs + k] = s[i * stride + k];
        off = va_at(v) + v->nva;
        v->nva += (vs * vcount + 255) & ~255UL;
        px_commit(v);
        stride = vs;
    }
    if (textured) { view = tex_view(v); samp = sampler(v); if (!view) return -1; }
    pipe_of(v, &pp, layout, textured);
    if (group_for(v, buf, off, layout, stride, textured ? v->t_addr : 0, maxv, &first)) {
        v->tri = 1;
        prepare(v, &pp, layout, view, samp);
        v->tri = 0;
        bind_vbuf(v);
        /* the state may have sent the block (a full stream): the run starts again */
        if (!v->g_on) { group_for(v, buf, off, layout, stride, textured ? v->t_addr : 0, maxv, &first); bind_vbuf(v); }
    } else v->st.merged++;
    if (rhi && v->g_on) {
        if (!v->g_hi || rlo < v->g_lo) v->g_lo = rlo;
        if (rhi > v->g_hi) v->g_hi = rhi;
    }
    indices(v, prim, first, n, iaddr ? (const UBYTE *)iaddr : 0, isize, 0);
    return 0;
}

/* A quad over the rectangle with a pipeline of its own: a fill (colour
 * argb) or a depth or stencil clear (value), inside the clip. */
static void quad(struct w3dvg *v, LONG x, LONG y, LONG w, LONG h, ULONG argb, ULONG z, int kind, ULONG sval)
{
    struct pipe pp;
    UBYTE *d;
    ULONG off, first, i, qbuf;
    static const UBYTE cx[4] = { 0, 1, 1, 0 }, cy[4] = { 0, 0, 1, 1 };
    need(v, 4 * 32 + 256, 6);
    end_group(v);
    qbuf = v->px ? v->vbuf_h : v->vbuf->handle;
    d = v->va + v->nva;
    off = va_at(v) + v->nva;
    for (i = 0; i < 4; i++, d += 32) {
        VG_A32(d, (ULONG)((x + (cx[i] ? w : 0)) << 16)); VG_A32(d + 4, (ULONG)((y + (cy[i] ? h : 0)) << 16));
        VG_A32(d + 8, argb); VG_A32(d + 12, 0); VG_A32(d + 16, 0); VG_A32(d + 20, z); VG_A32(d + 24, fbits(1.0f)); VG_A32(d + 28, 0xFF000000UL);
    }
    v->nva += 256;
    px_commit(v);
    pp.blend[0] = kind == 0 ? 15UL << 27 : 0;
    pp.blend[1] = 0;
    pp.dsa[0] = kind == 1 ? 1 | 2 | 7UL << 2 : 0;
    pp.dsa[1] = kind == 2 ? 1 | 7UL << 1 | 2UL << 4 | 2UL << 7 | 2UL << 10 | 255UL << 13 | 255UL << 21 : 0;
    pp.dsa[2] = pp.dsa[3] = 0;
    pp.sref = sval & 255;
    pp.bconst = 0;
    pp.stipple = 0;
    pp.rs = 1UL << 14 | 1UL << 29 | 1UL << 30;
    pp.vs = VK_Z | VK_W | VK_SPEC;
    pp.fs = FK_GOURAUD | FK_LINEAR;
    pp.vs |= VK_LINEAR;
    group_for(v, qbuf, off, OGPU_LAY_Z | OGPU_LAY_W | OGPU_LAY_SPEC, 32, 0xFFFFFFFFUL, 4, &first);
    prepare(v, &pp, OGPU_LAY_Z | OGPU_LAY_W | OGPU_LAY_SPEC, 0, 0);
    if (!v->g_on) group_for(v, qbuf, off, OGPU_LAY_Z | OGPU_LAY_W | OGPU_LAY_SPEC, 32, 0xFFFFFFFFUL, 4, &first);
    bind_vbuf(v);
    indices(v, OGPU_LAY_FAN, 0, 4, 0, 0, 0);
    end_group(v);
}

/* The rectangle inside the target and the clip; 0 when nothing is left. */
static int clipped(struct w3dvg *v, LONG *x, LONG *y, LONG *w, LONG *h)
{
    const ULONG *t = v->slot[v->target];
    LONG x0 = *x, y0 = *y, x1 = *x + *w, y1 = *y + *h, cx0 = 0, cy0 = 0, cx1 = (LONG)t[2], cy1 = (LONG)t[3];
    if (v->cw > 0 && v->ch > 0) {
        if (v->cx > cx0) cx0 = v->cx;
        if (v->cy > cy0) cy0 = v->cy;
        if (v->cx + v->cw < cx1) cx1 = v->cx + v->cw;
        if (v->cy + v->ch < cy1) cy1 = v->cy + v->ch;
    }
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
    return x1 > x0 && y1 > y0;
}

static void clear(struct w3dvg *v, ULONG buffers, ULONG argb, double depth, ULONG stencil)
{
    union { ULONG u[4]; float f[4]; } c;
    end_group(v);
    if (v->fb_dirty) { set_framebuffer(v); v->fb_dirty = 0; }
    colour4(c.u, argb);
    vgc_clear(cmd(v, VGC_CLEAR_WORDS), buffers, c.f, depth, stencil);
}

static int whole(struct w3dvg *v, LONG x, LONG y, LONG w, LONG h)
{
    const ULONG *t = v->slot[v->target];
    return x == 0 && y == 0 && w == (LONG)t[2] && h == (LONG)t[3];
}

static LONG fill(struct w3dvg *v, LONG x, LONG y, LONG w, LONG h, ULONG raw)
{
    ULONG argb;
    int fmt = (int)v->slot[v->target][4];
    if (!clipped(v, &x, &y, &w, &h)) return 0;
    if (fmt == OGPU_FMT_RGB565) {
        ULONG r = (raw >> 11) & 31, g = (raw >> 5) & 63, b = raw & 31;
        argb = 0xFF000000UL | (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
    } else if (fmt == OGPU_FMT_ARGB32) argb = raw;
    else return -1;
    if (!ready(v, whole(v, x, y, w, h))) return -1;
    need(v, 512, 6);
    /* a drawn quad, also for the whole area: a virgl CLEAR of the colour
     * buffer didn't reach it on the lab's GPU host (W3DCheck's
     * ClearDrawRegion pixel and W3DTest's background, 10 November), while
     * llvmpipe took it */
    quad(v, x, y, w, h, argb, 0, 0, 0);
    return 0;
}

static LONG clear3d(struct w3dvg *v, ULONG kind, LONG x, LONG y, LONG w, LONG h, ULONG val)
{
    if (kind & OGPU_KIND_READBACK) return 0;    /* the frame reaches memory at the present */
    if (kind > OGPU_KIND_STENCIL) return -1;
    if (!colour_for(v)) return -1;
    depth_for(v);
    if (!v->depth || !clipped(v, &x, &y, &w, &h)) return 0;
    if (kind == OGPU_KIND_STENCIL && v->zfmt != VG_FMT_D24S8) return 0;
    need(v, 512, 6);
    /* one part of a 24/8 buffer: the other part must be there first */
    depth_known(v, whole(v, x, y, w, h) && (v->zfmt != VG_FMT_D24S8 || (kind == OGPU_KIND_DEPTH ? !v->s_addr : !v->z_addr)));
    if (whole(v, x, y, w, h)) {
        if (kind == OGPU_KIND_DEPTH)
            clear(v, VG_CLEAR_DEPTH, 0, v->z_fmt == OGPU_FMT_Z16 ? (double)(val >> 16) / 65535.0 : (double)val / 4294967295.0, 0);
        else clear(v, VG_CLEAR_STENCIL, 0, 1.0, val & 255);
    } else {
        if (!v->have_frame) ready(v, 0);        /* the quad writes no colour, but the frame must be there */
        quad(v, x, y, w, h, 0, kind == OGPU_KIND_DEPTH ? (v->z_fmt == OGPU_FMT_Z16 ? val & 0xFFFF0000UL : val) : 0,
             kind == OGPU_KIND_DEPTH ? 1 : 2, val);
    }
    return 0;
}

/* ---- the batch ------------------------------------------------------------------------------------ */

/* The calls a batch is made of (stage M7 part 2). Warp3D's draw calls make them as they come (w3d_batch.c); the
 * reader below makes them from a recorded batch, for the route of before the shared pipe (ENV:Warp3D/PipeOld=1,
 * one release) and the PC test. */

static void batch_start(struct w3dvg *v, ULONG vbase, ULONG vlen, int grow)
{
    v->errors = 0;
    v->declined = 0;
    v->lost = 0;
    v->region = vbase;
    v->region_len = vlen <= REGION_MAX ? vlen : 0;
    v->grow = grow && v->px && v->region_len;
    v->g_lo = v->g_hi = 0;
    v->region_top = 0;
    block_begin(v);
    if (v->grow) {
        if (v->rbuf) v->rb_at = v->rb_off;      /* the batch's one place in the ring */
        else v->grow = 0;                       /* no ring: the vertices are copied */
    }
}

void w3dvg_begin(struct w3dvg *v, ULONG vbase, ULONG vcap) { batch_start(v, vbase, vcap, 1); }

void w3dvg_area(struct w3dvg *v, ULONG address, ULONG bpr, LONG w, LONG h, ULONG format)
{
    end_group(v);
    v->slot[0][0] = address; v->slot[0][1] = bpr; v->slot[0][2] = (ULONG)w; v->slot[0][3] = (ULONG)h; v->slot[0][4] = format;
    if (format != OGPU_FMT_ARGB32 && format != OGPU_FMT_RGB565) { v->declined++; return; }
    if (v->target != 0) v->sc_dirty = 1;
    v->target = 0;
    v->vp_dirty = 1;
    v->vc_ok = 0;
}

void w3dvg_clip(struct w3dvg *v, LONG x, LONG y, LONG w, LONG h)
{
    end_group(v);
    v->cx = x; v->cy = y; v->cw = w; v->ch = h;
}

void w3dvg_render3d(struct w3dvg *v, const ULONG *args)
{
    int i;
    end_group(v);
    for (i = 0; i < OGPU_R3D_ARGS; i++) v->r3d[i] = args[i];
}

void w3dvg_texenv(struct w3dvg *v, ULONG mode, ULONG colour)
{
    end_group(v);
    v->env = mode; v->envc = colour;
}

void w3dvg_texture(struct w3dvg *v, ULONG address, ULONG bpr, LONG w, LONG h, ULONG format, ULONG generation,
                   ULONG min, ULONG mag, ULONG wrap_s, ULONG wrap_t, ULONG border, ULONG levels, const ULONG *level_addrs)
{
    ULONG i;
    end_group(v);
    if (!levels) levels = 1;
    if (levels > OGPU_TEX_LEVELS) { v->t_addr = 0; v->declined++; return; }
    v->t_addr = address; v->t_bpr = bpr; v->t_w = (ULONG)w; v->t_h = (ULONG)h; v->t_fmt = format;
    v->t_gen = generation; v->t_filter = min | (mag << 8); v->t_wrap = wrap_s | (wrap_t << 8); v->t_border = border;
    v->t_levels = levels;
    for (i = 1; i < levels; i++) v->t_lv[i] = level_addrs ? level_addrs[i - 1] : 0;
}

void w3dvg_depth(struct w3dvg *v, ULONG kind, ULONG address, ULONG bpr, LONG w, LONG h, ULONG format)
{
    end_group(v);
    if (kind == OGPU_KIND_DEPTH) {
        if (address != v->z_addr) v->have_z = 0;
        v->z_addr = address; v->z_fmt = format; v->z_bpr = bpr; v->z_w = (ULONG)w; v->z_h = (ULONG)h;
    } else if (kind == OGPU_KIND_STENCIL) {
        if (address != v->s_addr) v->have_z = 0;
        v->s_addr = address; v->s_bpr = bpr; v->s_w = (ULONG)w; v->s_h = (ULONG)h;
    }
    v->sc_dirty = 1;
}

void w3dvg_fill(struct w3dvg *v, LONG x, LONG y, LONG w, LONG h, ULONG raw)
{
    end_group(v);
    if (fill(v, x, y, w, h, raw) < 0) v->declined++;
}

void w3dvg_clear3d(struct w3dvg *v, ULONG kind, LONG x, LONG y, LONG w, LONG h, ULONG value)
{
    end_group(v);
    if (clear3d(v, kind, x, y, w, h, value) < 0) v->declined++;
}

void w3dvg_triangles(struct w3dvg *v, ULONG vaddr, ULONG vcount, ULONG stride, ULONG tex, ULONG layout)
{
    if (triangles(v, vaddr, vcount, stride, 0, 0, 0, tex & 0xFFFF, layout) < 0) v->declined++;
}

LONG w3dvg_end(struct w3dvg *v, int present, int *presented)
{
    end_group(v);
    if (present && !v->colour) colour_for(v);
    v->presented = 0;
    block_send(v, present);
    if (v->grow) {
        v->rb_off += (v->region_top + 255) & ~255UL;    /* the ring's next place is after what this batch put there */
        v->grow = 0;
    }
    if (presented) *presented = present && (!v->px || !v->os.area_lock || !v->colour || v->presented);
    v->st.refused += v->errors;
    v->st.declined += v->declined;
    if (v->lost && v->os.area_lock) return -1;  /* direct calls: there is no batch to draw again; the caller leaves the route */
    return (LONG)(v->errors + v->declined);
}

/* A recorded batch, read back. */
LONG w3dvg_run(struct w3dvg *v, const UBYTE *s, long words, ULONG vbase, ULONG vlen, int present)
{
    long at = 0;
    batch_start(v, vbase, vlen, 0);
    while (at < words) {
        ULONG hdr = rd32(s + at * 4), op = OGPU_HDR_OP(hdr), n = OGPU_HDR_WORDS(hdr);
        const UBYTE *a = s + at * 4 + 4;
        long args = (long)n - 1;
        if (!n || at + (long)n > words) break;
        if (op != OGPU_OP_TRIANGLES && op != OGPU_OP_NOP) end_group(v);
        switch (op) {
        case OGPU_OP_NOP: break;
        case OGPU_OP_SURFACE: {
            ULONG sl = rd32(a);
            if (sl < 4) {
                v->slot[sl][0] = rd32(a + 4); v->slot[sl][1] = rd32(a + 8);
                v->slot[sl][2] = rd32(a + 12) >> 16; v->slot[sl][3] = rd32(a + 12) & 0xFFFF; v->slot[sl][4] = rd32(a + 16);
            }
            break;
        }
        case OGPU_OP_TARGET: {
            ULONG sl = rd32(a);
            if (sl < 4) {
                ULONG f = v->slot[sl][4];
                if (f != OGPU_FMT_ARGB32 && f != OGPU_FMT_RGB565) { v->declined++; break; }
                if ((int)sl != v->target) v->sc_dirty = 1;
                v->target = (int)sl;
                v->vp_dirty = 1;
                v->vc_ok = 0;
            } else v->declined++;
            break;
        }
        case OGPU_OP_CLIP: {
            ULONG xy = rd32(a), wh = rd32(a + 4);
            w3dvg_clip(v, (WORD)(xy >> 16), (WORD)(xy & 0xFFFF), (LONG)(wh >> 16), (LONG)(wh & 0xFFFF));
            break;
        }
        case OGPU_OP_FILL: {
            ULONG xy = rd32(a), wh = rd32(a + 4);
            w3dvg_fill(v, (WORD)(xy >> 16), (WORD)(xy & 0xFFFF), (LONG)(wh >> 16), (LONG)(wh & 0xFFFF), rd32(a + 8));
            break;
        }
        case OGPU_OP_RENDER3D: {
            int i;
            for (i = 0; i < OGPU_R3D_ARGS && i < args; i++) v->r3d[i] = rd32(a + i * 4);
            break;
        }
        case OGPU_OP_TEXENV:
            if (rd32(a) == 0) w3dvg_texenv(v, rd32(a + 4), rd32(a + 8));
            else v->declined++;
            break;
        case OGPU_OP_TEXTURE: {
            ULONG i, lv[OGPU_TEX_LEVELS], levels = rd32(a + 40);
            if (rd32(a)) { v->declined++; break; }
            if (!levels) levels = 1;
            if (levels > OGPU_TEX_LEVELS || (long)(10 + levels) > args) { v->t_addr = 0; v->declined++; break; }
            for (i = 1; i < levels; i++) lv[i - 1] = rd32(a + 44 + (i - 1) * 4);
            w3dvg_texture(v, rd32(a + 4), rd32(a + 8), (LONG)(rd32(a + 12) >> 16), (LONG)(rd32(a + 12) & 0xFFFF), rd32(a + 16),
                          rd32(a + 24), rd32(a + 28) & 255, rd32(a + 28) >> 8, rd32(a + 32) & 255, rd32(a + 32) >> 8,
                          rd32(a + 36), levels, lv);
            break;
        }
        case OGPU_OP_DEPTH: {
            ULONG wh = rd32(a + 12);
            w3dvg_depth(v, rd32(a), rd32(a + 4), rd32(a + 8), (LONG)(wh >> 16), (LONG)(wh & 0xFFFF), rd32(a + 16));
            break;
        }
        case OGPU_OP_CLEAR3D: {
            ULONG xy = rd32(a + 4), wh = rd32(a + 8);
            w3dvg_clear3d(v, rd32(a), (WORD)(xy >> 16), (WORD)(xy & 0xFFFF), (LONG)(wh >> 16), (LONG)(wh & 0xFFFF), rd32(a + 12));
            break;
        }
        case OGPU_OP_TRIANGLES: {
            if (args < 9                                /* SDL's form: not Warp3D's */
                || triangles(v, rd32(a), rd32(a + 4), rd32(a + 8), rd32(a + 12), rd32(a + 16), rd32(a + 20),
                             rd32(a + 24) & 0xFFFF, rd32(a + 32)) < 0)
                v->declined++;
            break;
        }
        case OGPU_OP_FENCE: break;
        default: v->declined++;
        }
        at += (long)n;
    }
    return w3dvg_end(v, present, 0);
}

/* ---- the context ----------------------------------------------------------------------------------- */

static void free_all(struct w3dvg *v)
{
    struct res *r;
    int i;
    if (v->px) { v->pipe->ctx_destroy(v->px); v->px = 0; }
    if (!v->os.pipe_old) v->va = 0, v->ia = 0;  /* the pipe's arenas: gone with its context */
    while ((r = v->dead) != 0) { v->dead = r->next_dead; dealloc(v, r); }
    for (i = 0; i < TEXTS; i++) dealloc(v, v->tex[i].res);
    if (v->zstage) v->os.free(v->os.user, v->zstage);
    if (v->cstage) v->os.free(v->os.user, v->cstage);
    dealloc(v, v->colour); dealloc(v, v->depth); dealloc(v, v->stage); dealloc(v, v->vbuf); dealloc(v, v->ibuf);
    dealloc(v, v->rbuf);
    dealloc(v, v->blk); dealloc(v, v->post); dealloc(v, v->cmd); dealloc(v, v->va); dealloc(v, v->ia); dealloc(v, v->text);
    if (v->screen) v->pipe->screen_close(v->screen);
    v->os.free(v->os.user, v);
}

struct w3dvg *w3dvg_open(const struct w3dvg_os *os, struct w3dvg_info *info)
{
    static const char name[] = "Warp3D.library";
    const struct OGPUPipeOps *pipe = ogpu_pipe_ops(OGPU_PIPE_VERSION);
    struct w3dvg *v;
    struct OGPUPipeOpen o;
    struct OGPUPipeInfo pi;
    ULONG i;
    char *nm;
    if (!pipe || !ogpu_pipe_xport_ready()) return 0;
    if (!(v = os->alloc(os->user, sizeof *v))) return 0;
    v->os = *os;
    v->pipe = pipe;
    v->ctx_id = ADDR(v);
    if (os->pipe_old) {
        v->blk = alloc(v, (PRE_WORDS + BLK_SLACK + 64) * 4);
        v->post = alloc(v, POST_WORDS * 4);
        v->cmd = alloc(v, CMD_WORDS * 4);
        v->va = alloc(v, VA_BYTES);
        v->ia = alloc(v, IA_COUNT * 2);
        v->text = alloc(v, TEXT_BYTES);
        if (!v->blk || !v->post || !v->cmd || !v->va || !v->ia || !v->text) { free_all(v); return 0; }
    } else {
        /* on the pipe (stage M6) the block, the arenas and the shaders are the pipe's: this file keeps the open's
         * block (and Z reads), and a little stream for a command no stream has room for (never, in Warp3D) */
        v->blk = alloc(v, 1024 * 4);
        v->cmd = alloc(v, 1024 * 4);
        if (!v->blk || !v->cmd) { free_all(v); return 0; }
    }
    nm = (char *)(v->blk + 512);
    for (i = 0; i < sizeof name; i++) nm[i] = name[i];
    /* HELLO, the context, the caps: the shared pipe's screen (library/pipe/pipe_screen.c), in the blocks this
     * file sent before; no caps is no error here (caps_bits stays 0) */
    o.size = sizeof o; o.emit_wanted = OGPU_EMIT_VIRGL;
    o.ctx_id = v->ctx_id; o.name = nm; o.name_len = sizeof name - 1;
    o.blk = v->blk;
    o.caps = v->blk + 768;
    o.caps_bytes = 1024;
    v->screen = pipe->screen_open(&o, &pi);
    v->st.blocks += pi.blocks;
    if (!v->screen) { say(v, "virgl: no virgl on the host"); free_all(v); return 0; }
    v->host_flags = pi.host_flags;
    v->caps_bits = pi.caps_bits;
    v->gles = (v->host_flags & ACV_HOST_GLES) != 0;
    if (!os->pipe_old) {
        /* the pipe's context: the one the open made, with its block, its arenas and their buffers */
        if (!(v->px = pipe->ctx_create(v->screen, 0))) { say(v, "virgl: no pipe context"); free_all(v); return 0; }
        pipe->state_reset(v->px);
        v->seq = pipe->sent(v->px);
        px_arenas(v);
        v->b_stride = v->b_vboff = v->b_iboff = 0xFFFFFFFFUL;  /* nothing bound yet: the first draw binds its buffers */
        v->b_vbuf = 0;
    }
    v->b_blend = v->b_dsa = v->b_rs = v->b_vs = v->b_fs = v->b_ve = v->b_view = v->b_samp = 0xFFFFFFFFUL;
    v->b_sref = v->b_bconst = 0xFFFFFFFFUL;
    v->b_stipple = 0;
    v->fb_dirty = v->vp_dirty = v->sc_dirty = 1;
    w3dvg_reset(v);
    if (info) { info->host_flags = v->host_flags; info->caps_bits = v->caps_bits; }
    return v;
}

void w3dvg_close(struct w3dvg *v)
{
    ULONG a[1];
    int i;
    if (!v) return;
    if (v->px) {
        end_group(v);
        for (i = 0; i < TEXTS; i++) if (v->tex[i].res) { res_release(v, v->tex[i].res); v->tex[i].res = 0; }
        res_release(v, v->colour); res_release(v, v->depth); res_release(v, v->stage); res_release(v, v->rbuf);
        v->colour = v->depth = v->stage = v->rbuf = 0;
        free_all(v);                    /* the pipe's context goes: what there is sent, the host's context gone */
        return;
    }
    block_begin(v);
    v->ncmd = 0;
    for (i = 0; i < TEXTS; i++) if (v->tex[i].res) { res_release(v, v->tex[i].res); v->tex[i].res = 0; }
    res_release(v, v->colour); res_release(v, v->depth); res_release(v, v->stage);
    res_release(v, v->vbuf); res_release(v, v->ibuf);
    v->colour = v->depth = v->stage = v->vbuf = v->ibuf = 0;
    block_send(v, 0);
    a[0] = v->ctx_id;
    blk_cmd(v->blk, ACV_CTX_DESTROY, 1, a);
    run(v, v->blk, BLK_WORDS(1));
    free_all(v);
}

void w3dvg_forget(struct w3dvg *v, ULONG addr)
{
    struct tex *t;
    if (!v || !(t = tex_find(v, addr))) return;
    block_begin(v);
    tex_drop(v, t);
    /* the unref goes with the next block */
}

void w3dvg_reset(struct w3dvg *v)
{
    int i;
    if (!v) return;
    end_group(v);
    /* the core's state at the start (ogpu3d_init, ogpu_core_init) */
    for (i = 0; i < OGPU_R3D_ARGS; i++) v->r3d[i] = 0;
    v->r3d[0] = OGPU_R3D_GOURAUD; v->r3d[1] = 2; v->r3d[2] = 8; v->r3d[3] = 2 | 1UL << 16;
    v->r3d[5] = 0xFF0F; v->r3d[6] = 4; v->r3d[12] = 2 | 255UL << 16; v->r3d[13] = 1 | 1 << 8 | 1UL << 16 | 255UL << 24;
    v->r3d[14] = 1; v->r3d[17] = 0xFFFFFFFFUL;
    v->env = OGPU_ENV_MODULATE; v->envc = 0;
    v->t_addr = 0;
    v->z_addr = v->s_addr = 0; v->have_z = 0;
    v->cx = v->cy = 0; v->cw = v->ch = 0;
    v->sc_dirty = v->vp_dirty = 1;
}

void w3dvg_forget_all(struct w3dvg *v)
{
    int i;
    if (!v) return;
    block_begin(v);
    for (i = 0; i < TEXTS; i++) if (v->tex[i].res) tex_drop(v, &v->tex[i]);
}

void w3dvg_stale(struct w3dvg *v) { if (v) v->have_frame = v->have_z = 0; }

void w3dvg_stale_z(struct w3dvg *v) { if (v) v->have_z = 0; }

void w3dvg_forget_z(struct w3dvg *v)
{
    if (!v) return;
    v->z_addr = v->s_addr = 0;
    v->have_z = 0;
    v->sc_dirty = 1;
}

/* The host's depth (and stencil) back into memory, for a program that reads
 * them (W3D_ReadZSpan ...). Z16 alone comes back as it is; the 24/8 and
 * 24-bit buffers come back a word a pixel and the 68k takes them apart: Z16
 * the top 16 bits of the 24, Z32 the 24 with its top byte again below (the
 * 8 bits the host doesn't keep). */
void w3dvg_read_z(struct w3dvg *v)
{
    ULONG w, h, zw = 0, zh = 0, sw = 0, sh = 0, x, y, a[14], bytes;
    if (!v || !v->depth || !v->have_z || (!v->z_addr && !v->s_addr)) return;
    w = v->depth->w; h = v->depth->h;
    if (v->z_addr) { zw = v->z_w < w ? v->z_w : w; zh = v->z_h < h ? v->z_h : h; }
    if (v->s_addr) { sw = v->s_w < w ? v->s_w : w; sh = v->s_h < h ? v->s_h : h; }
    bytes = v->zfmt == VG_FMT_Z16_UNORM ? w * h * 2 : w * h * 4;
    if (!stage_mem(v, &v->zstage, &v->zstage_bytes, bytes)) return;
    if (v->px) {
        /* what was drawn goes first; then the GET in a block of its own */
        px_commit(v);
        v->pipe->flush(v->px, 0);
        px_sync(v);
        a[0] = v->depth->handle; a[1] = v->ctx_id; a[2] = 0; a[3] = bytes / h; a[4] = 0;
        a[5] = 0; a[6] = 0; a[7] = 0; a[8] = w; a[9] = h; a[10] = 1;
        a[11] = ADDR(v->zstage); a[12] = bytes; a[13] = v->zfmt == VG_FMT_Z16_UNORM ? ACV_SWAP_16 : ACV_SWAP_32;
        blk_cmd(v->blk, ACV_GET, 14, a);
        v->errors = 0;
        if (run(v, v->blk, BLK_WORDS(14)) || result(v->blk, 0)) { v->errors++; say(v, "virgl: the host refused the Z read"); }
    } else {
    block_begin(v);
    a[0] = v->depth->handle; a[1] = v->ctx_id; a[2] = 0; a[3] = bytes / h; a[4] = 0;
    a[5] = 0; a[6] = 0; a[7] = 0; a[8] = w; a[9] = h; a[10] = 1;
    a[11] = ADDR(v->zstage); a[12] = bytes; a[13] = v->zfmt == VG_FMT_Z16_UNORM ? ACV_SWAP_16 : ACV_SWAP_32;
    blk_cmd(pre(v, BLK_WORDS(14)), ACV_GET, 14, a);
    v->errors = 0;
    block_send(v, 0);
    }
    v->st.refused += v->errors;
    if (v->errors) return;
    for (y = 0; y < h; y++) {
        const UBYTE *sp = (const UBYTE *)v->zstage + y * (bytes / h);
        UBYTE *z = y < zh ? (UBYTE *)(v->z_addr + y * v->z_bpr) : 0;
        UBYTE *st = y < sh ? (UBYTE *)(v->s_addr + y * v->s_bpr) : 0;
        if (v->zfmt == VG_FMT_Z16_UNORM) {
            for (x = 0; z && x < zw * 2; x++) z[x] = sp[x];
            continue;
        }
        for (x = 0; x < w; x++, sp += 4) {
            ULONG word = (ULONG)sp[0] << 24 | (ULONG)sp[1] << 16 | (ULONG)sp[2] << 8 | sp[3];
            ULONG d = v->zfmt == VG_FMT_D24S8 ? word >> 8 : word & 0xFFFFFFUL;
            if (z && x < zw) {
                if (v->z_fmt == OGPU_FMT_Z16) { z[x * 2] = (UBYTE)(d >> 16); z[x * 2 + 1] = (UBYTE)(d >> 8); }
                else { z[x * 4] = (UBYTE)(d >> 16); z[x * 4 + 1] = (UBYTE)(d >> 8); z[x * 4 + 2] = (UBYTE)d; z[x * 4 + 3] = (UBYTE)(d >> 16); }
            }
            if (st && x < sw && v->zfmt == VG_FMT_D24S8) st[x] = (UBYTE)word;
        }
    }
}

void w3dvg_stats(struct w3dvg *v, struct w3dvg_stats *s)
{
    *s = v->st;
    v->st.blocks = v->st.draws = v->st.merged = v->st.triangles = v->st.objects = v->st.shaders = 0;
    v->st.puts = v->st.put_bytes = v->st.loads = v->st.presents = v->st.refused = v->st.declined = 0;
}
