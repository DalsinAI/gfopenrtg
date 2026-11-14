/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's own per-vertex conversion (library/warp3d/w3d_vtx.h, as
 * convert() runs it with no fog factor in the vertex) against
 * hostlib_w3d_vertices (library/hostlib/w3d/vertices.c, the host build), on
 * the same generated vertices: arrays and pointer lists, fan pieces, every
 * texture scale, Gouraud and specular on and off. The host build reads the
 * 68k's memory big-endian, so the vertices are written to its arena that
 * way. Every output word must be the library's.
 *   tests/run_w3d_hlv.sh [cases] */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
/* include/Warp3D/Warp3D.h's vertex (it needs the Amiga's exec headers; its
 * 68k layout is tests/w3d_layout.c's), and the state bits used here */
typedef float W3D_Float;
typedef double W3D_Double;
typedef struct { W3D_Float r, g, b, a; } W3D_Color;
typedef struct { W3D_Float r, g, b; } W3D_ColorRGB;
typedef struct {
    W3D_Float x, y; W3D_Double z; W3D_Float w; W3D_Float u, v, tex3d;
    W3D_Color color; W3D_ColorRGB spec; W3D_Float l;
} W3D_Vertex;
_Static_assert(sizeof(W3D_Vertex) == 64, "W3D_Vertex is 64 bytes");
#define W3D_TEXMAPPING (1 << 8)
#define W3D_GOURAUD    (1 << 10)
#define W3D_SPECULAR   (1 << 23)
#include "../library/warp3d/w3d_conv.h"

/* w3d_internal.h's, which needs the Amiga's headers */
struct ovtx { LONG x, y; ULONG argb; LONG u, v; ULONG z; ULONG w; ULONG spec; };
W3D_CONV_FN ULONG w3d_colour(const W3D_Color *c)
{
    return (w3d_unit8(c->a) << 24) | (w3d_unit8(c->r) << 16) | (w3d_unit8(c->g) << 8) | w3d_unit8(c->b);
}
#include "../library/warp3d/w3d_vtx.h"

/* the host build of the hostlib function, over a 1 MiB guest arena */
#define ARENA (1u << 20)
#define GBASE 0x00200000u
static uint8_t *arena;
static uint8_t *hl_host_mem(void) { return arena - GBASE; }
static int hl_host_plain(uint32_t at, uint32_t n, int write)
{
    (void)write;
    return at >= GBASE && (uint64_t)at + n <= (uint64_t)GBASE + ARENA;
}
static int hl_host_aliased(uint32_t at, uint32_t n) { (void)at; (void)n; return 0; }
static void hl_host_written(uint32_t at, uint32_t n) { (void)at; (void)n; }
static _Thread_local uint8_t hl_test_state[65536];
uint8_t *hl_host_state_buf(void) { return hl_test_state; }
#define HL_HOST 1
#define HL_VENDORED 1
#include "../library/hostlib/hl.h"
void hl_host_parallel(struct hl_ctx *hl, uint32_t parts,
                      void (*fn)(struct hl_ctx *, hl_addr, uint32_t, uint32_t), hl_addr block)
{
    (void)hl; (void)parts; (void)fn; (void)block;
}
#include "../library/hostlib/w3d/vertices.c"

#define MAXV 600u
#define VTX_AT (GBASE + 4096u)
#define PTR_AT (GBASE + 4096u + MAXV * 64u)
#define OUT_AT (PTR_AT + MAXV * 4u)

