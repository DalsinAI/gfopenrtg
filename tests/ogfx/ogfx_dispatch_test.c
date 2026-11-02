/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GFOpenGfx (library/ogfx/ogfx_lib.c) with and without graphics.library
 * 47.101's drawing dispatch and 47.102's provider chain, under qemu-m68k (test_ogfx_dispatch.sh builds
 * it with a Linux m68k GCC, once as the disk build and once as the ROM build,
 * -DOGPU_ROM_NO_OGFX).
 *
 * exec, graphics.library and expansion.library are stand-ins: a jump table
 * whose every slot is JSR fake_entry, so a call through any LVO lands in
 * fake_call with the registers as they came (and, for a plain C call through
 * a vector, its arguments on the stack). graphics.library is 47.102 with a
 * jump table to -1098 (the dispatch with its provider chain), 47.101 with a
 * jump table to -1092 (the dispatch, one provider), 47.100, or 47.101 with a
 * table that stops at -1056 (no dispatch); the dispatch's calls answer as
 * gfopengfx's library/graphics/gfx_dispatch.c does.
 *
 * With the chain (47.102): the planar fast paths always go in at the
 * library's start, with GFX_RegisterProviderPri at GFXDISPATCH_PRI_FAST,
 * after a provider already in (GFOpenRTG's, at GFXDISPATCH_PRI_RTG); a
 * program's OGFX_RegisterProvider goes in at the default priority beside
 * them, and taking it out leaves them as they were.
 * With the dispatch of 47.101: the planar fast paths are offered to
 * graphics.library at the library's start (only when no provider is in, and
 * only with graphics.library up), OGFX_Version, Status, Register/UnregisterProvider
 * and Register/UnregisterLook go to -1062 ... -1092 with their argument in
 * A0, InstallPatches patches nothing and answers 1, SetEnabled answers 1 and
 * changes nothing, the drawing LVOs call graphics.library's own vectors, and
 * the fast paths' RectFill draws. Without it: the ROM build's registry
 * refuses and it patches nothing; the disk build's keeps the provider and
 * patches all 28 vectors, as before. */
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "../../library/ogfx/ogfx.h"
#include <graphics/gfxdispatch.h>

/* ---- the stand-in libraries ------------------------------------------------- */

#define SLOTS 200                      /* LVOs to -1200 */
enum { L_EXEC, L_GFX, L_EXP, L_COUNT };

struct fake {
    UWORD jt[SLOTS * 3];
    union { struct Library lib; struct ExecBase exec; struct GfxBase gfx; } u;
};

static struct fake *libs;              /* mmap()ed: its jump tables are code (qemu-m68k keeps to PROT_EXEC) */

void fake_entry(void);
__asm__(
    "    .text\n"
    "    .globl fake_entry\n"
    "fake_entry:\n"
    "    movem.l %d0-%d7/%a0-%a6,-(%sp)\n"
    "    move.l %sp,-(%sp)\n"
    "    jsr fake_call\n"
    "    addq.l #4,%sp\n"
    "    move.l %d0,(%sp)\n"
    "    movem.l (%sp)+,%d0-%d7/%a0-%a6\n"
    "    addq.l #4,%sp\n"
    "    rts\n");

struct call { int lib; LONG lvo; ULONG d0, d1, a0, a1; ULONG s[8]; };
static struct call calls[256];
static int ncalls;

/* the scene */
static int gfx_present, gfx_up, board_present;
static UWORD gfx_version, gfx_revision, gfx_negsize;
static int gp_have;                    /* graphics.library's dispatch: a provider in */
static struct OGFXProviderAll gp;      /* ... its record, as gfx_dispatch.c copies it */
/* 47.102's chain: the records in order, highest priority first, equals as they came */
static struct { struct OGFXProviderAll rec; LONG pri; } gch[4];
static int gn;

