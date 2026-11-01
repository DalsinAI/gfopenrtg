/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenRTGScrollExact: does ScrollRaster in a window partly covered by
 * another give on an GFOpenRTG screen what graphics.library gives on a
 * planar one? A Shell window with another window over its middle is the
 * case: its ClipRects are a band above, the pieces left and right of the
 * cover, the hidden piece and a band below, and the contents must move
 * across all of them as one (review NEW 3, 1 Nov 2026).
 *
 * Each case fills a window with a pattern (WriteChunkyPixels), opens a cover
 * over its middle, sets odd pens and a draw mode, and calls ScrollRaster. It
 * then reads the window's pixels where they show, its DamageList and
 * LAYERREFRESH (simple refresh), closes the cover and reads the window again
 * (all of it for smart refresh, whose hidden part comes back from the backing
 * store). Each case runs in a smart and in a simple refresh window, on a
 * planar 8-colour screen with the ROM's ScrollRaster (the reference); on the
 * same screen through library/scroll_plan.h, the rules and the move
 * gfopenrtg.library's ScrollRaster runs, here with graphics.library's
 * BltBitMap, RectFill and regions on the window's real ClipRects; and on an
 * 8-bit GFOpenRTG screen with ORTG_DRAW_CPU_ONLY and with GFOpenGPU (the
 * gfopenrtg.library that is in the system). Every result must equal the ROM's
 * pixel for pixel, damage for damage. Each is also checked against the model
 * (make_model: the rules as measured on OS 3.2), so a change in the ROM's
 * rules shows.
 *
 * OpenRTGScrollExact SHELL shows the Shell case to the eye instead: on an
 * 8-bit GFOpenRTG screen of its own, a smart refresh window with
 * console.device in it and a window over its middle, 120 lines written; it
 * waits 20 seconds, closes the cover, and waits 20 more (for screenshots).
 *   OpenRTGScrollExact      Returns 0 when every result equals the ROM's,
 *                           10 when one doesn't, 20 when it can't run. */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/console.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/layers.h>
#include <graphics/clip.h>
#include <graphics/regions.h>
#include <graphics/modeid.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/cybergraphics.h>
#include <proto/gfopenrtg.h>
#include <stdio.h>
#include <string.h>
#include "../library/scroll_plan.h"

#define IW 160                  /* the window's inside */
#define IH 96
#define CX 50                   /* the cover, in the window */
#define CY 36
#define CW 60
#define CH 24

struct Library *CyberGfxBase, *OpenRTGBase;

struct scase { WORD dx, dy, x0, y0, x1, y1; const char *what; UBYTE pre; };   /* pre: damage 10,40-29,49 there first */
static const struct scase cases[] = {
    {   0,   8,   0,   0, IW - 1, IH - 1, "up 8 (a Shell's new line)" },
    {   0,  -9,   0,   0, IW - 1, IH - 1, "down 9" },
    {   0,   1,   4,   2, IW - 5, IH - 3, "up 1, inner box" },
    {  11,   0,   0,   0, IW - 1, IH - 1, "left 11" },
    { -13,   0,   0,   0, IW - 1, IH - 1, "right 13" },
    {   7,   5,   0,   0, IW - 1, IH - 1, "left 7 up 5" },
    {  -6,  -4,  10,   8, IW - 20, IH - 6, "right 6 down 4, box" },
    {   0,  30,  20,  20, 300, 300, "up 30, box past the window" },
    {   0,  IH,   0,   0, IW - 1, IH - 1, "up the box's height" },
    {  IW + 5, 0, 0,   0, IW - 1, IH - 1, "left past the box's width" },
    {   0,   8,   0,   0, IW,     IH - 1, "up 8, box 1 past the right" },
    {   0,  -8,   0,   0, IW - 1, IH + 9, "down 8, box past the bottom" },
    {   0,   8,   0,   0, IW - 1, 30,     "up 8, box above the cover" },
    {   0,   8,  -5,   0, IW - 1, IH - 1, "up 8, box from x -5" },
    {   0,   8,   0,   0, IW - 1, IH - 1, "up 8, damage there before", 1 },
    {   0, IH + 20, 0,  0, IW - 1, IH + 9, "up past the box, box past" },
};
#define CASES (int)(sizeof cases / sizeof cases[0])
#define KINDS 2                 /* smart, simple */
#define RUNS 4                  /* planar (the ROM), planar through scroll_plan.h, GFOpenRTG CPU, GFOpenRTG GFOpenGPU */

