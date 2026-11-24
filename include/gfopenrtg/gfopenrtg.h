/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * gfopenrtg.library's public interface (phase 2's first part, then each
 * revision's calls; the revision that brought one is in its comment). */
#ifndef OPENRTG_OPENRTG_H
#define OPENRTG_OPENRTG_H

#define OPENRTG_NAME    "gfopenrtg.library"
#define OPENRTG_VERSION 0
/* lib_Revision to check before a call: ORTG_Present and ORTG_PresentDone with
 * kinds VIRGL, MEMORY and MEDIA (0.30); kind RETAINED (0.31). */
#define ORTG_PRESENT_REVISION   30
#define ORTG_RETAINED_REVISION  31

/* One mode of a monitor (ORTG_GetMode). */
struct OpenRTGMode {
    unsigned long  mode_id;
    unsigned short width, height;
    unsigned char  depth;          /* 8, 16 or 32 */
    unsigned char  format;         /* 0: 8-bit CLUT, 1: 16-bit, 2: 32-bit ARGB */
    unsigned char  standard;       /* 1: in the Standard list */
    char           name[32];       /* "GFOpenRTG.1: 1920x1080 32-bit" */
    unsigned char  native;         /* 0.17: 1 for a Native mode (the struct's last byte, padding before) */
};

/* The Native modes (0.17, the native screen mode): on an ACRTG board of protocol v5 or later, the
 * host's window can ask for the size of the Amiga's screen. While it does, each RTG monitor lists three
 * more modes, last: "GFOpenRTG.n: Native 8-bit", "16-bit" and "32-bit", at that size. Their ModeIDs never
 * change (the place 0x22 of the mode table): 0x6n221000, 0x6n221100 and 0x6n221200. A screen opened on
 * one gets the size of that moment and keeps it until it is opened again. BestModeIDA never picks one:
 * a program gets a Native mode only by naming it. */
#define ORTG_NATIVE_PLACE   0x22
#define ORTG_IS_NATIVE(id)  ((((id) >> 16) & 0xFFUL) == ORTG_NATIVE_PLACE && ((id) & 0x1000UL))

/* What the host asks of a monitor's board, for ORTG_NativeSize (0.17). */
struct OpenRTGNativeSize {
    unsigned short width, height;              /* the size the host's window asks for; 0 x 0 when none is */
    unsigned long  gen;                        /* up by one each time that size changes (WANT_GEN) */
    unsigned long  ask;                        /* up by one each time a page asks the Amiga to follow now (ASK) */
    unsigned short limit_width, limit_height;  /* the board's largest picture (LIMIT) */
    unsigned long  mode_id;                    /* the monitor's Native 32-bit ModeID */
};

/* A rectangle of pixels in memory, for ORTG_WritePixels and ORTG_ReadPixels
 * (0.4). The formats are CyberGraphX's RECTFMT_ numbers, so its library can
 * pass them on as they are; ORTG_PIX_INDEX is bytes looked up in ctable. */
struct OpenRTGPixels {
    void                *data;              /* row 0 of the memory */
    long                 x, y;              /* the rectangle's corner in it */
    long                 modulo;            /* bytes a row */
    unsigned long        format;            /* ORTG_PIX_ */
    const unsigned long *ctable;            /* ORTG_PIX_INDEX: 256 colours, 0x00RRGGBB */
    long                 width, height;     /* the rectangle */
    long                 dest_width, dest_height;   /* written at this size (0: the same); 0.16: through GFOpenGPU
                                                       for ARGB, 0RGB, and RAW on 16-bit */
};

#define ORTG_PIX_RGB      0UL     /* 3 bytes: R, G, B */
#define ORTG_PIX_RGBA     1UL
#define ORTG_PIX_ARGB     2UL
#define ORTG_PIX_PEN      3UL     /* 1 byte: the pen itself (CyberGraphX's LUT8) */
#define ORTG_PIX_GREY     4UL
#define ORTG_PIX_RAW      5UL     /* the bitmap's own pixels (8-bit: pens; 32-bit: all four bytes, alpha included,
                                     written and read as they are: DESIGN.md, "A 32-bit pixel's alpha byte") */
#define ORTG_PIX_BGR      109UL
#define ORTG_PIX_BGRA     111UL
#define ORTG_PIX_ABGR     113UL
#define ORTG_PIX_0RGB     114UL
#define ORTG_PIX_BGR0     115UL
#define ORTG_PIX_RGB0     116UL
#define ORTG_PIX_0BGR     117UL
#define ORTG_PIX_INDEX    0x100UL /* 1 byte, looked up in ctable */

