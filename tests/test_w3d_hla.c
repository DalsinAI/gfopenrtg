/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Warp3D.library's own DrawArray / DrawElements vertex reading
 * (library/warp3d/w3d_vtx.h's w3d_array_read and w3d_tri_elem, then
 * w3d_vtx_core, as arrays() and array_vertex() run them with no fog factor)
 * against hostlib_w3d_arrays (library/hostlib/w3d/vertices.c, the host
 * build), on the same generated arrays: lists, fans and strips, no indices
 * or 1, 2 and 4-byte ones, float and double depths, every colour mode,
 * texture coordinates with w before u, strides of 0, every texture scale,
 * Gouraud and specular on and off. The library reads its own copy of the
 * arrays; the host build reads the 68k's, big-endian. Every output word
 * must be the library's.
 *   tests/run_w3d_hlv.sh [cases] */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
typedef float W3D_Float;
typedef double W3D_Double;
typedef struct { W3D_Float r, g, b, a; } W3D_Color;
typedef struct { W3D_Float r, g, b; } W3D_ColorRGB;
typedef struct {
    W3D_Float x, y; W3D_Double z; W3D_Float w; W3D_Float u, v, tex3d;
    W3D_Color color; W3D_ColorRGB spec; W3D_Float l;
} W3D_Vertex;
#define W3D_GOURAUD    (1 << 10)
#define W3D_SPECULAR   (1 << 23)
#define W3D_VERTEX_F_F_F 0
#define W3D_VERTEX_F_F_D 1
#define W3D_COLOR_UBYTE  (0x02u << 30)
#define W3D_CMODE_RGB    0x01
#define W3D_CMODE_BGR    0x02
#define W3D_CMODE_RGBA   0x04
#define W3D_CMODE_ARGB   0x08
#define W3D_CMODE_BGRA   0x10
#include "../library/warp3d/w3d_conv.h"
struct ovtx { LONG x, y; ULONG argb; LONG u, v; ULONG z; ULONG w; ULONG spec; };
W3D_CONV_FN ULONG w3d_colour(const W3D_Color *c)
{
    return (w3d_unit8(c->a) << 24) | (w3d_unit8(c->r) << 16) | (w3d_unit8(c->g) << 8) | w3d_unit8(c->b);
}
#include "../library/warp3d/w3d_vtx.h"

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
#include "../library/hostlib/probe/probe.h"
#include "../library/hostlib/hl_ids.h"

#define SLOTS 512u
#define VP_AT 0x01000u
#define TP_AT 0x09010u
#define CP_AT 0x11000u
#define SP_AT 0x19000u
#define IX_AT 0x21000u
#define OUT_AT 0x24000u

