/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Stage M7, part 1: the shared pipe's stream3d emitter (library/pipe/pipe_emit_s3d.c) writes the words the front
 * ends' own builders wrote. `old_` below is that builder as w3d_batch.c and mgl_out.c had it before the move
 * (main at 3b8ab52), kept here as the reference; the same calls go to both, and the two streams are compared
 * word for word, with where they start and how long they are.
 *
 *   tests/pipe/test_s3d_same.sh */
#include <stdio.h>
#include <string.h>
#include "../../include/gfopengpu/pipe_s3d.h"
#include "../../include/gfopengpu/stream3d.h"

#define BATCH_WORDS  1024
#define PREFIX_WORDS 16
#define VERT_BYTES   4096
#define STRIDE       40

static int checks, failed;
#define CHECK(c, ...) do { checks++; if (!(c)) { failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the builder as the front ends had it ------------------------------------------------------ */

struct old {
    struct OGPUBatch b;
    ULONG cmdmem[BATCH_WORDS];
    ULONG vused;
    int body;
    long list_at;
    ULONG list_vend;
};

static void old_begin(struct old *x, const struct OGPUPipeS3DBuf *z, const struct OGPUPipeS3DBuf *st)
{
    if (!x->body) {
        ogpu_batch_init(&x->b, x->cmdmem + PREFIX_WORDS, BATCH_WORDS - PREFIX_WORDS);
        x->body = 1;
        x->list_at = -1;
        if (z) ogpu_depth(&x->b, OGPU_KIND_DEPTH, z->address, z->bpr, z->w, z->h, z->format);
        if (st) ogpu_depth(&x->b, OGPU_KIND_STENCIL, st->address, st->bpr, st->w, st->h, st->format);
    }
}

static void old_emit(struct old *x, unsigned long d, ULONG n, int prim, int tex, unsigned long layout)
{
    ULONG bytes = n * STRIDE;
    if (!n) return;
    if (prim == OGPU_LAY_LIST && x->list_at >= 0 && (ULONG)d == x->list_vend) {
        unsigned char *cnt = (unsigned char *)(x->cmdmem + PREFIX_WORDS) + (x->list_at + 2) * 4;
        ULONG v = (((ULONG)cnt[0] << 24) | ((ULONG)cnt[1] << 16) | ((ULONG)cnt[2] << 8) | cnt[3]) + n;
        cnt[0] = (unsigned char)(v >> 24); cnt[1] = (unsigned char)(v >> 16); cnt[2] = (unsigned char)(v >> 8); cnt[3] = (unsigned char)v;
        x->list_vend = (ULONG)d + bytes;
        return;
    }
    if (prim == OGPU_LAY_LIST) { x->list_at = x->b.words; x->list_vend = (ULONG)d + bytes; }
    else x->list_at = -1;
    ogpu_triangles3d(&x->b, d, n, STRIDE, 0, 0, 0, tex, 0, layout | (unsigned long)prim);
}

static long old_finish(struct old *x, const struct OGPUPipeS3DBuf *a, unsigned char **stream)
{
    struct OGPUBatch pre;
    ULONG prefix[PREFIX_WORDS];
    long words;
    x->body = 0;
    x->list_at = -1;
    ogpu_batch_init(&pre, prefix, PREFIX_WORDS);
    ogpu_surface(&pre, 0, a->address, a->bpr, a->w, a->h, a->format);
    ogpu_target(&pre, 0);
    *stream = (unsigned char *)(x->cmdmem + PREFIX_WORDS - pre.words);
    memcpy(*stream, prefix, (size_t)pre.words * 4);
    words = pre.words + x->b.words;
    return words;
}

/* ---- the two side by side ---------------------------------------------------------------------- */

static const struct OGPUPipeS3DOps *o;
static struct OGPUPipeS3D e;
static ULONG cmd[BATCH_WORDS];
static UBYTE vmem[VERT_BYTES];
static struct old x;