static ULONG chain_reg(struct OGFXProviderV1 *p, LONG pri)
{
    ULONG size, k;
    int i, at;
    if (!p || p->abi != OGFX_PROVIDER_ABI_V1 || p->size < OGFX_PROVIDER_V1_0_SIZE || !p->owner) return 0;
    pri = (BYTE)pri;
    for (i = 0; i < gn; i++)
        if (gch[i].rec.v1.owner == p->owner) {          /* its own entry: out, then in again */
            memmove(&gch[i], &gch[i + 1], sizeof gch[0] * (size_t)(gn - i - 1));
            gn--;
            break;
        }
    if (gn == 4) return 0;
    for (at = 0; at < gn && gch[at].pri >= pri; at++) ;
    memmove(&gch[at + 1], &gch[at], sizeof gch[0] * (size_t)(gn - at));
    size = p->size < sizeof gch[0].rec ? p->size : sizeof gch[0].rec;
    for (k = 0; k < sizeof gch[0].rec; k++) ((UBYTE *)&gch[at].rec)[k] = k < size ? ((UBYTE *)p)[k] : 0;
    gch[at].pri = pri;
    gn++;
    return 1;
}
static ULONG fast_ram[64];             /* where TypeOfMem says Fast RAM */

static void fake_lib(int id, UWORD version, UWORD revision, UWORD negsize)
{
    struct fake *f = &libs[id];
    int k;
    memset(f, 0, sizeof *f);
    for (k = 0; k < SLOTS; k++) {
        ULONG to = (ULONG)fake_entry;
        f->jt[k * 3] = 0x4EB9;         /* JSR fake_entry */
        f->jt[k * 3 + 1] = (UWORD)(to >> 16);
        f->jt[k * 3 + 2] = (UWORD)to;
    }
    f->u.lib.lib_Version = version;
    f->u.lib.lib_Revision = revision;
    f->u.lib.lib_NegSize = negsize;
    f->u.lib.lib_PosSize = sizeof f->u;
}

#define BASE(id) ((APTR)&libs[id].u)

ULONG fake_call(ULONG *r);
ULONG fake_call(ULONG *r)
{
    ULONG slot = r[15] - 6;
    int id = -1, i;
    LONG lvo = 0;
    struct call *c;
    for (i = 0; i < L_COUNT; i++)
        if (slot >= (ULONG)libs[i].jt && slot < (ULONG)&libs[i].u) {
            id = i;
            lvo = (LONG)slot - (LONG)(ULONG)&libs[i].u;
        }
    if (id < 0) { printf("FAIL: a call into nothing at %08lx\n", (unsigned long)slot); return 0; }
    if (ncalls < 256) {
        c = &calls[ncalls++];
        c->lib = id; c->lvo = lvo; c->d0 = r[0]; c->d1 = r[1]; c->a0 = r[8]; c->a1 = r[9];
        for (i = 0; i < 8; i++) c->s[i] = r[17 + i];
    }
    if (id == L_EXEC) switch (lvo) {
        case -552:                     /* OpenLibrary(a1 name, d0 version) */
            if (!strcmp((char *)r[9], "graphics.library")) return gfx_present ? (ULONG)BASE(L_GFX) : 0;
            if (!strcmp((char *)r[9], "expansion.library")) return (ULONG)BASE(L_EXP);
            return 0;
        case -276:                     /* FindName(a0 list, a1 name) */
            return !strcmp((char *)r[9], "graphics.library") && gfx_up ? (ULONG)BASE(L_GFX) : 0;
        case -534:                     /* TypeOfMem(a1) */
            return 0;                  /* Fast RAM, everywhere */
        case -420:                     /* SetFunction(a1 library, a0 offset, d0 function) */
            return 0x1000 + (UWORD)(WORD)r[8];
        default: return 0;             /* Forbid, Permit, CloseLibrary, CacheClearU */
    }
    if (id == L_EXP)
        return lvo == -72 && board_present ? (ULONG)fast_ram : 0;   /* FindConfigDev */
    switch (lvo) {                     /* graphics.library */
    case -1062: return gfx_revision >= 102 ? 0x00010006 : 0x00010005;   /* GFX_DispatchVersion */
    case -1098:                        /* GFX_RegisterProviderPri(a0, d0), 47.102 */
        return gfx_revision >= 102 ? chain_reg((struct OGFXProviderV1 *)r[8], (LONG)r[0]) : 77;
    case -1068: {                      /* GFX_RegisterProvider(a0) */
        struct OGFXProviderV1 *p = (struct OGFXProviderV1 *)r[8];
        ULONG size, k;
        if (gfx_revision >= 102) return chain_reg(p, 0);
        if (!p || p->abi != OGFX_PROVIDER_ABI_V1 || p->size < OGFX_PROVIDER_V1_0_SIZE || !p->owner) return 0;
        size = p->size < sizeof gp ? p->size : sizeof gp;
        for (k = 0; k < sizeof gp; k++) ((UBYTE *)&gp)[k] = k < size ? ((UBYTE *)p)[k] : 0;
        gp_have = 1;
        return 1;
    }
    case -1074:                        /* GFX_UnregisterProvider(a0) */
        if (gfx_revision >= 102) {
            for (i = 0; i < gn; i++)
                if (r[8] && gch[i].rec.v1.owner == (APTR)r[8]) {
                    memmove(&gch[i], &gch[i + 1], sizeof gch[0] * (size_t)(gn - i - 1));
                    gn--;
                    return 1;
                }
            return 0;
        }
        if (gp_have && r[8] && gp.v1.owner == (APTR)r[8]) { memset(&gp, 0, sizeof gp); gp_have = 0; return 1; }
        return 0;
    case -1080: return 1;              /* GFX_RegisterLook(a0) */
    case -1086: return 1;              /* GFX_UnregisterLook(a0) */
    case -1092:                        /* GFX_DispatchStatus */
        return OGFX_STATUS_PATCHED | OGFX_STATUS_ENABLED | (gp_have || gn ? OGFX_STATUS_PROVIDER : 0);
    case -960: {                       /* GetBitMapAttr(a0, d1) */
        struct BitMap *bm = (struct BitMap *)r[8];
        switch (r[1]) {
        case BMA_HEIGHT: return bm->Rows;
        case BMA_DEPTH: return bm->Depth;
        case BMA_WIDTH: return (ULONG)bm->BytesPerRow * 8;
        case BMA_FLAGS: return BMF_STANDARD;
        }
        return 0;
    }
    default: return 77;                /* a drawing vector, WaitBlit, ... */
    }
}