/* What an GFOpenRTG bitmap is, for ORTG_BitMapInfo (0.4). */
struct OpenRTGBitMapInfo {
    void          *memory;          /* pixel 0,0 */
    unsigned long  bytes_per_row;
    unsigned short width, height;
    unsigned char  depth;           /* 8 */
    unsigned char  format;          /* as OpenRTGMode's */
    unsigned char  monitor;         /* 1-4: in that board's video RAM; 0: fast RAM */
    unsigned char  flags;           /* 0.20: ORTG_BMI_CARD; 0 before */
};
/* 0.20: in video RAM's card part (acrtg-v7), the PC graphics card's own
 * memory: its GPU draws there at full speed, the CPU reads it at a crawl.
 * cybergraphics.library's LockBitMapTags gives no address for it. 0.22:
 * not set when the host shadows the card part (acrtg-v8), where the CPU
 * reads at host speed. */
#define ORTG_BMI_CARD     0x01

/* 0.20: AllocBitMap's tags (BMATags_*, OS 3.2's extended form): ti_Data TRUE
 * asks for a displayable friend that only drawing commands will read (no
 * program locks it, the display never shows it): layers' window surfaces.
 * On a board with a card part it goes there, then below it, then in fast
 * RAM. Other graphics libraries ignore it. */
#define ORTG_BMA_CARD     0x804F5201UL

/* ORTG_WrittenSpans (0.23; decision D7(b), acrtg-v9): what the CPU itself
 * wrote into an GFOpenRTG bitmap in video RAM by address since the last
 * call, a row's piece at a time; never what GFOpenGPU or the board drew.
 * - The first call for a bitmap asks the board to watch it, and answers 0.
 *   A board watches one bitmap: a call for another moves the watch when
 *   it is free (its bitmap freed) or when the other is the one a monitor
 *   shows. A board that can't watch (before acrtg-v9, or a runtime not
 *   asked to) answers 0 always, and is not asked again.
 * - It answers how many spans it put in buf, max at most. When it answers
 *   max, call again for the rest: nothing is lost between calls.
 * - buf NULL: the watch is set as above, and the answer is the address of
 *   a long that is not 0 while there is something to take, or 0 when the
 *   board doesn't watch the bitmap. Reading it costs nothing; the board
 *   sets it at the CPU's first write and clears it once everything is
 *   taken from the board. After a call that answered max there may be
 *   more whatever the long says.
 * layers.library uses it on a composited screen's bitmap: what a program
 * writes into the screen by address goes into the window there. */
struct OpenRTGSpan {
    unsigned short y;               /* the row */
    unsigned short x0, x1;          /* its pixels x0 to x1, both written */
};

/* ORTG_ShowInstead and ORTG_ShownNow (0.28; acrtg-v11): finished frames
 * only, on a display that shows video RAM itself and so shows drawing as
 * it happens. The caller keeps the screen's bitmap for drawing and
 * assembling, copies each finished frame into one of a few bitmaps of its
 * own (AllocBitMap, BMF_DISPLAYABLE friends of the screen's: the same
 * board's video RAM, format, size and bytes a row) with GFOpenGPU, and
 * right after that batch's OGPU_Submit asks for the bitmap to be shown.
 * - ORTG_ShowInstead(screen, picture): the board shows picture where it
 *   shows the screen's bitmap, from the moment everything GFOpenGPU has
 *   been given so far has run; no wait in the call. picture NULL: the
 *   screen's bitmap itself again, at once. 1 when asked, 0 when it can't
 *   be: a board before acrtg-v11, another kind of board, a picture that is
 *   not such a friend, or a screen its monitor does not show now. The
 *   caller then shows its frames in the screen's bitmap as before.
 * - ORTG_ShownNow(screen, picture): 1 while the display itself shows
 *   picture (NULL: the screen's bitmap). The display changes pictures at
 *   its vertical blank, a little after ORTG_ShowInstead: until this
 *   answers 0 for a picture, the picture is still on the glass and must
 *   not be drawn into. With three pictures there is always one that is
 *   neither on the display nor the one asked for last.
 * It ends by itself when another screen comes to the front, the mode
 * changes or either bitmap is freed; ask again with the next frame.
 * layers.library's composited mode uses it for its screens. */

/* A frame the host's GPU rendered through ACVirgl, for ORTG_PresentVirgl
 * (0.15). The host copies the resource's damage box straight into the
 * window's visible rectangles (ACV_PRESENT); nothing passes through the
 * 68k's memory twice. The 68k still decides where, and what is visible. */