static const struct OGPUPipeS3DBuf AREA = { 0x40000000UL, 1280, 320, 240, OGPU_FMT_ARGB32 };
static const struct OGPUPipeS3DBuf AREA16 = { 0x40800000UL, 640, 320, 240, OGPU_FMT_RGB565 };
static const struct OGPUPipeS3DBuf Z = { 0x41000000UL, 640, 320, 240, OGPU_FMT_Z16 };
static const struct OGPUPipeS3DBuf ST = { 0x42000000UL, 320, 320, 240, OGPU_FMT_S8 };
#define VBASE 0x50000000UL

static void r3d_args(unsigned long *a, unsigned long seed)
{
    int i;
    for (i = 0; i < OGPU_R3D_ARGS; i++) a[i] = seed * 2654435761UL + (unsigned long)i * 0x01010101UL;
}

static void both_begin(const struct OGPUPipeS3DBuf *z, const struct OGPUPipeS3DBuf *st, int expect_new)
{
    int was = x.body;
    LONG r = o->begin(&e, z, st);
    old_begin(&x, z, st);
    CHECK(r == expect_new && (was == 0) == expect_new, "begin answered %ld, wanted %d", (long)r, expect_new);
}

static void both_clip(int l, int t, int w, int h) { o->clip(&e, l, t, w, h); ogpu_clip(&x.b, l, t, w, h); x.list_at = -1; }
static void both_render3d(unsigned long seed)
{
    unsigned long a[OGPU_R3D_ARGS];
    r3d_args(a, seed);
    o->render3d(&e, a); ogpu_render3d(&x.b, a); x.list_at = -1;
}
static void both_texture(unsigned long addr, int w, int h, int levels)
{
    unsigned long lv[OGPU_TEX_LEVELS];
    int i;
    for (i = 0; i < OGPU_TEX_LEVELS; i++) lv[i] = addr + 0x10000UL * (unsigned long)(i + 1);
    if (!addr) {
        o->texture(&e, 0, 0, 0, 1, 1, OGPU_FMT_ARGB32, 0, 0, 1, 1, 1, 1, 0, 1, 0);
        ogpu_texture(&x.b, 0, 0, 0, 1, 1, OGPU_FMT_ARGB32, 0, 0, 1, 1, 1, 1, 0, 1, 0);
    } else {
        o->texenv(&e, 0, 2, 0x80FF4020UL);
        ogpu_texenv(&x.b, 0, 2, 0x80FF4020UL);
        o->texture(&e, 0, addr, (unsigned long)w * 4, w, h, OGPU_FMT_ARGB32, 0, 7, 2, 1, 1, 0, 0x11223344UL, levels, lv);
        ogpu_texture(&x.b, 0, addr, (unsigned long)w * 4, w, h, OGPU_FMT_ARGB32, 0, 7, 2, 1, 1, 0, 0x11223344UL, levels, lv);
    }
    x.list_at = -1;
}
/* n vertices from the data area, as the front ends take them, then drawn */
static void both_draw(ULONG n, int prim, int tex)
{
    UBYTE *p = o->data(&e, n * STRIDE);
    unsigned long d = VBASE + x.vused;
    CHECK(p == vmem + x.vused, "data answered %p, wanted %p", (void *)p, (void *)(vmem + x.vused));
    x.vused += (n * STRIDE + 3) & ~3UL;
    o->emit(&e, VBASE + (unsigned long)(p - vmem), n, STRIDE, prim, tex, 0x00001300UL);
    old_emit(&x, d, n, prim, tex, 0x00001300UL);
}
static void both_fill(unsigned long raw) { o->fill(&e, 0, 0, 320, 240, raw); ogpu_fill(&x.b, 0, 0, 320, 240, raw); x.list_at = -1; }
static void both_clear(int kind, unsigned long v) { o->clear3d(&e, kind, 0, 0, 320, 240, v); ogpu_clear3d(&x.b, kind, 0, 0, 320, 240, v); x.list_at = -1; }