/* ---- gfopengpu.library's base, as opengpu_lib.c lays it out for GFOpenGfx ---- */

struct gpu { struct Library lib; struct ogfx_state st; };
const ULONG ogpu_ogfx_at = offsetof(struct gpu, st);
static struct gpu gpu;
#define B ((struct Library *)&gpu)
#define ST (&gpu.st)

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void scene(int present, int up, UWORD version, UWORD revision, UWORD negsize, int board)
{
    fake_lib(L_EXEC, 40, 0, SLOTS * 6);
    fake_lib(L_GFX, version, revision, negsize);
    fake_lib(L_EXP, 40, 0, SLOTS * 6);
    gfx_present = present; gfx_up = up; board_present = board;
    gfx_version = version; gfx_revision = revision; gfx_negsize = negsize;
    gp_have = 0; memset(&gp, 0, sizeof gp);
    gn = 0; memset(gch, 0, sizeof gch);
    memset(&gpu, 0, sizeof gpu);
    ncalls = 0;
}

static int count(int lib, LONG lvo)
{
    int i, n = 0;
    for (i = 0; i < ncalls; i++) if (calls[i].lib == lib && calls[i].lvo == lvo) n++;
    return n;
}

static struct call *last(int lib, LONG lvo)
{
    int i;
    for (i = ncalls - 1; i >= 0; i--) if (calls[i].lib == lib && calls[i].lvo == lvo) return &calls[i];
    return NULL;
}

static int dispatch_calls(void)
{
    return count(L_GFX, -1062) + count(L_GFX, -1068) + count(L_GFX, -1074) +
           count(L_GFX, -1080) + count(L_GFX, -1086) + count(L_GFX, -1092) + count(L_GFX, -1098);
}

static LONG rtg_rectfill(APTR u, struct OGFXRectFillRequest *r) { (void)u; (void)r; return 0; }
static LONG look_rectfill(APTR u, struct OGFXRectFillRequest *r) { (void)u; (void)r; return 0; }

/* The drawing LVOs reach graphics.library's vector of the call (lvo), with
 * the arguments as they came. */