static uint32_t rng = 0x5EED3Du;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t below(uint32_t n) { return n ? rnd() % n : 0; }
static uint8_t *G(uint32_t a) { return arena + (a - GBASE); }
static void wr32(uint32_t a, uint32_t v)
{
    uint8_t *p = G(a);
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint32_t rd32(uint32_t a)
{
    const uint8_t *p = G(a);
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static float bf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t fb(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static uint32_t fbits(uint32_t elo, uint32_t ehi)
{
    uint32_t r = below(40);
    if (r == 0) return 0x7fc00000u | below(0x400000);
    if (r == 1) return (rnd() & 0x80000000u) | 0x7f800000u;
    if (r == 2) return rnd() & 0x807fffffu;
    if (r == 3) return rnd();
    if (r == 4) return (elo + below(ehi - elo + 1)) << 23 | (below(2) ? 0x400000u : 0);
    return (below(3) == 0 ? 0x80000000u : 0) | (elo + below(ehi - elo + 1)) << 23 | below(0x800000);
}
static double dval(void)
{
    uint64_t b;
    double d;
    uint32_t r = below(24);
    if (r == 0) b = 0x7ff8000000000000ull;
    else if (r == 1) b = 0x3ff0000000000000ull;
    else if (r == 2) b = (uint64_t)(rnd() & 0x80000000u) << 32;
    else if (r == 3) b = 0x8000000000000000ull | (uint64_t)(1000u + below(30)) << 52 | ((uint64_t)rnd() << 20);
    else b = (uint64_t)(980u + below(48)) << 52 | ((uint64_t)below(0x100000) << 32) | rnd();
    memcpy(&d, &b, 8);
    return d;
}
static void mkvertex(W3D_Vertex *v)
{
    v->x = bf(fbits(110, 137)); v->y = bf(fbits(110, 137));
    v->z = dval();
    v->w = bf(fbits(100, 130));
    v->u = bf(fbits(110, 132)); v->v = bf(fbits(110, 132)); v->tex3d = bf(rnd());
    v->color.r = bf(fbits(112, 128)); v->color.g = bf(fbits(112, 128));
    v->color.b = bf(fbits(112, 128)); v->color.a = bf(fbits(112, 128));
    v->spec.r = bf(fbits(112, 128)); v->spec.g = bf(fbits(112, 128)); v->spec.b = bf(fbits(112, 128));
    v->l = bf(rnd());
}
static void put_vertex(uint32_t a, const W3D_Vertex *v)       /* the 68k's layout, big-endian */
{
    uint64_t z;
    memcpy(&z, &v->z, 8);
    wr32(a, fb(v->x)); wr32(a + 4, fb(v->y));
    wr32(a + 8, (uint32_t)(z >> 32)); wr32(a + 12, (uint32_t)z);
    wr32(a + 16, fb(v->w)); wr32(a + 20, fb(v->u)); wr32(a + 24, fb(v->v)); wr32(a + 28, fb(v->tex3d));
    wr32(a + 32, fb(v->color.r)); wr32(a + 36, fb(v->color.g)); wr32(a + 40, fb(v->color.b)); wr32(a + 44, fb(v->color.a));
    wr32(a + 48, fb(v->spec.r)); wr32(a + 52, fb(v->spec.g)); wr32(a + 56, fb(v->spec.b)); wr32(a + 60, fb(v->l));
}

/* convert() with no fog factor: w3d_vtx_core, then 255 << 24 and the specular colour */
static void library(ULONG state, const W3D_Color *cur, const W3D_Vertex *v, const struct uvs *s, struct ovtx *o)
{
    w3d_vtx_core(state, cur, v, s, w3d_zd(&v->z), 0, o);
    o->spec = (255UL << 24) | ((state & W3D_SPECULAR) ? (w3d_unit8(v->spec.r) << 16) | (w3d_unit8(v->spec.g) << 8) | w3d_unit8(v->spec.b) : 0);
}

int main(int argc, char **argv)
{
    static W3D_Vertex vs[MAXV];
    static const int ks[] = { UV_NONE, 16, 16, 8, 10, 13, 5, 0, -3, 32, -16 };
    long cases = argc > 1 ? atol(argv[1]) : 3000, c, verts = 0, lists = 0;
    arena = calloc(1, ARENA);
    if (!arena) return 2;
    for (c = 0; c < cases; c++) {
        uint32_t i, j, n = 3u + below(below(4) ? 30 : 400), list = below(2), first = 0, at = 0, slots, blk[HL_WV_LONGS];
        uint32_t pick[MAXV];
        ULONG state = (below(2) ? W3D_GOURAUD : 0) | (below(2) ? W3D_SPECULAR : 0) | (rnd() & W3D_TEXMAPPING);
        W3D_Color cur;
        struct uvs s;
        struct hl_ctx hl;
        uint32_t r;
        if (below(3) == 0) { at = below(100); first = below(2) ? 0 : at; }
        slots = at + n > first + 1 ? at + n : first + 1;
        if (slots > MAXV) { n = MAXV - at; slots = MAXV; }
        cur.r = bf(fbits(112, 128)); cur.g = bf(fbits(112, 128)); cur.b = bf(fbits(112, 128)); cur.a = bf(fbits(112, 128));
        s.ku = ks[below(sizeof ks / sizeof ks[0])]; s.kv = ks[below(sizeof ks / sizeof ks[0])];
        s.su = s.sv = 0.0f;
        for (i = 0; i < MAXV; i++) { mkvertex(&vs[i]); put_vertex(VTX_AT + i * 64u, &vs[i]); }
        for (i = 0; i < slots; i++) { pick[i] = below(MAXV); wr32(PTR_AT + 4u * i, VTX_AT + pick[i] * 64u); }
        blk[HL_WV_SRC] = list ? PTR_AT : VTX_AT;
        blk[HL_WV_LIST] = list;
        blk[HL_WV_FIRST] = first;
        blk[HL_WV_AT] = at;
        blk[HL_WV_COUNT] = n;
        blk[HL_WV_OUT] = OUT_AT;
        blk[HL_WV_FLAGS] = ((state & W3D_GOURAUD) ? HL_WV_GOURAUD : 0) | ((state & W3D_SPECULAR) ? HL_WV_SPECULAR : 0);
        blk[HL_WV_CURRENT] = w3d_colour(&cur);
        blk[HL_WV_KU] = (uint32_t)s.ku;
        blk[HL_WV_KV] = (uint32_t)s.kv;
        for (i = 0; i < HL_WV_LONGS; i++) wr32(GBASE + 4u * i, blk[i]);
        hl_init(&hl, 0);
        r = hostlib_w3d_vertices_host(&hl, GBASE);
        if (r != n || hl.why) {
            printf("FAIL case %ld: the hostlib call gave %u of %u (why %d)\n", c, r, n, hl.why);
            return 1;
        }
        for (j = 0; j < n; j++) {
            struct ovtx o;
            ULONG w[8];
            uint32_t k, idx = j ? at + j : first;
            library(state, &cur, list ? &vs[pick[idx]] : &vs[idx], &s, &o);
            w[0] = (ULONG)o.x; w[1] = (ULONG)o.y; w[2] = o.argb; w[3] = (ULONG)o.u; w[4] = (ULONG)o.v;
            w[5] = o.z; w[6] = o.w; w[7] = o.spec;
            for (k = 0; k < 8; k++)
                if (rd32(OUT_AT + j * 32u + 4u * k) != w[k]) {
                    printf("FAIL case %ld vertex %u word %u: hostlib %08x, Warp3D %08x\n", c, j, k,
                           rd32(OUT_AT + j * 32u + 4u * k), (unsigned)w[k]);
                    return 1;
                }
        }
        verts += n;
        lists += list;
    }
    printf("ok   Warp3D vertices: %ld primitives (%ld pointer lists), %ld vertices, hostlib_w3d_vertices = Warp3D's convert() word for word\n",
           cases, lists, verts);
    return 0;
}