static uint8_t nat[ARENA] __attribute__((aligned(16)));    /* the library's copy, native order */
static uint32_t rng = 0xA4A4A5u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint32_t below(uint32_t n) { return n ? rnd() % n : 0; }
static void be32(uint32_t off, uint32_t v)
{
    uint8_t *p = arena + off;
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint32_t rd32(uint32_t off)
{
    const uint8_t *p = arena + off;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void put32(uint32_t off, uint32_t v) { memcpy(nat + off, &v, 4); be32(off, v); }
static void put16(uint32_t off, uint32_t v)
{
    uint16_t h = (uint16_t)v;
    memcpy(nat + off, &h, 2);
    arena[off] = (uint8_t)(v >> 8); arena[off + 1] = (uint8_t)v;
}
static void put8(uint32_t off, uint32_t v) { nat[off] = arena[off] = (uint8_t)v; }
static void putd(uint32_t off, uint32_t hi, uint32_t lo)
{
    uint64_t b = (uint64_t)hi << 32 | lo;
    memcpy(nat + off, &b, 8);
    be32(off, hi); be32(off + 4, lo);
}
static float bf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
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
static uint32_t zbits(void) { return below(4) ? (100u + below(27)) << 23 | below(0x800000) : fbits(100, 130); }
static void dbits(uint32_t *hi, uint32_t *lo)
{
    uint32_t r = below(24);
    *lo = rnd();
    if (r == 0) { *hi = 0x7ff80000u | below(0x80000); return; }
    if (r == 1) { *hi = 0x3ff00000u; *lo = 0; return; }
    if (r == 2) { *hi = rnd() & 0x80000000u; *lo = 0; return; }
    if (r == 3) { *hi = 0x80000000u | (1000u + below(30)) << 20 | below(0x100000); return; }
    *hi = (980u + below(48)) << 20 | below(0x100000);
}
static const ULONG modes[] = { W3D_CMODE_RGBA, W3D_CMODE_ARGB, W3D_CMODE_BGRA, W3D_CMODE_RGB, W3D_CMODE_BGR };
static ULONG cmode(void) { return modes[below(5)] | (below(2) ? W3D_COLOR_UBYTE : 0); }
static void colours(uint32_t at, uint32_t stride, ULONG mode, uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++)
        for (k = 0; k < 4; k++) {
            if (mode & W3D_COLOR_UBYTE) put8(at + i * stride + k, rnd());
            else put32(at + i * stride + 4 * k, fbits(112, 128));
        }
}

int main(int argc, char **argv)
{
    static const int ks[] = { UV_NONE, 16, 16, 8, 10, 13, 5, 0, -3, 32, -16 };
    long cases = argc > 1 ? atol(argv[1]) : 3000, c, verts = 0, indexed = 0, dbl = 0, kinds[3] = { 0, 0, 0 };
    /* the probe gate (w3d_lib.c): only "runs as host code" from a runtime with the probe */
    if (w3d_hl_hosted(0, 0) || w3d_hl_hosted(0, HL_PROBE_HOST) || w3d_hl_hosted(0, HL_PROBE_HOST | HL_PROBE_TOTAL) ||
        w3d_hl_hosted(2, 0) || w3d_hl_hosted(2, HL_PROBE_TOTAL) || !w3d_hl_hosted(2, HL_PROBE_HOST) ||
        !w3d_hl_hosted(2, HL_PROBE_HOST | HL_PROBE_TOTAL) || W3D_HL_ARRAYS_VERSION != 1u || HL_ID_W3D_ARRAYS != 0x00013001u) {
        printf("FAIL: w3d_hl_hosted / W3D_ARRAYS id or version\n");
        return 1;
    }
    if (w3d_hl_cmode(W3D_CMODE_RGBA) != HL_WA_C_RGBA || w3d_hl_cmode(W3D_CMODE_ARGB) != HL_WA_C_ARGB ||
        w3d_hl_cmode(W3D_CMODE_BGRA) != HL_WA_C_BGRA || w3d_hl_cmode(W3D_CMODE_RGB) != HL_WA_C_RGB ||
        w3d_hl_cmode(W3D_CMODE_BGR | W3D_COLOR_UBYTE) != (HL_WA_C_BGR | HL_WA_C_UBYTE) ||
        W3D_TRI_LIST != HL_WA_LIST || W3D_TRI_FAN != HL_WA_FAN || W3D_TRI_STRIP != HL_WA_STRIP) {
        printf("FAIL Warp3D arrays: w3d_vtx.h's modes are not arrays.h's\n");
        return 1;
    }
    arena = calloc(1, ARENA);
    if (!arena) return 2;
    for (c = 0; c < cases; c++) {
        int kind = (int)below(3), vpmode = below(4) == 0, packed;
        uint32_t i, j, k, ntri, first = below(4) ? 0 : below(40), base = below(2) ? 0 : below(50), sz = 0, blk[HL_WA_LONGS], r;
        uint32_t vps, tps, cps, sps, ne, top;
        int32_t voff = 4, woff = 8;
        ULONG state = (below(2) ? W3D_GOURAUD : 0) | (below(2) ? W3D_SPECULAR : 0), cm = cmode(), sm = cmode();
        W3D_Color cur;
        struct uvs s;
        struct w3d_arrs a;
        struct hl_ctx hl;
        vps = vpmode ? 16 + 8 * below(4) : 12 + 4 * below(6);
        tps = 12 + 4 * below(5); cps = 16 + 4 * below(5); sps = 16 + 4 * below(5);
        if (below(4) == 0) { voff = 4 + 4 * (int32_t)below(3); woff = -4 * (int32_t)below(3); }
        if (below(6) == 0) vps = tps = cps = sps = 0;
        a.vp = nat + VP_AT; a.vps = (LONG)vps; a.vpmode = vpmode ? W3D_VERTEX_F_F_D : W3D_VERTEX_F_F_F;
        a.tp = below(4) ? nat + TP_AT : 0; a.tps = (LONG)tps; a.tpv = voff; a.tpw = woff;
        a.cp = below(4) ? nat + CP_AT : 0; a.cps = (LONG)cps; a.cpmode = cm;
        a.scp = below(3) ? nat + SP_AT : 0; a.scps = (LONG)sps; a.scpmode = sm;
        for (i = 0; i < SLOTS; i++) {
            uint32_t o = VP_AT + i * (vps ? vps : 16), hi, lo;
            put32(o, fbits(110, 137)); put32(o + 4, fbits(110, 137));
            if (vpmode) { dbits(&hi, &lo); putd(o + 8, hi, lo); } else put32(o + 8, zbits());
            o = TP_AT + i * (tps ? tps : 16);
            put32(o, fbits(110, 132)); put32(o + (uint32_t)voff, fbits(110, 132)); put32(o + (uint32_t)woff, fbits(100, 130));
            if (!vps) break;
        }
        colours(CP_AT, cps ? cps : 16, cm, vps ? SLOTS : 1);
        colours(SP_AT, sps ? sps : 16, sm, vps ? SLOTS : 1);
        ntri = 1u + below(below(4) ? 30 : 300);
        if (below(3)) {
            static const uint32_t szs[] = { 1, 2, 4 };
            sz = szs[below(3)];
            ne = kind == W3D_TRI_LIST ? 3 * (first + ntri) : first + ntri + 2;
            top = SLOTS - base;
            if (sz == 1 && top > 256) top = 256;
            for (i = 0; i < ne; i++) {
                uint32_t v = vps ? below(top) : 0;
                if (sz == 1) put8(IX_AT + i, v); else if (sz == 2) put16(IX_AT + 2 * i, v); else put32(IX_AT + 4 * i, v);
            }
        } else if (vps) {
            uint32_t room = SLOTS - base;
            if (kind == W3D_TRI_LIST) { if (3 * (first + ntri) > room) { first = 0; ntri = room / 3; } }
            else if (first + ntri + 2 > room) { first = 0; ntri = room - 2; }
        }
        cur.r = bf(fbits(112, 128)); cur.g = bf(fbits(112, 128)); cur.b = bf(fbits(112, 128)); cur.a = bf(fbits(112, 128));
        s.ku = ks[below(sizeof ks / sizeof ks[0])]; s.kv = ks[below(sizeof ks / sizeof ks[0])];
        s.su = s.sv = 0.0f;
        /* hl_arrays()'s block (w3d_draw.c) */
        blk[HL_WA_VP] = GBASE + VP_AT; blk[HL_WA_VPSTRIDE] = vps; blk[HL_WA_VPMODE] = vpmode ? HL_WA_VP_FFD : HL_WA_VP_FFF;
        blk[HL_WA_TP] = a.tp ? GBASE + TP_AT : 0; blk[HL_WA_TPSTRIDE] = tps;
        blk[HL_WA_TPVOFF] = (uint32_t)voff; blk[HL_WA_TPWOFF] = (uint32_t)woff;
        blk[HL_WA_CP] = a.cp ? GBASE + CP_AT : 0; blk[HL_WA_CPSTRIDE] = cps; blk[HL_WA_CPMODE] = w3d_hl_cmode(cm);
        blk[HL_WA_SCP] = a.scp ? GBASE + SP_AT : 0; blk[HL_WA_SCPSTRIDE] = sps; blk[HL_WA_SCPMODE] = w3d_hl_cmode(sm);
        blk[HL_WA_IX] = sz ? GBASE + IX_AT : 0; blk[HL_WA_IXSIZE] = sz;
        blk[HL_WA_BASE] = base; blk[HL_WA_PRIM] = (uint32_t)kind; blk[HL_WA_FIRST] = first; blk[HL_WA_NTRI] = ntri;
        blk[HL_WA_OUT] = GBASE + OUT_AT;
        blk[HL_WA_FLAGS] = ((state & W3D_GOURAUD) ? HL_WV_GOURAUD : 0) | ((state & W3D_SPECULAR) ? HL_WV_SPECULAR : 0);
        blk[HL_WA_CURRENT] = w3d_colour(&cur);
        blk[HL_WA_KU] = (uint32_t)s.ku; blk[HL_WA_KV] = (uint32_t)s.kv;
        for (i = 0; i < HL_WA_LONGS; i++) be32(4u * i, blk[i]);
        hl_init(&hl, 0);
        r = hostlib_w3d_arrays_host(&hl, GBASE);
        if (r != 3 * ntri || hl.why) {
            printf("FAIL case %ld: the hostlib call gave %u of %u (why %d)\n", c, r, 3 * ntri, hl.why);
            return 1;
        }
        for (j = 0; j < 3 * ntri; j++) {
            W3D_Vertex v;
            struct ovtx o;
            ULONG w[8], argb, z, e = w3d_tri_elem(kind, first + j / 3, j % 3), vi;
            if (!sz) vi = e;
            else if (sz == 1) vi = nat[IX_AT + e];
            else if (sz == 2) { uint16_t h; memcpy(&h, nat + IX_AT + 2 * e, 2); vi = h; }
            else memcpy(&vi, nat + IX_AT + 4 * e, 4);
            z = w3d_array_read(&a, base + vi, 0, &cur, &v, &argb, &packed);
            w3d_vtx_core(state, &cur, &v, &s, z, packed ? &argb : 0, &o);
            o.spec = (255UL << 24) | ((state & W3D_SPECULAR) ? (w3d_unit8(v.spec.r) << 16) | (w3d_unit8(v.spec.g) << 8) | w3d_unit8(v.spec.b) : 0);
            w[0] = (ULONG)o.x; w[1] = (ULONG)o.y; w[2] = o.argb; w[3] = (ULONG)o.u; w[4] = (ULONG)o.v;
            w[5] = o.z; w[6] = o.w; w[7] = o.spec;
            for (k = 0; k < 8; k++)
                if (rd32(OUT_AT + j * 32u + 4u * k) != w[k]) {
                    printf("FAIL case %ld vertex %u word %u: hostlib %08x, Warp3D %08x\n", c, j, k,
                           rd32(OUT_AT + j * 32u + 4u * k), (unsigned)w[k]);
                    return 1;
                }
        }
        verts += 3 * ntri;
        kinds[kind]++;
        indexed += sz != 0;
        dbl += vpmode;
    }
    printf("ok   Warp3D arrays: %ld runs (%ld lists, %ld fans, %ld strips; %ld indexed, %ld double depths), %ld vertices, hostlib_w3d_arrays = Warp3D's array_vertex() word for word\n",
           cases, kinds[0], kinds[1], kinds[2], indexed, dbl, verts);
    return 0;
}
