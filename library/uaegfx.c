/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The uaegfx backend (0.26, uaegfx.h): GFOpenRTG on the RTG board of
 * Amiberry, WinUAE and FS-UAE. docs/design/UAEGFX-20261111.md says why it is
 * built this way. Our own code: the emulators' sources (GPL) and AROS's were
 * read for the interface only, and the commercial Picasso96 not at all.
 *
 * The board. A UAE-family emulator with an RTG board ("uaegfx", Zorro II or
 * III) makes a library of its own, uaegfx.card, as it mounts its boot ROM's
 * drives: in exec's list before DOS boots, with two calls, FindCard (-30)
 * and InitCard (-36), the card-driver interface Picasso96 defined. Given a
 * BoardInfo, FindCard claims the board and fills in where its video RAM is
 * and how big; InitCard fills in the board's functions. The emulator shows
 * what those functions are told: a mode (SetGC), the format (SetDAC), where
 * the picture starts in video RAM (SetPanning), the display (SetDisplay),
 * the switch between the chipset and the board (SetSwitch), the 8-bit pens
 * (SetColorArray) and, when it offers one, a pointer sprite (SetSprite and
 * the rest; Amiberry's "RTG hardware sprite" option). GFOpenRTG draws into
 * the video RAM with the CPU, as on ACRTG; the emulator shows it as it is.
 *
 * The BoardInfo, ModeInfo and BitMapExtra fields used are below, at the
 * offsets of the published Picasso96 card-driver interface (BoardInfo's
 * layout, as every card driver and every UAE-family emulator has it).
 *
 * Kept nowhere but in memory it allocates (ortg_S->ugx), as the rest of the
 * library: no data or BSS, so it runs from a ROM.
 */
#include <exec/types.h>
#include "ortg_state.h"
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <libraries/configvars.h>
#include "../include/oa_protos_begin.h"
#include <proto/exec.h>
#include "../include/oa_protos_end.h"
#include "uaegfx.h"

/* BoardInfo */
#define BI_MemoryBase        4
#define BI_MemorySize        12
#define BI_ExecBase          60
#define BI_UtilBase          64
#define BI_ResolutionsList   158     /* a MinList, 12 bytes */
#define BI_Flags             186
#define BI_SetSwitch         282     /* each a function the card fills in */
#define BI_SetColorArray     286
#define BI_SetDAC            290
#define BI_SetGC             294
#define BI_SetPanning        298
#define BI_SetDisplay        314
#define BI_SetSprite         518
#define BI_SetSpritePosition 522
#define BI_SetSpriteImage    526
#define BI_SetSpriteColor    530
#define BI_CLUT              578     /* 256 x R, G, B */
#define BI_BitMapExtra       1354
#define BI_BitMapList        1358    /* a MinList */
#define BI_MemList           1370    /* a MinList */
#define BI_MouseX            1382
#define BI_MouseY            1384
#define BI_MouseWidth        1386
#define BI_MouseHeight       1387
#define BI_MouseXOffset      1388
#define BI_MouseYOffset      1389
#define BI_MouseImage        1390
#define BI_BYTES             2418
#define BI_ALLOC             2560    /* with room to spare */
#define BIF_HARDWARESPRITE   (1UL << 0)
/* ModeInfo */
#define MI_Width             18
#define MI_Height            20
#define MI_Depth             22
#define MI_BYTES             48
/* BitMapExtra: the size of the bitmap a picture is in, its rows' width with it */
#define BME_Width            40
#define BME_Height           42
#define BME_BYTES            64
/* RGB formats (RGBFTYPE) */
#define RGBFB_CLUT           1
#define RGBFB_A8R8G8B8       6
#define RGBFB_R5G6B5         10

/* The register page's stand-in: ACRTG's offsets, as screens.c writes them */
#define R_WIDTH 0x10
#define R_HEIGHT 0x14
#define R_FORMAT 0x18
#define R_SWITCH 0x30
#define R_COMMIT 0x48
#define R_ARG_A 0x60
#define R_ARG_B 0x64
#define R_PAL_INDEX 0x80
#define R_PAL_RGB 0x84
#define R_SPR_CTRL 0x90
#define R_SPR_X 0x94
#define R_SPR_Y 0x98
#define R_SPR_HOT 0x9C
#define R_SPR_SIZE 0xA0
#define R_SPR_DATA 0xA4
#define R_SPR_COLOR 0xA8
#define C_MODE 1
#define C_PAN 6
#define C_DISPLAY 7
#define FMT_CLUT 1          /* screens.c's board_fmt */
#define FMT_ARGB32 6
#define FMT_RGB16 11
#define SHADOW_LONGS (0x120 / 4)
#define SPR_ROWS 64

struct ugx_board {
    struct ConfigDev cd;            /* what openrtg_lib.c keeps as the monitor's board */
    struct Library *card;           /* the emulator's uaegfx.card */
    UBYTE *bi;                      /* our BoardInfo */
    UBYTE *mode;                    /* our ModeInfo */
    UBYTE *bme;                     /* our BitMapExtra */
    UWORD *spr;                     /* the pointer's picture: 2 words, 2 a row, 2 more */
    int monitor;
    ULONG fmt;                      /* RGBFTYPE now shown */
    ULONG spr_h, spr_ww, spr_n;     /* the picture coming in: rows, words a plane, longs so far */
    ULONG r[SHADOW_LONGS];
};

int ugx_monitor_of(const struct ugx_board *u) { return u->monitor; }

#define U ((struct ugx_board *)ortg_S->ugx)
#define BIL(off) (*(ULONG *)(U->bi + (off)))
#define BIW(off) (*(UWORD *)(U->bi + (off)))
#define BIB(off) (*(UBYTE *)(U->bi + (off)))

/* One of the card's functions, in the card driver interface's registers: BoardInfo in a0, the rest
 * as each takes them, the card's base in a6. The emulator's are traps; any register but a4 and a7 may
 * come back changed, so the trampoline keeps them all. Arguments on the stack:
 * fn, card, bi, a1, d0, d1, d2, d3, d7. */
__asm(
"_ugx_trampoline:\n"
"   movem.l d2-d7/a2-a3/a5-a6,-(sp)\n"
"   move.l 44(sp),a2\n"
"   move.l 48(sp),a6\n"
"   move.l 52(sp),a0\n"
"   move.l 56(sp),a1\n"
"   move.l 60(sp),d0\n"
"   move.l 64(sp),d1\n"
"   move.l 68(sp),d2\n"
"   move.l 72(sp),d3\n"
"   move.l 76(sp),d7\n"
"   jsr (a2)\n"
"   movem.l (sp)+,d2-d7/a2-a3/a5-a6\n"
"   rts\n");
ULONG ugx_trampoline(APTR fn, APTR card, APTR bi, APTR a1, ULONG d0, ULONG d1, ULONG d2, ULONG d3, ULONG d7);

static ULONG ugx_call(APTR fn, APTR a1v, ULONG d0v, ULONG d1v, ULONG d2v, ULONG d3v, ULONG d7v)
{
    return fn ? ugx_trampoline(fn, U->card, U->bi, a1v, d0v, d1v, d2v, d3v, d7v) : 0;
}
#define BI_FN(off) (*(APTR *)(U->bi + (off)))

static void min_list(UBYTE *l)
{
    *(UBYTE **)(l + 0) = l + 4;     /* mlh_Head: the tail */
    *(UBYTE **)(l + 4) = NULL;      /* mlh_Tail */
    *(UBYTE **)(l + 8) = l;         /* mlh_TailPred: the head */
}

int ugx_attach(int monitor, struct ConfigDev **cd, ULONG *vram_bytes)
{
    struct ExecBase *sys = ortg_S->sys;
    struct ugx_board *u;
    struct Library *card;
    UBYTE *mem;
    int p96;
    if (ortg_S->ugx) return 0;
    /* Picasso96 drives the board itself (its rtg.library has called FindCard); a second FindCard
     * would take the card's BoardInfo from it. And only the emulator's card, the one in memory:
     * never a uaegfx.card from a disk. */
    Forbid();
    p96 = FindName(&sys->LibList, (CONST_STRPTR)"rtg.library") != NULL;
    card = p96 ? NULL : (struct Library *)FindName(&sys->LibList, (CONST_STRPTR)"uaegfx.card");
    if (card) card = OpenLibrary((CONST_STRPTR)"uaegfx.card", 0);
    Permit();
    if (!card) return 0;
    mem = AllocVec(sizeof *u + BI_ALLOC + MI_BYTES + BME_BYTES + (4 + 2 * SPR_ROWS) * 2, MEMF_PUBLIC | MEMF_CLEAR);
    if (!mem) { CloseLibrary(card); return 0; }
    u = (struct ugx_board *)mem; mem += sizeof *u;
    u->bi = mem; mem += BI_ALLOC;
    u->mode = mem; mem += MI_BYTES;
    u->bme = mem; mem += BME_BYTES;
    u->spr = (UWORD *)mem;
    u->card = card;
    u->monitor = monitor;
    u->fmt = RGBFB_CLUT;
    ortg_S->ugx = u;
    min_list(u->bi + BI_ResolutionsList);
    min_list(u->bi + BI_BitMapList);
    min_list(u->bi + BI_MemList);
    BIL(BI_ExecBase) = (ULONG)sys;
    BIL(BI_UtilBase) = (ULONG)ortg_S->utility;
    BIL(BI_BitMapExtra) = (ULONG)u->bme;
    /* FindCard and InitCard: the library's own calls, -30 and -36 */
    if (!ugx_call((UBYTE *)card - 30, NULL, 0, 0, 0, 0, 0) || !ugx_call((UBYTE *)card - 36, NULL, 0, 0, 0, 0, 0) ||
        !BIL(BI_MemoryBase) || BIL(BI_MemorySize) < 0x100000UL || !BI_FN(BI_SetGC) || !BI_FN(BI_SetPanning) || !BI_FN(BI_SetSwitch)) {
        ortg_S->ugx = NULL;
        CloseLibrary(card);
        FreeVec(u);
        return 0;
    }
    u->cd.cd_BoardAddr = (APTR)BIL(BI_MemoryBase);
    u->cd.cd_BoardSize = BIL(BI_MemorySize);
    u->cd.cd_Rom.er_SerialNumber = (ULONG)monitor;
    *cd = &u->cd;
    *vram_bytes = BIL(BI_MemorySize);
    return 1;
}

static ULONG rgbf_of(ULONG fmt)
{
    return fmt == FMT_RGB16 ? RGBFB_R5G6B5 : fmt == FMT_ARGB32 ? RGBFB_A8R8G8B8 : RGBFB_CLUT;
}
static ULONG bytes_of(ULONG rgbf)
{
    return rgbf == RGBFB_R5G6B5 ? 2 : rgbf == RGBFB_A8R8G8B8 ? 4 : 1;
}

/* C_MODE: the size and format (SetGC, then SetDAC) */
static void ugx_mode(void)
{
    ULONG w = U->r[R_WIDTH / 4], h = U->r[R_HEIGHT / 4];
    U->fmt = rgbf_of(U->r[R_FORMAT / 4]);
    *(UWORD *)(U->mode + MI_Width) = (UWORD)w;
    *(UWORD *)(U->mode + MI_Height) = (UWORD)h;
    U->mode[MI_Depth] = (UBYTE)(bytes_of(U->fmt) * 8);
    ugx_call(BI_FN(BI_SetGC), U->mode, 0, 0, 0, 0, 0);
    ugx_call(BI_FN(BI_SetDAC), NULL, 0, 0, 0, 0, U->fmt);
}

/* C_PAN: the picture at ARG_A in video RAM, ARG_B bytes a row (SetPanning) */
static void ugx_pan(void)
{
    ULONG off = U->r[R_ARG_A / 4], stride = U->r[R_ARG_B / 4], bpp = bytes_of(U->fmt);
    *(UWORD *)(U->bme + BME_Width) = (UWORD)(stride / bpp);
    *(UWORD *)(U->bme + BME_Height) = (UWORD)U->r[R_HEIGHT / 4];
    BIL(BI_BitMapExtra) = (ULONG)U->bme;
    ugx_call(BI_FN(BI_SetPanning), (UBYTE *)U->cd.cd_BoardAddr + off, stride / bpp, 0, 0, 0, U->fmt);
}

/* SPR_HOT ends a picture: hand it to the card (SetSpriteImage). 16 pixels wide, its first word a
 * plane: the card's format is the chipset sprite's, two control words first. */
static void ugx_sprite_image(void)
{
    UWORD h = (UWORD)(U->spr_h > SPR_ROWS ? SPR_ROWS : U->spr_h);
    U->spr[2 + 2 * h] = 0; U->spr[3 + 2 * h] = 0;
    BIL(BI_MouseImage) = (ULONG)U->spr;
    BIB(BI_MouseWidth) = 16;
    BIB(BI_MouseHeight) = (UBYTE)h;
    BIB(BI_MouseXOffset) = 0;
    BIB(BI_MouseYOffset) = 0;
    ugx_call(BI_FN(BI_SetSpriteImage), NULL, 0, 0, 0, 0, U->fmt);
}

void ugx_reg(ULONG off, ULONG v)
{
    struct ugx_board *u = U;
    if (off < SHADOW_LONGS * 4) u->r[off / 4] = v;
    switch (off) {
    case R_COMMIT:
        if (v == C_MODE) ugx_mode();
        else if (v == C_PAN) ugx_pan();
        else if (v == C_DISPLAY) ugx_call(BI_FN(BI_SetDisplay), NULL, u->r[R_ARG_A / 4] ? 1 : 0, 0, 0, 0, 0);
        break;
    case R_SWITCH:
        ugx_call(BI_FN(BI_SetSwitch), NULL, v ? 1 : 0, 0, 0, 0, 0);
        break;
    case R_PAL_RGB: {
        ULONG i = u->r[R_PAL_INDEX / 4] & 255;
        UBYTE *c = u->bi + BI_CLUT + 3 * i;
        c[0] = (UBYTE)(v >> 16); c[1] = (UBYTE)(v >> 8); c[2] = (UBYTE)v;
        ugx_call(BI_FN(BI_SetColorArray), NULL, i, 1, 0, 0, 0);
        break;
    }
    case R_SPR_SIZE:
        u->spr_h = v & 0xFFFF; u->spr_ww = (v >> 16) / 16; u->spr_n = 0;
        if (!u->spr_ww) u->spr_ww = 1;
        u->spr[0] = u->spr[1] = 0;
        break;
    case R_SPR_DATA: {
        ULONG row = u->spr_n / u->spr_ww;
        if (u->spr_n % u->spr_ww == 0 && row < SPR_ROWS) { u->spr[2 + 2 * row] = (UWORD)(v >> 16); u->spr[3 + 2 * row] = (UWORD)v; }
        u->spr_n++;
        break;
    }
    case R_SPR_COLOR:
        if ((BIL(BI_Flags) & BIF_HARDWARESPRITE) && (v >> 24) < 3)
            ugx_call(BI_FN(BI_SetSpriteColor), NULL, v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255, u->fmt);
        break;
    case R_SPR_HOT:
        if (BIL(BI_Flags) & BIF_HARDWARESPRITE) ugx_sprite_image();
        break;
    case R_SPR_Y:
        BIW(BI_MouseX) = (UWORD)u->r[R_SPR_X / 4];
        BIW(BI_MouseY) = (UWORD)v;
        if (BIL(BI_Flags) & BIF_HARDWARESPRITE)
            ugx_call(BI_FN(BI_SetSpritePosition), NULL, u->r[R_SPR_X / 4], v, 0, 0, u->fmt);
        break;
    case R_SPR_CTRL:
        if (BIL(BI_Flags) & BIF_HARDWARESPRITE) ugx_call(BI_FN(BI_SetSprite), NULL, v ? 1 : 0, 0, 0, 0, u->fmt);
        break;
    default:
        break;
    }
}

/* Every register reads 0 but what was written to it: ACRTG v0, so the ring, the card part, the watch
 * and the Native size stay off. */
ULONG ugx_rreg(ULONG off)
{
    return off < SHADOW_LONGS * 4 && off != 0x04 ? U->r[off / 4] : 0;
}