static void drawing_lvos_forward(const char *how)
{
    struct RastPort rp;
    struct TextExtent te, ce;
    struct call *c;
    memset(&rp, 0, sizeof rp);
    ncalls = 0;
    OGFX_RectFill(&rp, 1, 2, 3, 4, B);
    c = last(L_GFX, -306);
    CHECK(c && c->s[0] == (ULONG)&rp && c->s[1] == 1 && c->s[2] == 2 && c->s[3] == 3 && c->s[4] == 4 &&
          c->s[5] == (ULONG)BASE(L_GFX), "%s: RectFill not to graphics.library's -306 as it came", how);
    CHECK(OGFX_Text(&rp, (STRPTR)"ab", 2, B) == 77 && count(L_GFX, -60) == 1, "%s: Text not -60's", how);
    CHECK(OGFX_TextLength(&rp, (STRPTR)"ab", 2, B) == 77 && count(L_GFX, -54) == 1, "%s: TextLength not -54's", how);
    OGFX_TextExtent(&rp, (STRPTR)"ab", 2, &te, B);
    CHECK(count(L_GFX, -690) == 1, "%s: TextExtent not -690", how);
    CHECK(OGFX_TextFit(&rp, (STRPTR)"ab", 2, &te, &ce, 1, 10, 10, B) == 77 && count(L_GFX, -696) == 1, "%s: TextFit not -696's", how);
    CHECK(OGFX_BltBitMap(NULL, 0, 0, NULL, 0, 0, 1, 1, 0xC0, 0xFF, NULL, B) == 77 && count(L_GFX, -30) == 1,
          "%s: BltBitMap not -30's", how);
    OGFX_BltTemplate(NULL, 0, 2, &rp, 0, 0, 1, 1, B);
    CHECK(count(L_GFX, -36) == 1, "%s: BltTemplate not -36", how);
    OGFX_ScrollRaster(&rp, 1, 0, 0, 0, 5, 5, B);
    CHECK(count(L_GFX, -396) == 1, "%s: ScrollRaster not -396", how);
    CHECK(count(L_EXEC, -420) == 0, "%s: a drawing LVO patched", how);
}