/* For each run, kind and case: the pixels where they show with the cover
 * open, the damage, LAYERREFRESH, and the pixels after the cover went. */
struct result { UBYTE shown[IH][IW], damage[IH][IW], after[IH][IW]; UBYTE refresh, ok; };
static struct result *res;      /* [RUNS][KINDS][CASES] */
static UBYTE pattern[IH][IW], model[IH][IW], mdamage[IH][IW];
static int fails, checks, model_diffs;

static struct result *R(int run, int kind, int n) { return &res[(run * KINDS + kind) * CASES + n]; }
static int covered(LONG x, LONG y) { return x >= CX && x < CX + CW && y >= CY && y < CY + CH; }

static void read_window(struct Screen *scr, struct Window *w, UBYTE to[IH][IW])
{
    struct RastPort rp;
    InitRastPort(&rp);
    rp.BitMap = scr->RastPort.BitMap;
    for (LONG y = 0; y < IH; y++)
        for (LONG x = 0; x < IW; x++) to[y][x] = (UBYTE)ReadPixel(&rp, w->LeftEdge + x, w->TopEdge + y);
}

static void read_damage(struct Layer *l, UBYTE to[IH][IW])
{
    struct RegionRectangle *rr;
    memset(to, 0, IW * IH);
    if (!l->DamageList) return;
    for (rr = l->DamageList->RegionRectangle; rr; rr = rr->Next)
        for (LONG y = rr->bounds.MinY + l->DamageList->bounds.MinY; y <= rr->bounds.MaxY + l->DamageList->bounds.MinY; y++)
            for (LONG x = rr->bounds.MinX + l->DamageList->bounds.MinX; x <= rr->bounds.MaxX + l->DamageList->bounds.MinX; x++)
                if (x >= 0 && y >= 0 && x < IW && y < IH) to[y][x] = 1;
}

/* ScrollRaster through library/scroll_plan.h, the code gfopenrtg.library's
 * ScrollRaster runs, with graphics.library's own BltBitMap, RectFill and
 * regions on a planar window's real ClipRects: the same rules and the same
 * move as gfopenrtg.library's, checked against the ROM here even where
 * gfopenrtg.library is the ROM's own copy and can't be replaced. */
static struct RastPort *plan_rp;
static LONG plan_sw, plan_sh;

static void p_damage(void *c, const struct sp_box *r)
{
    struct Layer *l = plan_rp->Layer;
    struct Rectangle q;
    q.MinX = r->x0; q.MinY = r->y0; q.MaxX = r->x1; q.MaxY = r->y1;
    LockLayerRom(l);
    if (l->DamageList && OrRectRegion(l->DamageList, &q)) l->Flags |= LAYERREFRESH;
    UnlockLayerRom(l);
}

static void p_fill(void *c, const struct sp_box *r)
{
    UBYTE a = plan_rp->FgPen, m = plan_rp->DrawMode;
    SetAPen(plan_rp, plan_rp->BgPen);
    SetDrMd(plan_rp, JAM1);
    RectFill(plan_rp, r->x0, r->y0, r->x1, r->y1);
    SetAPen(plan_rp, a);
    SetDrMd(plan_rp, m);
}

static void p_blit(void *c, void *sbm, long sx, long sy, void *dbm, long dx, long dy, long w, long h)
{
    BltBitMap(sbm, sx, sy, dbm, dx, dy, w, h, 0xC0, 0xFF, NULL);
}