static void both_finish(const struct OGPUPipeS3DBuf *area, const char *what)
{
    UBYTE *s = 0;
    unsigned char *os = 0;
    LONG words = o->finish(&e, area, &s);
    long owords = old_finish(&x, area, &os);
    long i, bad = -1;
    CHECK(words == owords, "%s: %ld words, the old builder wrote %ld", what, (long)words, owords);
    CHECK(s - (UBYTE *)cmd == os - (unsigned char *)x.cmdmem, "%s: starts at byte %ld, the old one at %ld", what,
          (long)(s - (UBYTE *)cmd), (long)(os - (unsigned char *)x.cmdmem));
    if (words == owords)
        for (i = 0; i < words * 4; i++) if (s[i] != os[i]) { bad = i; break; }
    CHECK(bad < 0, "%s: differs in word %ld", what, bad / 4);
    CHECK(e.vused == x.vused, "%s: %lu bytes of data, the old builder %lu", what, (unsigned long)e.vused, (unsigned long)x.vused);
    CHECK(e.b.overflow == x.b.overflow, "%s: overflow %d, the old builder %d", what, e.b.overflow, x.b.overflow);
    CHECK(!e.body && e.list_at == -1, "%s: the batch still has a body after finish", what);
    printf("  %-44s %4ld words, %5lu bytes of data\n", what, (long)words, (unsigned long)e.vused);
    o->reset(&e);
    x.b.words = 0; x.vused = 0;
    CHECK(e.b.words == 0 && e.vused == 0, "%s: reset left something", what);
}