static void with_dispatch(void)
{
    struct OGFXProviderAll rtg;
    struct OGFXLookV1 look;
    struct call *c;
    UBYTE plane[32];
    struct BitMap bm;
    struct RastPort rp;
    struct OGFXRectFillRequest req;
    int i, ok;

    scene(1, 1, 47, 101, 1092, 1);
    ogfx_init(ST, &libs[L_EXEC].u.exec);
    /* at the start: the board looked for, the fast paths offered */
    CHECK(count(L_EXP, -72) == 1 && ST->amicradle, "the board not looked for at the start");
    CHECK(count(L_GFX, -1092) == 1 && count(L_GFX, -1068) == 1 && ST->fast_in, "the fast paths not offered at the start");
    CHECK(!ST->chain && count(L_GFX, -1098) == 0, "47.101 taken for a chain");
    CHECK(gp_have && gp.v1.owner == (APTR)ST && gp.v1.userdata == (APTR)ST && gp.v1.size == sizeof(struct OGFXProviderV1) &&
          gp.v1.rectfill && gp.v1.bltbitmap && gp.v1.scrollraster && !gp.v1.text && !gp.v1.blttemplate && !gp.bltpattern,
          "the fast paths' record: owner %p size %lu", gp.v1.owner, (unsigned long)gp.v1.size);
    CHECK(!ogfx_may_expunge(ST), "may expunge with the fast paths in graphics.library");

    /* the fast paths' RectFill draws a Fast RAM planar bitmap */
    memset(plane, 0, sizeof plane);
    memset(&bm, 0, sizeof bm);
    bm.BytesPerRow = 4; bm.Rows = 8; bm.Depth = 1; bm.Planes[0] = plane;
    memset(&rp, 0, sizeof rp);
    rp.BitMap = &bm; rp.FgPen = 1; rp.Mask = 0xFF; rp.DrawMode = JAM1;
    req.rp = &rp; req.x0 = 2; req.y0 = 1; req.x1 = 9; req.y1 = 2;
    CHECK(gp.v1.rectfill(gp.v1.userdata, &req) == 1, "the fast RectFill declined a Fast RAM planar bitmap");
    ok = 1;
    for (i = 0; i < 32; i++) {
        UBYTE want = (i == 4 || i == 8) ? 0x3F : (i == 5 || i == 9) ? 0xC0 : 0;
        if (plane[i] != want) ok = 0;
    }
    CHECK(ok, "the fast RectFill drew %02x %02x %02x %02x / %02x %02x", plane[4], plane[5], plane[6], plane[8], plane[9], plane[10]);
    rp.Layer = (struct Layer *)plane;  /* a layer: graphics.library's */
    CHECK(gp.v1.rectfill(gp.v1.userdata, &req) == 0, "the fast RectFill took a layered RastPort");

    ncalls = 0;
    CHECK(OGFX_Version(B) == 0x00010005 && count(L_GFX, -1062) == 1, "Version not GFX_DispatchVersion's");
    CHECK(OGFX_Status(B) == (OGFX_STATUS_PATCHED | OGFX_STATUS_ENABLED | OGFX_STATUS_PROVIDER | OGFX_STATUS_AMICRADLE),
          "Status not GFX_DispatchStatus's with AMICRADLE");
    CHECK(OGFX_InstallPatches(B) == 1 && count(L_EXEC, -420) == 0 && !ST->patched, "InstallPatches patched");
    CHECK(OGFX_SetEnabled(0, B) == 1 && ST->enabled, "SetEnabled changed something");
    CHECK(OGFX_SetEnabled(1, B) == 1, "SetEnabled's answer not 1");

    /* GFOpenRTG's record (an older one, through gfopengpu.library): graphics.library's now */
    memset(&rtg, 0, sizeof rtg);
    rtg.v1.size = sizeof rtg; rtg.v1.abi = OGFX_PROVIDER_ABI_V1; rtg.v1.owner = &rtg; rtg.v1.rectfill = rtg_rectfill;
    ncalls = 0;
    CHECK(OGFX_RegisterProvider(&rtg.v1, B) == 1, "RegisterProvider refused");
    c = last(L_GFX, -1068);
    CHECK(c && c->a0 == (ULONG)&rtg && gp.v1.owner == &rtg && gp.v1.rectfill == rtg_rectfill,
          "RegisterProvider not GFX_RegisterProvider(a0)");
    CHECK(!ST->have_provider, "RegisterProvider kept the record in gfopengpu.library too");
    /* taken out: the fast paths go back in */
    ncalls = 0;
    CHECK(OGFX_UnregisterProvider(&rtg, B) == 1, "UnregisterProvider failed");
    c = last(L_GFX, -1074);
    CHECK(c && c->a0 == (ULONG)&rtg, "UnregisterProvider not GFX_UnregisterProvider(a0)");
    CHECK(count(L_GFX, -1068) == 1 && gp_have && gp.v1.owner == (APTR)ST, "the fast paths not offered again");
    /* the fast paths' own record out: nothing offered */
    ncalls = 0;
    CHECK(OGFX_UnregisterProvider(ST, B) == 1 && count(L_GFX, -1068) == 0 && !gp_have, "the fast paths' own record");
    CHECK(OGFX_UnregisterProvider(&rtg, B) == 0, "a record not in, taken out");

    memset(&look, 0, sizeof look);
    look.size = sizeof look; look.abi = OGFX_LOOK_ABI_V1; look.owner = &look; look.rectfill = look_rectfill;
    ncalls = 0;
    CHECK(OGFX_RegisterLook(&look, B) == 1 && (c = last(L_GFX, -1080)) && c->a0 == (ULONG)&look && !ST->have_look,
          "RegisterLook not GFX_RegisterLook(a0)");
    CHECK(OGFX_UnregisterLook(&look, B) == 1 && (c = last(L_GFX, -1086)) && c->a0 == (ULONG)&look,
          "UnregisterLook not GFX_UnregisterLook(a0)");

    drawing_lvos_forward("dispatch");
}

/* A provider is in already when the library starts: the fast paths stay out. */
static void dispatch_provider_first(void)
{
    scene(1, 1, 47, 101, 1092, 0);
    gp_have = 1; gp.v1.owner = (APTR)&gp;
    ogfx_init(ST, &libs[L_EXEC].u.exec);
    CHECK(count(L_GFX, -1092) == 1 && count(L_GFX, -1068) == 0 && !ST->fast_in && gp.v1.owner == (APTR)&gp,
          "the fast paths took the place of a provider in");
    CHECK(ogfx_may_expunge(ST), "may not expunge with nothing in graphics.library");
    CHECK(OGFX_Status(B) == (OGFX_STATUS_PATCHED | OGFX_STATUS_ENABLED | OGFX_STATUS_PROVIDER), "Status without a board");
}

/* 47.102, the provider chain: the fast paths go in at the start with
 * GFX_RegisterProviderPri at GFXDISPATCH_PRI_FAST, whatever is in already;
 * GFOpenRTG's provider (GFXDISPATCH_PRI_RTG) is asked before them; a
 * program's provider through OGFX_RegisterProvider goes in beside them at
 * the default priority, and out again without touching them. */