/* as screens.c's scroll_where and scroll_move */
static void p_move(void *c, const struct sp_box *r, long dx, long dy)
{
    struct Layer *l = plan_rp->Layer;
    struct sp_piece p[64];
    UWORD ord[128];
    struct ClipRect *cr;
    struct sp_box b;
    int n = 0;
    b.x0 = r->x0 + l->bounds.MinX - l->Scroll_X; b.x1 = r->x1 + l->bounds.MinX - l->Scroll_X;
    b.y0 = r->y0 + l->bounds.MinY - l->Scroll_Y; b.y1 = r->y1 + l->bounds.MinY - l->Scroll_Y;
    LockLayerRom(l);
    for (cr = l->ClipRect; cr && n < 64; cr = cr->Next) {
        struct sp_box q;
        q.x0 = cr->bounds.MinX; q.y0 = cr->bounds.MinY; q.x1 = cr->bounds.MaxX; q.y1 = cr->bounds.MaxY;
        if (!cr->obscured) {
            if (q.x0 < 0) q.x0 = 0;
            if (q.y0 < 0) q.y0 = 0;
            if (q.x1 >= plan_sw) q.x1 = plan_sw - 1;
            if (q.y1 >= plan_sh) q.y1 = plan_sh - 1;
            p[n].bm = plan_rp->BitMap; p[n].tx = 0; p[n].ty = 0;
        } else {
            if ((l->Flags & LAYERSIMPLE) || !cr->BitMap) continue;
            p[n].bm = cr->BitMap; p[n].tx = (cr->bounds.MinX & 15) - cr->bounds.MinX; p[n].ty = -cr->bounds.MinY;
        }
        if (sp_and(&q, &b, &p[n].box)) n++;
    }
    sp_move(p, n, &b, dx, dy, ord, ord + n, p_blit, NULL);
    UnlockLayerRom(l);
}

static void plan_scroll(struct Screen *scr, struct RastPort *rp, LONG dx, LONG dy, LONG x0, LONG y0, LONG x1, LONG y1)
{
    struct Layer *l = rp->Layer;
    struct sp_ops ops = { p_damage, p_fill, p_move };
    plan_rp = rp; plan_sw = scr->Width; plan_sh = scr->Height;
    sp_scroll(dx, dy, x0, y0, x1, y1, 1, l->bounds.MaxX - l->bounds.MinX, l->bounds.MaxY - l->bounds.MinY,
              (l->Flags & LAYERSIMPLE) != 0, &ops, NULL);
}

static int one(struct Screen *scr, int kind, int n, struct result *r, int plan)
{
    const struct scase *k = &cases[n];
    struct Window *w, *c;
    w = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 30, WA_Top, 20, WA_Width, IW, WA_Height, IH,
                       WA_Borderless, TRUE, kind ? WA_SimpleRefresh : WA_SmartRefresh, TRUE, WA_NoCareRefresh, FALSE,
                       WA_Activate, FALSE, WA_IDCMP, 0, TAG_DONE);
    if (!w) return 0;
    if (kind) { BeginRefresh(w); EndRefresh(w, TRUE); }        /* no damage from opening */
    WriteChunkyPixels(w->RPort, 0, 0, IW - 1, IH - 1, &pattern[0][0], IW);
    c = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)scr, WA_Left, 30 + CX, WA_Top, 20 + CY, WA_Width, CW, WA_Height, CH,
                       WA_Borderless, TRUE, WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE, WA_Activate, FALSE, WA_IDCMP, 0, TAG_DONE);
    if (!c) { CloseWindow(w); return 0; }
    SetAPen(c->RPort, 0);
    RectFill(c->RPort, 0, 0, CW - 1, CH - 1);
    SetAPen(w->RPort, 2);
    SetBPen(w->RPort, 6);
    SetDrMd(w->RPort, JAM2);
    if (kind && k->pre) {
        struct Rectangle pr = { 10, 40, 29, 49 };
        LockLayerRom(w->WLayer);
        OrRectRegion(w->WLayer->DamageList, &pr);
        w->WLayer->Flags |= LAYERREFRESH;
        UnlockLayerRom(w->WLayer);
    }
    if (plan) plan_scroll(scr, w->RPort, k->dx, k->dy, k->x0, k->y0, k->x1, k->y1);
    else ScrollRaster(w->RPort, k->dx, k->dy, k->x0, k->y0, k->x1, k->y1);
    Forbid();                                   /* Intuition is not to refresh it first */
    read_damage(w->WLayer, r->damage);
    r->refresh = (w->WLayer->Flags & LAYERREFRESH) ? 1 : 0;
    Permit();
    read_window(scr, w, r->shown);
    CloseWindow(c);
    read_window(scr, w, r->after);
    CloseWindow(w);
    r->ok = 1;
    return 1;
}

