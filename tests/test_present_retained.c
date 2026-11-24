/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ORTG_Present's RETAINED route (library/present_retained.c, gfopenrtg.library
 * 0.31) on the PC: the GPU path's batch run by GFOpenGPU's CPU core into a
 * target in memory, the CPU path's writes caught. Checks the picture, the
 * tiles each present uploads, the stats, the fall back to the CPU when the
 * GPU fails the batch, OGPU_RSF_SOFTWARE, an INDEX8 surface as colours and as
 * pens, the crop, and a window whose pieces are all hidden. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../library/opengpu/ogpu_core.h"
#include "../library/present_retained.h"

#define ARENA (4u << 20)
static ogpu_u8 *arena;
static unsigned long used;
static int checks, failures, fail_submit, submits;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* addresses in the batch are the arena's, cut to 32 bits as on the Amiga */
static ogpu_u8 *map(void *user, ogpu_u32 address, ogpu_u32 length)
{
    uint32_t at = (uint32_t)address - (uint32_t)(uintptr_t)arena;   /* ogpu_u32 may be wider than 32 bits */
    (void)user;
    if (at >= ARENA || length > ARENA - at) return 0;
    return arena + at;
}

void *rsp_alloc(ULONG bytes)
{
    void *p = arena + used;
    used = (used + bytes + 15) & ~15UL;
    return used <= ARENA ? p : NULL;
}
void rsp_free(void *p) { (void)p; }
LONG rsp_submit(const void *buf, ULONG words)
{
    struct ogpu_core c;
    submits++;
    if (fail_submit) return -1;
    memset(&c, 0, sizeof c);
    ogpu_core_init(&c);
    c.map = map;
    ogpu_core_run(&c, buf, (long)words);
    return c.last_error;
}

/* the CPU path's writes */
static struct { long x, y, sx, sy, w, h; unsigned long format; const unsigned long *ctable; } wr[4096];
static int nwr;
LONG rsp_write(struct RastPort *rp, LONG x, LONG y, const struct OpenRTGPixels *px)
{
    (void)rp;
    if (nwr < 4096) {
        wr[nwr].x = x; wr[nwr].y = y; wr[nwr].sx = px->x; wr[nwr].sy = px->y; wr[nwr].w = px->width; wr[nwr].h = px->height;
        wr[nwr].format = px->format; wr[nwr].ctable = px->ctable;
    }
    nwr++;
    return px->width * px->height;
}

static unsigned long rd32(const ogpu_u8 *p) { return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3]; }
static void wr32(ogpu_u8 *p, unsigned long v) { p[0] = (ogpu_u8)(v >> 24); p[1] = (ogpu_u8)(v >> 16); p[2] = (ogpu_u8)(v >> 8); p[3] = (ogpu_u8)v; }

/* a surface as OGPU_RetainedCreate makes one */
static struct rs_handle *make(int w, int h, ULONG format, ULONG flags)
{
    struct rs_handle *s = rsp_alloc(sizeof *s);
    int bpp = format == OGPU_FMT_INDEX8 ? 1 : 4, i;
    ULONG bpr = ((ULONG)w * (ULONG)bpp + 15) & ~15UL;
    memset(s, 0, sizeof *s);
    if (format == OGPU_FMT_INDEX8) {
        s->pub.rs_Palette = rsp_alloc(256 * 4);
        for (i = 0; i < 256; i++) wr32((ogpu_u8 *)&s->pub.rs_Palette[i], 0xFF000000UL | (ULONG)i * 0x010203UL);
    }
    ogpu_rs_init(&s->core, NULL, (long)bpr, w, h, bpp, rsp_alloc((ULONG)ogpu_rs_mem(w, h)));
    s->pub.rs_Pixels = rsp_alloc(bpr * (ULONG)h);
    s->core.pixels = s->pub.rs_Pixels;
    s->pub.rs_BytesPerRow = bpr;
    s->pub.rs_Width = (UWORD)w; s->pub.rs_Height = (UWORD)h;
    s->pub.rs_Format = format; s->pub.rs_Flags = flags;
    s->pal_changed = format == OGPU_FMT_INDEX8;
    s->st.tiles = (ULONG)s->core.ntiles;
    return s;
}

static unsigned long pat(int x, int y, int k) { return 0xFF000000UL | (unsigned long)((x * 5 + y * 3 + k * 77) & 0xFFFFFF); }
static void draw(struct rs_handle *s, int k)
{
    int x, y;
    for (y = 0; y < s->pub.rs_Height; y++)
        for (x = 0; x < s->pub.rs_Width; x++)
            wr32((ogpu_u8 *)s->pub.rs_Pixels + y * s->pub.rs_BytesPerRow + x * 4, pat(x, y, k));
}