static void with_chain(int rtg_first)
{
    struct OGFXProviderAll rtg, prog;
    struct call *c;
    const char *how = rtg_first ? "chain, GFOpenRTG first" : "chain";
    memset(&rtg, 0, sizeof rtg);
    rtg.v1.size = sizeof rtg; rtg.v1.abi = OGFX_PROVIDER_ABI_V1; rtg.v1.owner = &rtg; rtg.v1.rectfill = rtg_rectfill;
    memset(&prog, 0, sizeof prog);
    prog.v1.size = sizeof prog.v1; prog.v1.abi = OGFX_PROVIDER_ABI_V1; prog.v1.owner = &prog; prog.v1.rectfill = rtg_rectfill;
    scene(1, 1, 47, 102, 1098, 1);
    if (rtg_first) chain_reg(&rtg.v1, GFXDISPATCH_PRI_RTG);
    ogfx_init(ST, &libs[L_EXEC].u.exec);
    c = last(L_GFX, -1098);
    CHECK(ST->dispatch == OGFX_DISPATCH_YES && ST->chain && ST->fast_in, "%s: the chain not seen", how);
    CHECK(c && count(L_GFX, -1098) == 1 && count(L_GFX, -1068) == 0 && (LONG)c->d0 == GFXDISPATCH_PRI_FAST,
          "%s: the fast paths not in with GFX_RegisterProviderPri(a0, d0 %d)", how, GFXDISPATCH_PRI_FAST);
    CHECK(gn == 1 + rtg_first && gch[gn - 1].rec.v1.owner == (APTR)ST && gch[gn - 1].rec.v1.rectfill &&
          gch[gn - 1].rec.v1.bltbitmap && gch[gn - 1].rec.v1.scrollraster && !gch[gn - 1].rec.v1.text,
          "%s: the fast paths not last in the chain (%d in)", how, gn);
    CHECK(!ogfx_may_expunge(ST), "%s: may expunge with the fast paths in graphics.library", how);
    CHECK(OGFX_Version(B) == 0x00010006, "%s: Version not GFX_DispatchVersion's 1.6", how);
    /* GFOpenRTG registers with graphics.library itself, before the fast paths */
    if (!rtg_first) CHECK(chain_reg(&rtg.v1, GFXDISPATCH_PRI_RTG) == 1, "%s: GFOpenRTG refused", how);
    CHECK(gn == 2 && gch[0].rec.v1.owner == &rtg && gch[1].rec.v1.owner == (APTR)ST, "%s: GFOpenRTG not first", how);
    /* a program's provider through gfopengpu.library: beside them, between */
    ncalls = 0;
    CHECK(OGFX_RegisterProvider(&prog.v1, B) == 1 && (c = last(L_GFX, -1068)) && c->a0 == (ULONG)&prog,
          "%s: RegisterProvider not GFX_RegisterProvider(a0)", how);
    CHECK(gn == 3 && gch[0].rec.v1.owner == &rtg && gch[1].rec.v1.owner == &prog && gch[2].rec.v1.owner == (APTR)ST,
          "%s: the program's provider not between GFOpenRTG's and the fast paths", how);
    CHECK(OGFX_Status(B) == (OGFX_STATUS_PATCHED | OGFX_STATUS_ENABLED | OGFX_STATUS_PROVIDER | OGFX_STATUS_AMICRADLE),
          "%s: Status", how);
    ncalls = 0;
    CHECK(OGFX_UnregisterProvider(&prog, B) == 1 && (c = last(L_GFX, -1074)) && c->a0 == (ULONG)&prog,
          "%s: UnregisterProvider not GFX_UnregisterProvider(a0)", how);
    CHECK(count(L_GFX, -1068) == 0 && count(L_GFX, -1098) == 0 && count(L_GFX, -1092) == 0,
          "%s: the fast paths offered again after another's provider went", how);
    CHECK(gn == 2 && gch[0].rec.v1.owner == &rtg && gch[1].rec.v1.owner == (APTR)ST, "%s: the others changed", how);
    CHECK(OGFX_InstallPatches(B) == 1 && count(L_EXEC, -420) == 0, "%s: InstallPatches patched", how);
    drawing_lvos_forward(how);
}