int main(void)
{
    ULONG i;
    UBYTE *s;
    LONG w;

    o = ogpu_pipe_s3d_ops(OGPU_PIPE_S3D_VERSION);
    CHECK(o != 0, "no table for version %d", OGPU_PIPE_S3D_VERSION);
    if (!o) return 1;
    CHECK(o->version == OGPU_PIPE_S3D_VERSION, "the table says version %lu", (unsigned long)o->version);
    CHECK(ogpu_pipe_s3d_ops(OGPU_PIPE_S3D_VERSION + 1) == 0, "a newer version than the pipe's was answered");
    o->init(&e, cmd, BATCH_WORDS, PREFIX_WORDS, vmem, VERT_BYTES);
    memset(&x, 0, sizeof x);
    x.list_at = -1;
    CHECK(!e.body && e.list_at == -1 && e.vused == 0, "init left a batch");

    /* 1. Warp3D's frame: depth and stencil, state, lists that join and lists that don't */
    both_begin(&Z, &ST, 1);
    both_begin(&Z, &ST, 0);
    both_clip(0, 0, 320, 240);
    both_texture(0x60000000UL, 64, 64, 4);
    both_render3d(1);
    both_draw(3, OGPU_LAY_LIST, OGPU_TRI_UNIT0);
    both_draw(6, OGPU_LAY_LIST, OGPU_TRI_UNIT0);       /* joins: 9 */
    both_draw(3, OGPU_LAY_LIST, OGPU_TRI_UNIT0);       /* joins: 12 */
    both_draw(5, OGPU_LAY_STRIP, OGPU_TRI_UNIT0);
    both_draw(3, OGPU_LAY_LIST, OGPU_TRI_UNIT0);       /* after a strip: a new list */
    both_draw(4, OGPU_LAY_FAN, OGPU_TRI_UNIT0);
    both_draw(0, OGPU_LAY_LIST, OGPU_TRI_UNIT0);       /* nothing */
    both_draw(3, OGPU_LAY_LIST, OGPU_TRI_UNIT0);
    both_render3d(2);
    both_draw(3, OGPU_LAY_LIST, OGPU_TRI_UNIT0);       /* state between: a new list */
    both_clip(10, 20, 100, 50);
    both_draw(3, OGPU_LAY_LIST, OGPU_TRI_UNIT0);
    both_texture(0, 0, 0, 1);
    both_draw(6, OGPU_LAY_LIST, OGPU_TRI_NOTEX);
    (void)o->data(&e, 10); x.vused += 12;              /* data that isn't the list's: the next list doesn't join */
    both_draw(3, OGPU_LAY_LIST, OGPU_TRI_NOTEX);
    both_draw(3, OGPU_LAY_LIST, OGPU_TRI_NOTEX);       /* joins */
    both_clip(0, 0, 0, 0);
    both_fill(0xFF102030UL);
    both_clip(0, 0, 0, 0);
    both_clear(OGPU_KIND_DEPTH, 0xFFFFFFFFUL);
    both_clear(OGPU_KIND_STENCIL, 0);
    both_finish(&AREA, "Warp3D: depth, stencil, lists, clears");

    /* 2. MiniGL's frame: depth alone, a clear first, a readback last, a 16-bit area */
    both_begin(&Z, 0, 1);
    both_clip(0, 0, 320, 240);
    both_fill(0x0000F81FUL);
    both_clear(OGPU_KIND_DEPTH, 0x7FFFFFFCUL);
    both_texture(0x61000000UL, 128, 32, 1);
    both_render3d(3);
    for (i = 0; i < 8; i++) both_draw(3, OGPU_LAY_LIST, OGPU_TRI_UNIT0);    /* one list of 24 */
    both_render3d(4);
    both_draw(4, OGPU_LAY_STRIP, OGPU_TRI_UNIT0);
    both_clear(OGPU_KIND_COLOUR | OGPU_KIND_READBACK, 0);
    both_finish(&AREA16, "MiniGL: depth, clear, one long list, readback");

    /* 3. no depth at all, and the second batch starts as the first did */
    both_begin(0, 0, 1);
    both_clip(0, 0, 320, 240);
    both_render3d(5);
    both_draw(3, OGPU_LAY_LIST, OGPU_TRI_NOTEX);
    both_finish(&AREA, "no depth buffer");

    /* 4. nothing drawn: SURFACE and TARGET alone (a present of what the host holds) */
    both_finish(&AREA, "an empty batch");

    /* 5. room, and the data area's end */
    CHECK(o->room(&e, BATCH_WORDS - PREFIX_WORDS, VERT_BYTES) == 1, "an empty batch has no room for a whole one");
    CHECK(o->room(&e, BATCH_WORDS - PREFIX_WORDS + 1, 0) == 0, "room for more words than the batch has");
    CHECK(o->room(&e, 0, VERT_BYTES + 1) == 0, "room for more data than the area has");
    both_begin(&Z, 0, 1);
    both_clip(0, 0, 320, 240);
    CHECK(o->room(&e, BATCH_WORDS - PREFIX_WORDS - e.b.words, 0) == 1, "the words left don't fit");
    CHECK(o->room(&e, BATCH_WORDS - PREFIX_WORDS - e.b.words + 1, 0) == 0, "one word too many fits");
    s = o->data(&e, 5);
    CHECK(s == vmem && e.vused == 8, "5 bytes of data took %lu", (unsigned long)e.vused);
    x.vused = 8;
    CHECK(o->room(&e, 0, VERT_BYTES - 8) == 1 && o->room(&e, 0, VERT_BYTES - 7) == 0, "the data left is counted wrong");
    CHECK(o->data(&e, VERT_BYTES - 7) == 0 && e.vused == 8, "data past the area's end was given");
    CHECK(o->data(&e, VERT_BYTES - 8) == vmem + 8 && e.vused == VERT_BYTES, "the area's last bytes were refused");
    x.vused = VERT_BYTES;
    both_finish(&AREA, "a full data area");

    /* 6. more commands than fit: left out and marked, as the builders do it */
    both_begin(&Z, &ST, 1);
    for (i = 0; i < 200; i++) both_render3d(i);
    CHECK(e.b.overflow, "an overfull batch isn't marked");
    both_finish(&AREA, "an overfull batch");
    both_begin(0, 0, 1);
    CHECK(!e.b.overflow, "the mark outlived its batch");
    both_clip(1, 2, 3, 4);
    w = e.b.words;
    both_finish(&AREA, "the batch after it");
    CHECK(w > 0, "CLIP wrote nothing");

    printf("test_s3d_same: %d checks, %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