struct OpenRTGVirglFrame {
    unsigned long  context;         /* ACVirgl context of the resource */
    unsigned long  handle;          /* the resource (its colour buffer) */
    unsigned short width, height;   /* of the resource */
    short          damage_x, damage_y;  /* the part that changed, in the resource; */
    unsigned short damage_w, damage_h;  /* 0 by 0: all of it */
    unsigned long  flags;           /* ORTG_VIRGL_FLIP_Y: row 0 is the bottom (GL) */
    /* ORTG_VIRGL_SCALE (0.29): the whole frame stretched into a box box_w by
     * box_h at x, y, by the host (ACV_PRESENT_SCALE); damage is not used.
     * Read only when the flag is set, so older callers' structs end above. */
    unsigned short box_w, box_h;
};
#define ORTG_VIRGL_FLIP_Y       1
#define ORTG_VIRGL_SCALE        2

/* ORTG_DrawStats (0.11): how GFOpenRTG drew on its bitmaps, by kind of
 * drawing, since it started or was last reset. Each kind has four counts:
 * pieces GFOpenGPU drew, pieces the CPU drew, and the pixels of each. A piece
 * is one visible rectangle of one call (a call through a window partly
 * covered draws several). GFOpenGPU draws a piece when its command does
 * exactly what GFOpenRTG's CPU code does; everything else stays on the CPU. */
/* ORTG_DrawStats' flags. */
#define ORTG_DRAW_RESET         1   /* the counts back to 0 */
#define ORTG_DRAW_CPU_ONLY      2   /* from now on, draw everything with the CPU code (to compare) */
#define ORTG_DRAW_OPENGPU       4   /* ... and through GFOpenGPU again, where it is exact (the default) */
#define ORTG_STAT_FILL          0   /* RectFill, Text's background, underlines */
#define ORTG_STAT_INVERT        1   /* COMPLEMENT fills */
#define ORTG_STAT_PATTERN       2   /* area patterns */
#define ORTG_STAT_TEMPLATE      3   /* BltTemplate, Text's glyphs, BltPattern's masks */
#define ORTG_STAT_COPY          4   /* plain blits, ClipBlit, ScrollRaster's move */
#define ORTG_STAT_BLIT          5   /* chunky blits with a minterm other than a copy, or a plane mask */
#define ORTG_STAT_SCROLL        6   /* ScrollRaster's uncovered area */
#define ORTG_STAT_SETRAST       7   /* SetRast */
#define ORTG_STAT_LINE          8   /* Draw, PolyDraw */
#define ORTG_STAT_PIXEL         9   /* WritePixel, ReadPixel */
#define ORTG_STAT_CHUNKY        10  /* WriteChunkyPixels, WritePixelArray8, WritePixelLine8 */
#define ORTG_STAT_CHUNKY_READ   11  /* ReadPixelArray8, ReadPixelLine8 */
#define ORTG_STAT_PIXELS        12  /* ORTG_WritePixels: CyberGraphX's and Picasso96API's pixel arrays */
#define ORTG_STAT_PIXELS_READ   13  /* ORTG_ReadPixels */
#define ORTG_STAT_PIXELS_FILL   14  /* ORTG_FillPixels */
#define ORTG_STAT_PIXELS_INVERT 15  /* ORTG_InvertPixels */
#define ORTG_STAT_ALPHA         16  /* ORTG_WritePixelsAlpha */
#define ORTG_STAT_BLIT_PLANAR   17  /* blits from or to a planar bitmap */
#define ORTG_STAT_BLIT_CONVERT  18  /* copies between GFOpenRTG bitmaps of two formats */
#define ORTG_STAT_BLIT_MASKED   19  /* blits through a mask (BltMaskBitMapRastPort) */
#define ORTG_STAT_VIRGL         20  /* ORTG_PresentVirgl: GL frames the host wrote straight in (0.15) */
/* 0.21: GFOpenGPU's batches, not pieces: those left running (where the GPU
 * count is), those waited for, the bytes of sources copied to be left
 * running; the fourth count stays 0 */
#define ORTG_STAT_WAIT          21
/* 0.30: ORTG_Present's routes besides the host writing a frame straight in
 * (that one counts as ORTG_STAT_VIRGL, as before) */
#define ORTG_STAT_PRESENT_READBACK 22   /* a virgl frame read back and written by the CPU */
#define ORTG_STAT_PRESENT_MEMORY   23   /* a frame in memory, written by the CPU (ORTG_WritePixels' way) */
#define ORTG_STAT_PRESENT_MEDIA    24   /* an ACMedia frame written by the host into the visible pieces (MEDIA, 0.30) */
#define ORTG_STAT_COUNT         25
#define ORTG_STAT_NAMES { "fill", "invert", "pattern", "template", "copy", "blit", "scroll", "setrast", "line", \
    "pixel", "chunky", "chunky read", "pixels", "pixels read", "pixels fill", "pixels invert", "alpha", \
    "blit planar", "blit convert", "blit masked", "virgl", "batches", "present readback", "present memory", "present media" }