/* What graphics.library's rules give: the move as AROS's MoveRaster has it,
 * the rest as OS 3.2's graphics.library 47.100 does it (this tool measured
 * it, 1 Nov 2026): a distance as wide or high as the box fills all of it
 * with the B pen; in a simple refresh layer what would come from a hidden
 * part is left as it is, with no damage, and a box reaching outside the
 * layer damages all of the box in it, the damage already there staying. */
static void make_model(int kind, int n)
{
    const struct scase *k = &cases[n];
    LONG x1 = k->x1 > IW - 1 ? IW - 1 : k->x1, y1 = k->y1 > IH - 1 ? IH - 1 : k->y1;
    LONG adx = k->dx < 0 ? -k->dx : k->dx, ady = k->dy < 0 ? -k->dy : k->dy;
    int all = adx >= x1 - k->x0 + 1 || ady >= y1 - k->y0 + 1;
    int past = k->x0 < 0 || k->y0 < 0 || k->x1 > IW - 1 || k->y1 > IH - 1;
    memset(mdamage, 0, sizeof mdamage);
    for (LONG y = 0; y < IH; y++)
        for (LONG x = 0; x < IW; x++) {
            LONG sx = x + k->dx, sy = y + k->dy;
            int in = x >= k->x0 && x <= x1 && y >= k->y0 && y <= y1;
            int from = sx >= k->x0 && sx <= x1 && sy >= k->y0 && sy <= y1;
            UBYTE v = pattern[y][x];
            if (in) {
                if (all || !from) v = 6;                            /* uncovered: the B pen */
                else if (!kind || !covered(sx, sy)) v = pattern[sy][sx];  /* else from a simple layer's hidden part: as it was */
                if (kind && past) mdamage[y][x] = 1;
            }
            if (kind && k->pre && x >= 10 && x <= 29 && y >= 40 && y <= 49) mdamage[y][x] = 1;
            model[y][x] = v;
        }
}

static ULONG differ(const struct result *a, const struct result *b, int kind, const char **where, LONG *fx, LONG *fy)
{
    ULONG d = 0;
    for (LONG y = 0; y < IH; y++)
        for (LONG x = 0; x < IW; x++) {
            const char *wh = NULL;
            if (!covered(x, y) && a->shown[y][x] != b->shown[y][x]) wh = "shown";
            else if (kind && a->damage[y][x] != b->damage[y][x]) wh = "damage";
            else if ((!kind || !covered(x, y)) && a->after[y][x] != b->after[y][x]) wh = "after";
            if (wh) { if (!d) { *where = wh; *fx = x; *fy = y; } d++; }
        }
    if (kind && a->refresh != b->refresh) { if (!d) { *where = "LAYERREFRESH"; *fx = *fy = -1; } d++; }
    return d;
}

static struct Screen *open_rtg(void)
{
    struct TagItem bt[] = { { CYBRBIDTG_Depth, 8 }, { CYBRBIDTG_NominalWidth, 640 }, { CYBRBIDTG_NominalHeight, 480 }, { TAG_DONE, 0 } };
    struct OpenRTGBitMapInfo bi;
    ULONG id = BestCModeIDTagList(bt);
    struct Screen *s;
    if (id == INVALID_ID) return NULL;
    s = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, 640, SA_Height, 480, SA_Depth, 8, SA_Title, (ULONG)"OpenRTGScrollExact",
                       SA_ShowTitle, FALSE, SA_Quiet, TRUE, SA_Behind, TRUE, TAG_DONE);
    if (s && !ORTG_BitMapInfo(s->RastPort.BitMap, &bi)) { CloseScreen(s); s = NULL; }
    return s;
}

