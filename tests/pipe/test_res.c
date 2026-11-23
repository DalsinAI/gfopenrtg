/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's resources, blocks and arenas (stage M3 of docs/design/OpenGPU-Shared-Pipe-Plan.md), against a
 * pretend host that keeps every resource handle it was given and checks each block's use of them: the resource
 * lifetime tests the plan puts first, before any MiniGL code moves:
 *   create, update (whole and boxed), delete;
 *   rename-on-write while an earlier block still reads the old version;
 *   deferred destroy: a released resource is freed only after the block that last used it has run (fence-safe);
 *   reference counts across two contexts;
 *   ring wrap with a block in flight (no PUT over what an earlier block's draws read);
 *   render-target and depth-buffer resize;
 * and the block's order (pre-commands, arenas, stream, post-commands, present), the arenas growing, read-back and a
 * context's end. Under AddressSanitizer: a version freed too early, or never, fails the run.
 *
 * Built 32-bit with VG_HOST (the blocks in the Amiga's order), as the pipe's other tests.
 *   tests/pipe/test_res.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/gfopengpu/pipe.h"
#include "../../include/gfopengpu/virgl_cmd.h"

static int failures, checks;
#define PIPE_TEST_SLOTS 64        /* the pipe's blend table (pipe_int.h's PIPE_CACHE) */
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* The test's own stream word: "this stream reads resource H" (two words: the mark, the handle). */
#define READS 0x52454144UL

/* ---- the pretend host ------------------------------------------------------------------------------------------- */

#define HMAX 256
static struct {
    ULONG alive[HMAX]; int nalive;          /* resource handles the host holds */
    ULONG made[HMAX]; ULONG mbytes[HMAX]; int nmade;   /* every create: handle, width */
    int blocks, bad;                        /* blocks run; a use of a handle the host doesn't hold */
    char order[64];                         /* the last block's commands: c(reate) p(ut) s(ubmit) u(nref) P(resent) d(estroy) g(et) */
    ULONG submit_reads[16]; int nreads;     /* the last SUBMIT's READS handles */
    struct { ULONG h, off, bytes; } puts[64]; int nputs;     /* every buffer PUT: for the ring check */
    ULONG last_put[16]; int nlast;          /* the last block's PUT args (level..swap), for the first PUT */
    ULONG get_handle;
    int fail_next;                          /* the next block doesn't run */
} H;

static int alive(ULONG h) { int i; for (i = 0; i < H.nalive; i++) if (H.alive[i] == h) return 1; return 0; }
static void add(ULONG h) { if (H.nalive < HMAX) H.alive[H.nalive++] = h; }
static int drop(ULONG h)
{
    int i;
    for (i = 0; i < H.nalive; i++) if (H.alive[i] == h) { H.alive[i] = H.alive[--H.nalive]; return 1; }
    return 0;
}
static void ord(char c) { size_t n = strlen(H.order); if (n + 1 < sizeof H.order) { H.order[n] = c; H.order[n + 1] = 0; } }

int ogpu_pipe_xport_ready(void) { return 1; }
void *ogpu_pipe_xport_alloc(ULONG bytes) { return calloc(1, bytes ? bytes : 4); }
void ogpu_pipe_xport_free(void *p) { free(p); }

LONG ogpu_pipe_xport_run(ULONG *blk, ULONG bytes)
{
    ULONG at = 0, words = bytes / 4, i;
    H.blocks++;
    H.order[0] = 0;
    if (H.fail_next) { H.fail_next = 0; return -1; }
    while (at < words) {
        ULONG hdr = VG_GETW(blk + at), cmd = ACV_HDR_CMD(hdr), n = ACV_HDR_WORDS(hdr);
        if (!n) break;
        VG_PUTW(blk + at + 1, 0);
        switch (cmd) {
        case ACV_HELLO: VG_PUTW(blk + at + 1, ACV_PROTOCOL); VG_PUTW(blk + at + 2, 0); break;
        case ACV_CTX_CREATE: break;
        case ACV_CAPSET_INFO: VG_PUTW(blk + at + 3, 2); VG_PUTW(blk + at + 4, 64); break;
        case ACV_CAPS: VG_PUTW(blk + at + 1, VG_GETW(blk + at + 5)); break;
        case ACV_RES_CREATE: {
            ULONG h = VG_GETW(blk + at + 2);
            if (alive(h)) H.bad++;
            add(h);
            if (H.nmade < HMAX) { H.made[H.nmade] = h; H.mbytes[H.nmade++] = VG_GETW(blk + at + 6); }
            ord('c');
            break;
        }
        case ACV_RES_UNREF:
            if (!drop(VG_GETW(blk + at + 2))) H.bad++;
            ord('u');
            break;
        case ACV_PUT: {
            ULONG h = VG_GETW(blk + at + 2);
            if (!alive(h)) H.bad++;
            if (H.nputs < 64) { H.puts[H.nputs].h = h; H.puts[H.nputs].off = VG_GETW(blk + at + 7); H.puts[H.nputs++].bytes = VG_GETW(blk + at + 14); }
            if (!strchr(H.order, 'p')) for (i = 0; i < 12; i++) H.last_put[i] = VG_GETW(blk + at + 4 + i);
            ord('p');
            break;
        }
        case ACV_GET:
            if (!alive(VG_GETW(blk + at + 2))) H.bad++;
            H.get_handle = VG_GETW(blk + at + 2);
            ord('g');
            break;
        case ACV_SUBMIT: {
            const ULONG *s = (const ULONG *)(unsigned long)VG_GETW(blk + at + 3);
            ULONG nw = VG_GETW(blk + at + 4), k;
            H.nreads = 0;
            for (k = 0; k + 1 < nw; k++)
                if (VG_LE(s[k]) == READS) {
                    ULONG h = VG_LE(s[k + 1]);
                    if (!alive(h)) H.bad++;     /* a draw reads a resource the host let go of */
                    if (H.nreads < 16) H.submit_reads[H.nreads++] = h;
                    k++;
                }
            ord('s');
            break;
        }
        case ACV_PRESENT: ord('P'); break;
        case ACV_CTX_DESTROY: ord('d'); break;
        default: VG_PUTW(blk + at + 1, (ULONG)-1);
        }
        at += n;
    }
    return 0;
}

static void host(void) { memset(&H, 0, sizeof H); }

/* ---- helpers ----------------------------------------------------------------------------------------------------- */

static const struct OGPUPipeOps *ops;
static ULONG obk[16][2], ocaps[2][64];
static char oname[] = "test";

static OGPUPipeCtx *open_ctx(OGPUPipeScreen **sp, ULONG id, int k)
{
    struct OGPUPipeOpen o;
    struct OGPUPipeInfo info;
    memset(&o, 0, sizeof o);
    o.size = sizeof o; o.emit_wanted = OGPU_EMIT_VIRGL; o.ctx_id = id; o.name = oname; o.name_len = 4;
    o.blk = obk[k * 8]; o.caps = ocaps[k]; o.caps_bytes = sizeof ocaps[k];
    *sp = ops->screen_open(&o, &info);
    return *sp ? ops->ctx_create(*sp, 0) : 0;
}

static void close_ctx(OGPUPipeScreen *s, OGPUPipeCtx *x)
{
    ops->ctx_destroy(x);
    ops->screen_close(s);
}

static void reads(OGPUPipeCtx *x, OGPUPipeRes *r)     /* a "draw" in the stream that samples r */
{
    ULONG *p = ops->cmd_room(x, 2);
    if (p) { VG_W(p, READS); VG_W(p, ops->res_handle(r)); }
    ops->res_used(x, r);
}

static OGPUPipeRes *tex(OGPUPipeCtx *x, ULONG w, ULONG h)
{
    struct OGPUPipeResDesc d;
    d.target = VG_TARGET_2D; d.format = VG_FMT_B8G8R8A8_UNORM; d.bind = VG_BIND_SAMPLER_VIEW;
    d.width = w; d.height = h; d.levels = 1; d.flags = 0;
    return ops->res_create(x, &d);
}

static ULONG texels[64 * 64];

static ULONG present_prepare(void *user, ULONG ctx, ULONG handle, ULONG *p, ULONG room)
{
    ULONG a[1];
    (void)user; (void)handle; (void)room;
    a[0] = ctx;
    vgc_blk_cmd(p, ACV_PRESENT, 1, a);
    return VG_BLK_WORDS(1);
}
static ULONG finish_at; static int finished;
static void present_finish(void *user, const ULONG *blk, ULONG at) { (void)user; finished = blk != 0; finish_at = at; }

int main(void)
{
    OGPUPipeScreen *s, *s2;
    OGPUPipeCtx *x, *y;
    OGPUPipeRes *a, *b, *rt, *z;
    struct OGPUBox box;
    struct OGPUPipeArenas ar;
    ULONG h0, h1, v5[5];
    int i, k;

    ops = ogpu_pipe_ops(2);
    CHECK(ops && ops->version >= 2, "the table, version 2 or later");
    CHECK(ogpu_pipe_ops(1) == ops, "a version 1 caller gets the same table");
    host();
    x = open_ctx(&s, 0x1000, 0);
    CHECK(x != 0, "a context");
    CHECK(ops->ctx_create(s, 0) == 0, "one context a screen (M3)");
    ops->flush(x, 0);
    CHECK(H.nalive == 2 && !H.bad, "its vertex and index buffers made (%d)", H.nalive);

    /* ---- create, update whole and boxed, delete ---- */
    a = tex(x, 64, 64);
    h0 = ops->res_handle(a);
    CHECK(a && h0, "a texture");
    box.x = 0; box.y = 0; box.z = 0; box.w = 64; box.h = 64; box.d = 1;
    ops->res_update(x, a, 0, &box, texels, 64 * 4, VG_SW32);
    CHECK(ops->res_pending(x, a) == OGPU_PIPE_RES_PUT, "a PUT of it waits");
    ops->flush(x, 0);
    CHECK(!strcmp(H.order, "cp") && alive(h0) && !H.bad, "made, then its texels (%s)", H.order);
    CHECK(H.last_put[0] == 0 && H.last_put[1] == 256 && H.last_put[6] == 64 && H.last_put[7] == 64
          && H.last_put[10] == 64 * 64 * 4 && H.last_put[11] == VG_SW32, "the whole level: stride 256, 64x64, 16384 bytes");
    CHECK(ops->res_pending(x, a) == 0, "nothing waits after the block");
    box.x = 8; box.y = 4; box.w = 16; box.h = 2;
    ops->res_update(x, a, 0, &box, texels + 4 * 64 + 8, 64 * 4, VG_SW32);
    ops->flush(x, 0);
    CHECK(H.last_put[3] == 8 && H.last_put[4] == 4 && H.last_put[6] == 16 && H.last_put[7] == 2
          && H.last_put[10] == 64 * 4 + 16 * 4, "a box: at 8,4, 16x2, one stride and a row (%lu)", (unsigned long)H.last_put[10]);
    CHECK(ops->res_handle(a) == h0, "written in place: nothing read it");
    ops->res_release(x, a);
    CHECK(alive(h0), "released: still on the host until the block");
    ops->flush(x, 0);
    CHECK(!alive(h0) && !strcmp(H.order, "u") && !H.bad, "unref'd in the next block (%s)", H.order);

    /* ---- rename-on-write while the stream not yet sent reads the old version ---- */
    a = tex(x, 64, 64);
    ops->flush(x, 0);
    h0 = ops->res_handle(a);
    reads(x, a);
    CHECK(ops->res_pending(x, a) == OGPU_PIPE_RES_DRAWN, "drawn with in the stream being built");
    CHECK(ops->res_write(x, a) == 1, "a write now: renamed");
    h1 = ops->res_handle(a);
    CHECK(h1 && h1 != h0, "a new version, the same OGPUPipeRes");
    box.x = 0; box.y = 0; box.w = 64; box.h = 64;
    ops->res_update(x, a, 0, &box, texels, 256, VG_SW32);
    CHECK(ops->res_handle(a) == h1, "the update after it: no second rename");
    reads(x, a);
    ops->flush(x, 0);
    CHECK(!strcmp(H.order, "cpsu"), "new version made and filled, the stream, then the old one let go of (%s)", H.order);
    CHECK(H.nreads == 2 && H.submit_reads[0] == h0 && H.submit_reads[1] == h1, "the earlier draw read the old, the later the new");
    CHECK(!alive(h0) && alive(h1) && !H.bad, "the old gone after the block, the new kept");
    CHECK(ops->res_write(x, a) == 0, "nothing reads it now: written in place");

    /* ---- deferred destroy: released while the stream being built reads it ---- */
    reads(x, a);
    ops->res_release(x, a);
    CHECK(alive(h1), "released: kept until the stream that reads it has run");
    ops->flush(x, 0);
    CHECK(!strcmp(H.order, "su") && H.nreads == 1 && H.submit_reads[0] == h1 && !alive(h1) && !H.bad,
          "the stream read it, then it was let go of (%s)", H.order);

    /* ---- reference counts across two contexts ---- */
    y = open_ctx(&s2, 0x2000, 1);
    CHECK(y != 0, "a second context");
    ops->flush(y, 0);
    a = tex(x, 32, 32);
    ops->flush(x, 0);
    h0 = ops->res_handle(a);
    ops->res_ref(a);                        /* the second context holds it too */
    reads(y, a);                            /* y's stream, not sent yet, reads it */
    ops->res_release(x, a);
    CHECK(alive(h0), "one of two holders let go: kept");
    ops->res_release(x, a);
    ops->flush(x, 0);
    CHECK(alive(h0) && !strchr(H.order, 'u'), "the last holder let go while y's stream reads it: not in x's block (%s)", H.order);
    ops->flush(y, 0);
    CHECK(!strcmp(H.order, "su") && H.nreads == 1 && H.submit_reads[0] == h0 && !alive(h0) && !H.bad,
          "after y's stream, in y's block (%s)", H.order);

    /* ---- ring wrap with a block in flight ---- */
    ops->arenas(x, &ar);
    h0 = ar.vbuf;
    H.nputs = 0;
    for (i = 0; i < 6; i++) {               /* each block nearly a whole arena */
        ULONG first;
        UBYTE *p = ops->vtx_room(x, ar.cva - 4096, &first);
        CHECK(p != 0 && first == 0, "block %d: the arena from its start", i);
        ops->flush(x, 0);
    }
    k = 1;
    for (i = 0; i < H.nputs; i++) {
        int j;
        for (j = 0; j < i; j++)
            if (H.puts[j].h == H.puts[i].h && H.puts[i].off < H.puts[j].off + H.puts[j].bytes
                && H.puts[j].off < H.puts[i].off + H.puts[i].bytes) k = 0;
    }
    CHECK(H.nputs == 6 && k, "no PUT over what an earlier block's draws read (%d PUTs)", H.nputs);
    ops->arenas(x, &ar);
    CHECK(ar.vbuf != h0, "past the ring's end: a new vertex buffer");
    CHECK(!alive(h0) && !H.bad, "the old one let go of after the block that last wrote it");
    CHECK(ops->changed(x) & OGPU_PIPE_CH_VBUF, "the front end told to set VERTEX_BUFFER again");

    /* ---- render-target and depth-buffer resize ---- */
    {
        struct OGPUPipeResDesc d;
        d.target = VG_TARGET_2D; d.format = VG_FMT_B8G8R8A8_UNORM; d.bind = VG_BIND_RENDER_TARGET | VG_BIND_SAMPLER_VIEW;
        d.width = 640; d.height = 480; d.levels = 1; d.flags = 0;
        rt = ops->res_create(x, &d);
        d.format = VG_FMT_Z24X8_UNORM; d.bind = VG_BIND_DEPTH_STENCIL;
        z = ops->res_create(x, &d);
        ops->flush(x, 0);
        h0 = ops->res_handle(rt); h1 = ops->res_handle(z);
        reads(x, rt); reads(x, z);          /* drawn into */
        d.format = VG_FMT_B8G8R8A8_UNORM; d.bind = VG_BIND_RENDER_TARGET | VG_BIND_SAMPLER_VIEW; d.width = 800; d.height = 600;
        a = ops->res_create(x, &d);
        d.format = VG_FMT_Z24X8_UNORM; d.bind = VG_BIND_DEPTH_STENCIL;
        b = ops->res_create(x, &d);
        ops->res_release(x, rt); ops->res_release(x, z);
        reads(x, a); reads(x, b);
        ops->flush(x, 0);
        CHECK(!strcmp(H.order, "ccsuu"), "the new pair made, the stream (old pair, then new), the old pair let go of (%s)", H.order);
        CHECK(H.nreads == 4 && H.submit_reads[0] == h0 && H.submit_reads[1] == h1 && !alive(h0) && !alive(h1)
              && alive(ops->res_handle(a)) && alive(ops->res_handle(b)) && !H.bad, "the resize safe for the draws before it");
        CHECK(H.mbytes[H.nmade - 1] == 800 && H.mbytes[H.nmade - 2] == 800, "made at the new size");
        ops->res_release(x, a); ops->res_release(x, b);
        ops->flush(x, 0);
    }

    /* ---- an arena too small for one draw grows; the old buffer goes after ---- */
    ops->arenas(x, &ar);
    h0 = ar.vbuf;
    {
        ULONG big = ar.cva * 3, first;
        UBYTE *p = ops->vtx_room(x, big, &first);
        ops->arenas(x, &ar);
        CHECK(p && first == 0 && ar.cva >= big + 64 && ar.vbuf != h0, "the arena grew to %lu bytes, a new buffer", (unsigned long)ar.cva);
        ops->flush(x, 0);
        CHECK(!alive(h0) && alive(ar.vbuf) && !H.bad, "the old buffer let go of");
    }

    /* ---- indices ---- */
    {
        ULONG first;
        UWORD *ip = ops->idx_room(x, 6, &first);
        CHECK(ip && first == 0, "six indices from the index arena's start");
        ip = ops->idx_room(x, 3, &first);
        CHECK(ip && first == 6, "three more after them");
        ops->flush(x, 0);
        CHECK(H.puts[H.nputs - 1].bytes == 18, "one PUT of the nine (18 bytes)");
    }

    /* ---- the present rides at the end of the frame's block ---- */
    {
        struct OGPUPresentHook hk;
        ULONG fence = 99;
        hk.prepare = present_prepare; hk.finish = present_finish; hk.user = 0;
        a = tex(x, 8, 8);
        reads(x, a);
        ops->res_release(x, a);
        CHECK(ops->end_frame(x, &hk, &fence) == 0 && fence == 0, "end_frame: sent, fence 0 (done when it returns)");
        CHECK(!strcmp(H.order, "csuP") && finished && finish_at > 0, "create, stream, unref, then the present (%s)", H.order);
    }

    /* ---- read-back ---- */
    {
        static ULONG back[16];
        a = tex(x, 4, 4);
        box.x = 0; box.y = 0; box.w = 4; box.h = 4;
        CHECK(ops->res_read(x, a, 0, &box, back, 16) == 0 && H.get_handle == ops->res_handle(a) && !strcmp(H.order, "g"),
              "read back: the block before it sent, then the GET (%s)", H.order);
        ops->res_release(x, a);
    }

    /* ---- a block that doesn't run: reported, and lost whole (the host is gone or refused it), so nothing of the
     * resources' lives rides in it here ---- */
    ops->flush(x, 0);
    H.fail_next = 1;
    {
        ULONG *p = ops->cmd_room(x, 2);
        if (p) { VG_W(p, 0); VG_W(p, 0); }
        CHECK(ops->flush(x, 0) == -1 && ops->errors(x) == 1, "a block that didn't run: reported");
    }

    ops->counts(x, v5);
    CHECK(v5[0] > 10 && v5[2] == 3 && v5[3] == 1, "the counts: %lu blocks, %lu puts, %lu renames",
          (unsigned long)v5[0], (unsigned long)v5[2], (unsigned long)v5[3]);

    /* ---- stage M4: state objects, made once per key, bound and sent only when they change ---- */
    {
        struct OGPUBlendKey bk; struct OGPUDsaKey dk; struct OGPURasterKey rk; struct OGPUSamplerKey sk;
        struct OGPULayout lk;
        OGPUPipeCSO *b1, *b2, *sa;
        ULONG *c0, *c1, h, o0;
        float vp[6] = { 1, 2, 3, 4, 5, 6 }, k4[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        ops->flush(x, 0);
        ops->state_reset(x);
#define WORDS(stmt) (c0 = ops->cmd_room(x, 0), stmt, c1 = ops->cmd_room(x, 0), (ULONG)(c1 - c0))
        o0 = ops->obj_new(x);
        CHECK(ops->obj_new(x) == o0 + 1, "object handles: one namespace, counting up");
        bk.rt0 = 0x12345;
        CHECK(WORDS(b1 = ops->blend_create(x, &bk)) == 12 && b1 && ops->cso_handle(b1) == o0 + 2,
              "a blend object: created once (12 words), its handle the next");
        CHECK(WORDS(b2 = ops->blend_create(x, &bk)) == 0 && b2 == b1, "the same key: the same object, nothing sent");
        CHECK(WORDS(ops->bind_blend(x, b1)) == 2 && WORDS(ops->bind_blend(x, b1)) == 0, "bound once, not again");
        bk.rt0 = 0x54321;
        b2 = ops->blend_create(x, &bk);
        CHECK(b2 != b1 && WORDS(ops->bind_blend(x, b2)) == 2, "another key: another object, bound");
        dk.s0 = 7; dk.alpha_ref = 0;
        CHECK(WORDS(ops->bind_dsa(x, ops->dsa_create(x, &dk))) == 6 + 2, "a DSA object (6 words) and its bind");
        memset(&rk, 0, sizeof rk); rk.s0 = 2;
        CHECK(WORDS(ops->bind_raster(x, ops->raster_create(x, &rk))) == 10 + 2, "a rasterizer object (10 words) and its bind");
        sk.s0 = 1; sk.levels = 3;
        CHECK(WORDS(sa = ops->sampler_create(x, &sk)) == 10 && ops->sampler_create(x, &sk) == sa, "a sampler state, once");
        for (k = 0; k < 40; k++) { sk.s0 = 100 + (ULONG)k; ops->sampler_create(x, &sk); }
        h = ops->cso_handle(ops->sampler_create(x, &sk));
        CHECK(h != 0, "samplers past the table's 32: still made (%lu)", (unsigned long)h);
        lk.key = 0x51; lk.n = 2; lk.e[0].offset = 0; lk.e[0].format = 31; lk.e[1].offset = 16; lk.e[1].format = 58;
        CHECK(WORDS(b1 = ops->layout_create(x, &lk)) == 2 * 4 + 2 && ops->layout_create(x, &lk) == b1,
              "a vertex layout (10 words), once per key");
        CHECK(WORDS(ops->bind_layout(x, b1)) == 2, "the layout bound");
        CHECK(WORDS(ops->set_viewport(x, vp)) == VGC_VIEWPORT_WORDS && WORDS(ops->set_viewport(x, vp)) == 0,
              "the viewport sent once");
        box.x = 1; box.y = 2; box.w = 3; box.h = 4;
        CHECK(WORDS(ops->set_scissor(x, &box)) == VGC_SCISSOR_WORDS && WORDS(ops->set_scissor(x, &box)) == 0
              && WORDS(ops->set_scissor(x, 0)) == 0, "the scissor sent once; none sends nothing");
        CHECK(WORDS(ops->set_constants(x, OGPU_STAGE_VERTEX, k4, 2)) == VGC_CONSTANTS_WORDS(2)
              && WORDS(ops->set_constants(x, OGPU_STAGE_VERTEX, k4, 2)) == 0
              && WORDS(ops->set_constants(x, OGPU_STAGE_FRAGMENT, k4, 2)) == VGC_CONSTANTS_WORDS(2),
              "constants sent when they changed, by stage");
        k4[7] = 9;
        CHECK(WORDS(ops->set_constants(x, OGPU_STAGE_VERTEX, k4, 2)) == VGC_CONSTANTS_WORDS(2), "one word changed: sent");
        ops->state_reset(x);
        CHECK(WORDS(ops->bind_layout(x, b1)) == 2 && WORDS(ops->set_viewport(x, vp)) == VGC_VIEWPORT_WORDS
              && WORDS(ops->set_constants(x, OGPU_STAGE_VERTEX, k4, 2)) == VGC_CONSTANTS_WORDS(2),
              "after a reset everything goes again");
        /* a full probe: keys below 2048 hash to themselves, so multiples of 64 all start at slot 0 and fill its four
         * probes; the fifth makes the slot's old object go (destroyed on the host) */
        for (k = 1; k <= 4; k++) {
            bk.rt0 = (ULONG)k * PIPE_TEST_SLOTS;
            ops->blend_create(x, &bk);
        }
        bk.rt0 = 5 * PIPE_TEST_SLOTS;
        CHECK(WORDS(b1 = ops->blend_create(x, &bk)) == VGC_DESTROY_WORDS + 12, "a full probe: one goes, the new made");
        CHECK(WORDS(ops->blend_create(x, &bk)) == 0 && ops->blend_create(x, &bk) == b1, "and found after");
        ops->flush(x, 0);
#undef WORDS
    }

    /* ---- the contexts' end: their buffers let go of, the context destroyed ---- */
    ops->res_release(x, 0);                 /* nothing happens */
    close_ctx(s2, y);
    CHECK(strchr(H.order, 'd') != 0, "y destroyed (%s)", H.order);
    close_ctx(s, x);
    CHECK(H.nalive == 0 && !H.bad, "nothing left on the host (%d)", H.nalive);

    printf("pipe resources: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