/* graphics.library not up yet at the start (from ROM): asked at the first call. */
static void dispatch_later(void)
{
    scene(1, 0, 47, 101, 1092, 0);
    ogfx_init(ST, &libs[L_EXEC].u.exec);
    CHECK(count(L_EXEC, -552) == 0 && !ST->dispatch, "graphics.library opened at the start before it was up");
    gfx_up = 1;
    CHECK(OGFX_Version(B) == 0x00010005 && count(L_GFX, -1062) == 1, "Version not forwarded later");
    CHECK(ST->dispatch == OGFX_DISPATCH_YES && ST->fast_in && count(L_GFX, -1068) == 1, "the fast paths not offered later");
}

/* No dispatch: as before (disk), or nothing at all (ROM). */
static void without_dispatch(UWORD version, UWORD revision, UWORD negsize)
{
    struct OGFXProviderAll rtg;
    char how[40];
    snprintf(how, sizeof how, "%u.%u, table to -%u", version, revision, negsize);
    scene(1, 1, version, revision, negsize, 1);
    ogfx_init(ST, &libs[L_EXEC].u.exec);
    CHECK(ST->dispatch == OGFX_DISPATCH_NO && dispatch_calls() == 0 && !ST->fast_in, "%s: the dispatch asked", how);
    memset(&rtg, 0, sizeof rtg);
    rtg.v1.size = sizeof rtg; rtg.v1.abi = OGFX_PROVIDER_ABI_V1; rtg.v1.owner = &rtg; rtg.v1.rectfill = rtg_rectfill;
    CHECK(OGFX_Version(B) == ((OGFX_INTERFACE_VERSION << 16) | OGFX_INTERFACE_REVISION), "%s: Version", how);
#ifdef OGPU_ROM_NO_OGFX
    CHECK(OGFX_RegisterProvider(&rtg.v1, B) == 0 && !ST->have_provider, "%s: the ROM build took a provider", how);
    CHECK(OGFX_InstallPatches(B) == 0 && count(L_EXEC, -420) == 0 && !ST->patched, "%s: the ROM build patched", how);
    CHECK(OGFX_Status(B) == OGFX_STATUS_ENABLED, "%s: the ROM build's Status %08lx", how, (unsigned long)OGFX_Status(B));
    drawing_lvos_forward(how);
#else
    {
        int p, all = 1;
        CHECK(OGFX_RegisterProvider(&rtg.v1, B) == 1 && ST->have_provider && ST->provider.v1.owner == &rtg,
              "%s: the provider not kept", how);
        CHECK(OGFX_InstallPatches(B) == 1 && ST->patched && count(L_EXEC, -420) == OGFX_P_COUNT,
              "%s: %d patches, not %d", how, count(L_EXEC, -420), OGFX_P_COUNT);
        for (p = 0; p < OGFX_P_COUNT; p++)
            if (!ST->old[p]) all = 0;
        CHECK(all, "%s: a vector not kept", how);
        CHECK(OGFX_Status(B) == (OGFX_STATUS_PATCHED | OGFX_STATUS_ENABLED | OGFX_STATUS_PROVIDER | OGFX_STATUS_AMICRADLE),
              "%s: Status %08lx", how, (unsigned long)OGFX_Status(B));
        CHECK(OGFX_SetEnabled(0, B) == 1 && !ST->enabled && OGFX_SetEnabled(1, B) == 0, "%s: SetEnabled", how);
        CHECK(!ogfx_may_expunge(ST), "%s: may expunge while patched", how);
        CHECK(OGFX_UnregisterProvider(&rtg, B) == 1 && !ST->have_provider, "%s: UnregisterProvider", how);
        CHECK(dispatch_calls() == 0, "%s: the dispatch called", how);
    }
#endif
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    libs = mmap(NULL, sizeof(struct fake) * L_COUNT, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (libs == MAP_FAILED) { printf("FAIL: mmap\n"); return 1; }
    with_dispatch();
    dispatch_provider_first();
    dispatch_later();
    with_chain(0);
    with_chain(1);
    without_dispatch(47, 100, 1092);
    without_dispatch(47, 101, 1056);
    without_dispatch(47, 102, 1056);
    without_dispatch(40, 68, 1056);
    if (fails) { printf("%d of %d checks failed\n", fails, checks); return 1; }
#ifdef OGPU_ROM_NO_OGFX
    printf("ROM build: %d checks passed\n", checks);
#else
    printf("disk build: %d checks passed\n", checks);
#endif
    return 0;
}