static struct Screen *open_planar(void)
{
    struct OpenRTGBitMapInfo bi;
    struct Screen *s = OpenScreenTags(NULL, SA_DisplayID, PAL_MONITOR_ID | HIRES_KEY, SA_Width, 640, SA_Height, 256, SA_Depth, 3,
                                      SA_Title, (ULONG)"OpenRTGScrollExact", SA_ShowTitle, FALSE, SA_Quiet, TRUE, SA_Behind, TRUE, TAG_DONE);
    if (s && ORTG_BitMapInfo(s->RastPort.BitMap, &bi)) { CloseScreen(s); s = NULL; }
    return s;
}

/* The Shell case, to look at. */
static int shell(void)
{
    struct Screen *s = open_rtg();
    struct Window *w = NULL, *c = NULL;
    struct MsgPort *port = NULL;
    struct IOStdReq *io = NULL;
    int rc = 20;
    char line[96];
    if (!s) { printf("OpenRTGScrollExact: no 8-bit GFOpenRTG screen\n"); return 20; }
    ScreenToFront(s);
    w = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, 20, WA_Top, 20, WA_Width, 560, WA_Height, 300,
                       WA_Title, (ULONG)"Scroll test (smart refresh)", WA_DragBar, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, TAG_DONE);
    c = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, 160, WA_Top, 120, WA_Width, 260, WA_Height, 90,
                       WA_Title, (ULONG)"Cover", WA_DragBar, TRUE, WA_SimpleRefresh, TRUE, WA_NoCareRefresh, TRUE, TAG_DONE);
    if (w && c && (port = CreateMsgPort()) && (io = (struct IOStdReq *)CreateIORequest(port, sizeof *io))) {
        io->io_Data = w;
        io->io_Length = sizeof *w;
        if (!OpenDevice((CONST_STRPTR)"console.device", 0, (struct IORequest *)io, 0)) {
            for (int i = 1; i <= 120; i++) {
                sprintf(line, "Line %03d: the quick brown fox jumps over the lazy dog %03d\n", i, i);
                io->io_Command = CMD_WRITE; io->io_Data = line; io->io_Length = -1;
                DoIO((struct IORequest *)io);
            }
            printf("OpenRTGScrollExact SHELL: 120 lines written, the cover open for 20 s\n");
            Delay(20 * 50);
            CloseWindow(c); c = NULL;
            printf("OpenRTGScrollExact SHELL: the cover closed, 20 s\n");
            Delay(20 * 50);
            CloseDevice((struct IORequest *)io);
            rc = 0;
        }
    }
    if (io) DeleteIORequest((struct IORequest *)io);
    if (port) DeleteMsgPort(port);
    if (c) CloseWindow(c);
    if (w) CloseWindow(w);
    CloseScreen(s);
    return rc;
}