/* ORTG_Present (0.30): the one present. A finished frame, wherever it lives,
 * into a RastPort: a window (its layer's visible pieces), a screen, or a bare
 * bitmap (a RastPort whose Layer is NULL). GFOpenRTG picks the route:
 *  - VIRGL, an RTG target the host can write: the host writes the frame
 *    straight in (ACV_PRESENT, the visible rectangles);
 *  - VIRGL, anything else: the frame comes back (ACV_GET) and the CPU
 *    writes it, as ORTG_WritePixels does (planar targets too);
 *  - MEMORY: the CPU writes it, as ORTG_WritePixels does;
 *  - MEDIA: an ACMedia frame the caller holds on the host (hostlib's
 *    ACM_HOLD; handle is its answer, width and height its size): an RTG
 *    target of 16 or 32 bits, the host writes it straight into the visible
 *    pieces (ACM_SHOW), so the frame never passes through 68k memory;
 *    anything else, the host writes it into a buffer of GFOpenRTG's and the
 *    CPU writes that (planar targets too). The caller releases the frame
 *    (ACM_RELEASE) once ORTG_Present (or, prepared, ORTG_PresentDone) has
 *    returned, never before: until then it is the present's;
 *  - RETAINED (0.31): the surface's changed tiles into its texture and the
 *    texture drawn into the target by GFOpenGPU, or the changed tiles by the
 *    CPU (library/present_retained.h).
 * ORTG_PRESENT_PREPARE: the present goes in the caller's own request block,
 * which it then runs, so a frame costs no block of its own. ORTG_Present
 * writes it at blk (room words) and returns the words written: 0 when it
 * can't go there (a window, a target the host can't write; nothing is lost:
 * ORTG_PresentDone then presents it the other way). The caller runs its
 * block, then calls ORTG_PresentDone(token, block, where the present
 * starts in it), always, even when 0 words were written. Until then the
 * target is the frame's: nothing else may draw on it.
 * Returns the pixels written (PREPARE: words written), or -1. */
struct ORTG_PresentInfo {
    unsigned long   size;           /* sizeof (struct ORTG_PresentInfo): the struct only grows at the end */
    unsigned long   kind;           /* ORTG_SURF_ */
    unsigned long   context, handle;    /* VIRGL: the ACVirgl context and the resource (its colour buffer); MEDIA: handle the frame */
    void           *memory;         /* MEMORY: row 0 */
    unsigned long   stride, format; /* MEMORY: bytes a row, ORTG_PIX_ */
    unsigned short  width, height;  /* of the source */
    struct RastPort *rp;            /* the target */
    long            x, y;           /* where in it the source's pixel 0, 0 goes */
    struct Rectangle *src;          /* the part of the source (damage); NULL: all of it */
    struct Rectangle *dst;          /* VIRGL: the whole source stretched into this box (by the host); NULL: none */
    unsigned long   flags;          /* ORTG_PRESENT_ */
    unsigned long  *blk;            /* PREPARE: the caller's request block, at the present's place */
    unsigned long   room;           /* PREPARE: words free there */
    void           *token;          /* PREPARE: out, for ORTG_PresentDone */
};
#define ORTG_SURF_VIRGL         1
#define ORTG_SURF_MEMORY        2
#define ORTG_SURF_MEDIA         3   /* 0.30: an ACMedia frame held on the host (ACM_HOLD) */
#define ORTG_SURF_RETAINED      4   /* 0.31: handle is a struct OGPURetained * (gfopengpu/retained.h); width and height
                                       the rectangle it is shown in (0: its own size); x, y its corner; only the
                                       VSYNC flag. Returns OGPU_RetainedPresent's answer: the tiles uploaded, or an
                                       OGPU_RS_ERR_; -1 when GFOpenRTG's screens are off, or into another RTG
                                       system's bitmap of over 8 bits (the caller presents its own way). Programs keep calling OGPU_RetainedPresent, which comes here. */
#define ORTG_PRESENT_FLIP_Y     1   /* VIRGL: row 0 is the bottom (GL) */
#define ORTG_PRESENT_VSYNC      2   /* wait for the display's next frame first */
#define ORTG_PRESENT_PREPARE    4   /* into the caller's block (blk, room, token) */
#define ORTG_PRESENT_ALPHA      8   /* VIRGL into 32-bit RTG: the frame's alpha too (else the top byte as the host
                                       likes it best: 0 where it writes XRGB32) */

#endif