int main(void)
{
    static struct RastPort rp;
    static struct BitMap bm;
    struct rsp_target t;
    struct rs_handle *s;
    ogpu_u8 *dst;
    int x, y, bad;
    LONG n;
    arena = malloc(ARENA);
    if ((uintptr_t)arena >> 32 != ((uintptr_t)arena + ARENA - 1) >> 32) { printf("arena straddles 4 GB, try again\n"); return 0; }
    rp.BitMap = &bm;
    dst = rsp_alloc(64 * 64 * 4);
    memset(&t, 0, sizeof t);
    t.rp = &rp; t.truecolour = 1; t.gpu = 1;
    t.addr = (unsigned long)(uintptr_t)dst; t.bpr = 64 * 4; t.w = 64; t.h = 64; t.dfmt = OGPU_FMT_XRGB32;

    /* the GPU path: the whole surface at the first present, the picture right */
    s = make(64, 64, OGPU_FMT_XRGB32, 0);
    draw(s, 1);
    n = rsp_present(s, &t, 0, 0, 64, 64);
    CHECK(n == 4, "first GPU present uploads all 4 tiles: %ld", (long)n);
    for (bad = 0, y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            if ((rd32(dst + y * 256 + x * 4) & 0xFFFFFF) != (pat(x, y, 1) & 0xFFFFFF)) bad++;
    CHECK(bad == 0, "GPU picture: %d pixels wrong", bad);
    n = rsp_present(s, &t, 0, 0, 64, 64);
    CHECK(n == 0, "no change: 0 tiles (%ld)", (long)n);
    wr32((ogpu_u8 *)s->pub.rs_Pixels + 40 * s->pub.rs_BytesPerRow + 40 * 4, 0xFF123456UL);
    n = rsp_present(s, &t, 0, 0, 64, 64);
    CHECK(n == 1, "one pixel changed: 1 tile (%ld)", (long)n);
    CHECK((rd32(dst + 40 * 256 + 40 * 4) & 0xFFFFFF) == 0x123456, "the changed pixel on the target: %08lx", rd32(dst + 40 * 256 + 40 * 4));
    CHECK(s->st.gpu_frames == 3 && s->st.cpu_frames == 0 && s->path == RS_GPU, "stats: gpu %lu cpu %lu",
          (unsigned long)s->st.gpu_frames, (unsigned long)s->st.cpu_frames);

    /* a batch the GPU fails: shown the CPU way at once, every tile */
    fail_submit = 1; nwr = 0;
    n = rsp_present(s, &t, 0, 0, 64, 64);
    CHECK(n == 4 && nwr > 0 && s->path == RS_CPU && s->st.cpu_frames == 1, "GPU failed: CPU path, %ld tiles, %d writes", (long)n, nwr);
    CHECK(!s->tex_valid, "the texture is not trusted after a failed batch");
    fail_submit = 0;
    n = rsp_present(s, &t, 0, 0, 64, 64);
    CHECK(n == 4 && s->path == RS_GPU, "back on the GPU: all tiles again (%ld)", (long)n);

    /* OGPU_RSF_SOFTWARE: never the GPU */
    s = make(64, 64, OGPU_FMT_ARGB32, OGPU_RSF_SOFTWARE);
    draw(s, 2);
    submits = 0; nwr = 0;
    n = rsp_present(s, &t, 10, 20, 64, 64);
    CHECK(n == 4 && submits == 0 && nwr > 0 && wr[0].format == ORTG_PIX_ARGB, "SOFTWARE: CPU, ARGB (%ld, %d submits)", (long)n, submits);
    CHECK(wr[0].x == 10 + wr[0].sx && wr[0].y == 20 + wr[0].sy, "CPU writes at the corner");
    nwr = 0;
    n = rsp_present(s, &t, 10, 20, 64, 64);
    CHECK(n == 0 && nwr == 0, "CPU, no change: nothing written (%ld, %d)", (long)n, nwr);
    nwr = 0;
    n = rsp_present(s, &t, 11, 20, 64, 64);
    CHECK(n == 4, "CPU, moved: every tile again (%ld)", (long)n);

    /* the CPU path crops to the rectangle */
    nwr = 0;
    n = rsp_present(s, &t, 0, 0, 40, 64);
    {
        int i, over = 0;
        for (i = 0; i < nwr && i < 4096; i++) if (wr[i].sx + wr[i].w > 40) over++;
        CHECK(nwr > 0 && over == 0, "crop: %d writes past x 40", over);
    }

    /* INDEX8: colours through its palette on a true-colour target, pens on an 8-bit one */
    s = make(32, 32, OGPU_FMT_INDEX8, OGPU_RSF_SOFTWARE);
    nwr = 0;
    rsp_present(s, &t, 0, 0, 32, 32);
    CHECK(nwr > 0 && wr[0].format == ORTG_PIX_INDEX && wr[0].ctable == (const unsigned long *)s->pub.rs_Palette, "INDEX8 as colours");
    s = make(32, 32, OGPU_FMT_INDEX8, OGPU_RSF_SOFTWARE);   /* another surface: an 8-bit screen's */
    t.truecolour = 0; nwr = 0;
    rsp_present(s, &t, 0, 0, 32, 32);
    CHECK(nwr > 0 && wr[0].format == ORTG_PIX_PEN && wr[0].ctable == NULL, "INDEX8 as pens");
    t.truecolour = 1;

    /* a window with nothing showing: the tiles go up, nothing is drawn */
    s = make(64, 64, OGPU_FMT_XRGB32, 0);
    draw(s, 3);
    memset(dst, 0x55, 64 * 64 * 4);
    t.nclips = -1;
    n = rsp_present(s, &t, 0, 0, 64, 64);
    CHECK(n == 4 && rd32(dst) == 0x55555555UL, "hidden window: target untouched (%ld)", (long)n);
    t.nclips = 0;

    /* arguments */
    CHECK(rsp_present(s, &t, 0, 0, 0, 64) == OGPU_RS_ERR_ARGS, "an empty rectangle");
    CHECK(rsp_present(NULL, &t, 0, 0, 64, 64) == OGPU_RS_ERR_ARGS, "no surface");

    printf("test_present_retained: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