int main(int argc, char **argv)
{
    static const char *const runs[RUNS] = { "planar", "plan", "CPU", "GFOpenGPU" };
    static const char *const kinds[KINDS] = { "smart", "simple" };
    int run, kind, n, rc = 0;
    if (!(OpenRTGBase = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, 0))) { printf("OpenRTGScrollExact: needs gfopenrtg.library\n"); return 20; }
    if (!(CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40))) {
        printf("OpenRTGScrollExact: needs cybergraphics.library\n");
        CloseLibrary(OpenRTGBase);
        return 20;
    }
    if (argc > 1 && !strcmp(argv[1], "SHELL")) { rc = shell(); goto out; }
    if (!(res = AllocVec(sizeof *res * RUNS * KINDS * CASES, MEMF_ANY | MEMF_CLEAR))) { rc = 20; goto out; }
    for (LONG y = 0; y < IH; y++)                                   /* pens 1 to 5, no run of one along x or y */
        for (LONG x = 0; x < IW; x++) pattern[y][x] = (UBYTE)(1 + ((x * 7 + y * 13 + (x / 5) * 3 + (y / 3)) * 2654435761UL >> 20) % 5);
    for (run = 0; run < RUNS; run++) {
        struct Screen *s = run >= 2 ? open_rtg() : open_planar();
        if (!s) { printf("OpenRTGScrollExact: no %s screen\n", run >= 2 ? "8-bit GFOpenRTG" : "planar"); rc = 20; continue; }
        ORTG_DrawStats(NULL, 0, ORTG_DRAW_RESET | (run == 2 ? ORTG_DRAW_CPU_ONLY : ORTG_DRAW_OPENGPU));
        for (kind = 0; kind < KINDS; kind++)
            for (n = 0; n < CASES; n++)
                if (!one(s, kind, n, R(run, kind, n), run == 1)) { printf("%-7s %-6s case %d: no windows\n", runs[run], kinds[kind], n); rc = 20; }
        if (run == 3) {
            ULONG c[4 * 21];
            ORTG_DrawStats(c, 21, 0);
            printf("GFOpenGPU run: ScrollRaster moves %lu pieces by GFOpenGPU, %lu by the CPU; fills %lu by GFOpenGPU, %lu by the CPU\n",
                   (unsigned long)c[4 * ORTG_STAT_COPY], (unsigned long)c[4 * ORTG_STAT_COPY + 1],
                   (unsigned long)c[4 * ORTG_STAT_SCROLL], (unsigned long)c[4 * ORTG_STAT_SCROLL + 1]);
        }
        ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
        CloseScreen(s);
    }
    for (kind = 0; kind < KINDS; kind++)
        for (n = 0; n < CASES; n++) {
            struct result m;
            make_model(kind, n);
            memcpy(m.shown, model, sizeof model); memcpy(m.after, model, sizeof model); memcpy(m.damage, mdamage, sizeof mdamage);
            m.refresh = 0;
            for (LONG y = 0; y < IH; y++) for (LONG x = 0; x < IW; x++) m.refresh |= mdamage[y][x];
            for (run = 0; run < RUNS; run++) {
                struct result *r = R(run, kind, n);
                const char *wh = "", *mw = "";
                LONG fx = 0, fy = 0, mx = 0, my = 0;
                ULONG d = 0, dm;
                if (!r->ok) continue;
                dm = differ(r, &m, kind, &mw, &mx, &my);
                if (dm) model_diffs++;
                if (run && R(0, kind, n)->ok) {
                    checks++;
                    if ((d = differ(r, R(0, kind, n), kind, &wh, &fx, &fy))) fails++;
                }
                printf("%-7s %-6s case %d %-28s %s", runs[run], kinds[kind], n, cases[n].what,
                       !run ? "(the ROM)" : !R(0, kind, n)->ok ? "(no reference)" : d ? "DIFFERENT from the ROM" : "same as the ROM");
                if (d) printf(": %lu, first %s at %ld,%ld", (unsigned long)d, wh, (long)fx, (long)fy);
                if (dm) printf("; model differs: %lu, first %s at %ld,%ld", (unsigned long)dm, mw, (long)mx, (long)my);
                if (kind) {
                    LONG bx0 = IW, by0 = IH, bx1 = -1, by1 = -1, cnt = 0;
                    for (LONG y = 0; y < IH; y++)
                        for (LONG x = 0; x < IW; x++)
                            if (r->damage[y][x]) {
                                cnt++;
                                if (x < bx0) bx0 = x;
                                if (y < by0) by0 = y;
                                if (x > bx1) bx1 = x;
                                if (y > by1) by1 = y;
                            }
                    printf("; %s, damage %ld pixels", r->refresh ? "LAYERREFRESH" : "no LAYERREFRESH", (long)cnt);
                    if (cnt) printf(" in %ld,%ld-%ld,%ld", (long)bx0, (long)by0, (long)bx1, (long)by1);
                }
                printf("\n");
            }
        }
    printf("OpenRTGScrollExact: %d of %d results (scroll_plan.h, GFOpenRTG CPU and GFOpenGPU) the same as the ROM's; %d results (of every run) unlike the model\n",
           checks - fails, checks, model_diffs);
    if (fails && rc < 10) rc = 10;
out:
    if (res) FreeVec(res);
    CloseLibrary(CyberGfxBase);
    CloseLibrary(OpenRTGBase);
    return rc;
}
