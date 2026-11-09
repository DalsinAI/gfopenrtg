/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * fsrestore: the Workbench comes back after full screen (patches/amigaos3/
 * 0019). A window on the Workbench goes to real full screen
 * (SDL_WINDOW_FULLSCREEN, a mode change, as a game's F9), back to the
 * window, to SDL_WINDOW_FULLSCREEN_DESKTOP, back again, then to real full
 * screen once more and quits from there (SDL_Quit with the window still
 * full screen).
 *
 *   fsrestore [surface|renderer] [W H] [SECONDS]
 *
 * W x H defaults to 640 x 400 (Canvas Skies' window), SECONDS to 3: how long
 * each step is shown, so a lab run can take a screenshot of every one. Every
 * step prints a line starting "step N" before it starts and its checks after
 * it, and SetEnv FSRESTORE_WAIT 1 makes each step wait for a key (or a
 * click) in the window instead of SECONDS, for a lab that drives it by hand.
 *
 * The checks, each a "pass" or "FAIL" line:
 *   full-N      the front screen is a screen of the program's own, not the
 *               Workbench, in SDL's display mode (real full screen), or
 *               the Workbench's size (desktop)
 *   window-N    after leaving full screen: the Workbench is the front screen
 *               and in its own mode, no screen titled "SDL" is left open,
 *               the window is on the Workbench with its border and title bar,
 *               and its drawing area is W x H again
 *   quit        after SDL_Quit from full screen: the Workbench in front, in
 *               its own mode, as many screens as before
 * The picture: four coloured quarters inside a yellow border and a moving
 * black bar, so a stale or misplaced picture shows on a screenshot.
 * Exit 0 when every check passes, 10 otherwise. */
#include "SDL.h"
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <intuition/intuition.h>

#define TITLE "fsrestore"

static int failures = 0;
static int wait_key = 0;
static int step_no = 0;

static void report(const char *what, int ok, const char *detail)
{
    printf("  %-12s %s  %s\n", what, ok ? "pass" : "FAIL", detail);
    fflush(stdout);
    if (!ok) {
        failures++;
    }
}

static void step(const char *what)
{
    step_no++;
    printf("step %d %s (t=%u ms)\n", step_no, what, (unsigned)SDL_GetTicks());
    fflush(stdout);
}

/* What Intuition shows: the screens front first, and the Workbench's. */
typedef struct Scene {
    int screens;      /* screens open */
    int sdl_screens;  /* of those, titled "SDL" (the module's own) */
    int front_is_wb;
    int front_w, front_h;
    ULONG front_mode, wb_mode;
    int wb_w, wb_h;
    /* the program's window on the Workbench, if it is there */
    int on_wb, bordered, border_top, inner_w, inner_h, left, top;
} Scene;

static void look(const char *when, Scene *sc)
{
    struct Screen *s, *wb;
    struct Window *w;
    ULONG lock;
    char line[640];
    int len = 0;

    SDL_zerop(sc);
    wb = LockPubScreen(NULL);
    lock = LockIBase(0);
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen) {
        int nw = 0;
        for (w = s->FirstWindow; w; w = w->NextWindow) {
            nw++;
        }
        if (sc->screens == 0) {
            sc->front_is_wb = (s == wb);
            sc->front_w = s->Width;
            sc->front_h = s->Height;
            sc->front_mode = GetVPModeID(&s->ViewPort);
        }
        if (s->Title && strcmp((const char *)s->Title, "SDL") == 0 && s != wb) {
            sc->sdl_screens++;
        }
        if (len < (int)sizeof(line) - 80) {
            len += SDL_snprintf(line + len, sizeof(line) - len, "%s%dx%d \"%.12s\"%s (%d windows)",
                                sc->screens ? ", " : "", s->Width, s->Height,
                                s->Title ? (const char *)s->Title : "", s == wb ? " WB" : "", nw);
        }
        sc->screens++;
    }
    if (wb) {
        sc->wb_w = wb->Width;
        sc->wb_h = wb->Height;
        sc->wb_mode = GetVPModeID(&wb->ViewPort);
        for (w = wb->FirstWindow; w; w = w->NextWindow) {
            if (w->Title && strcmp((const char *)w->Title, TITLE) == 0) {
                sc->on_wb = 1;
                sc->bordered = !(w->Flags & WFLG_BORDERLESS) && w->BorderTop > 0;
                sc->border_top = w->BorderTop;
                sc->inner_w = (w->Flags & WFLG_GIMMEZEROZERO) ? w->GZZWidth : w->Width - w->BorderLeft - w->BorderRight;
                sc->inner_h = (w->Flags & WFLG_GIMMEZEROZERO) ? w->GZZHeight : w->Height - w->BorderTop - w->BorderBottom;
                sc->left = w->LeftEdge;
                sc->top = w->TopEdge;
            }
        }
    }
    UnlockIBase(lock);
    if (wb) {
        UnlockPubScreen(NULL, wb);
    }
    printf("  screens %-10s %d, front first: %s\n", when, sc->screens, len ? line : "(none)");
    printf("  front %dx%d mode 0x%08lx%s; Workbench %dx%d mode 0x%08lx\n",
           sc->front_w, sc->front_h, (unsigned long)sc->front_mode,
           sc->front_is_wb ? " (the Workbench)" : "",
           sc->wb_w, sc->wb_h, (unsigned long)sc->wb_mode);
    if (sc->on_wb) {
        printf("  window on the Workbench at %d,%d, inside %dx%d, border top %d%s\n",
               sc->left, sc->top, sc->inner_w, sc->inner_h, sc->border_top,
               sc->bordered ? "" : " (NO BORDER)");
    }
    fflush(stdout);
}

static void describe(SDL_Window *win, const char *when)
{
    SDL_DisplayMode wm, cm, dm;
    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    SDL_zero(wm);
    SDL_zero(cm);
    SDL_zero(dm);
    SDL_GetWindowDisplayMode(win, &wm);
    SDL_GetCurrentDisplayMode(0, &cm);
    SDL_GetDesktopDisplayMode(0, &dm);
    printf("  sdl %-10s window %dx%d flags 0x%x; window's mode %dx%d; display %dx%d %s; desktop %dx%d\n",
           when, w, h, (unsigned)SDL_GetWindowFlags(win), wm.w, wm.h, cm.w, cm.h,
           SDL_GetPixelFormatName(cm.format), dm.w, dm.h);
    fflush(stdout);
}

static void draw(Uint32 *px, int pitch_px, int w, int h, int frame, const SDL_PixelFormat *fmt)
{
    Uint32 red = SDL_MapRGB(fmt, 255, 0, 0), green = SDL_MapRGB(fmt, 0, 200, 0);
    Uint32 blue = SDL_MapRGB(fmt, 0, 0, 255), white = SDL_MapRGB(fmt, 255, 255, 255);
    Uint32 yellow = SDL_MapRGB(fmt, 255, 255, 0), black = SDL_MapRGB(fmt, 0, 0, 0);
    int bar = h > 8 ? (frame * 4) % (h - 8) : 0;
    int x, y;

    for (y = 0; y < h; y++) {
        Uint32 *row = px + y * pitch_px;
        for (x = 0; x < w; x++) {
            Uint32 c;
            if (x < 4 || y < 4 || x >= w - 4 || y >= h - 4) {
                c = yellow;
            } else if (y >= bar && y < bar + 8) {
                c = black;
            } else if (y < h / 2) {
                c = x < w / 2 ? red : green;
            } else {
                c = x < w / 2 ? blue : white;
            }
            row[x] = c;
        }
    }
}

typedef struct Out {
    SDL_Renderer *ren;
    SDL_Texture *tex;
    int tw, th;
    int drawn_w, drawn_h;
} Out;

/* Draw for ms milliseconds (or, with FSRESTORE_WAIT, until a key or click).
   Returns 0, or -1 when the program is asked to quit. */
static int show(SDL_Window *win, Out *o, Uint32 ms)
{
    Uint32 t0 = SDL_GetTicks();
    int frame = 0, done = 0;
    SDL_Event ev;
    SDL_PixelFormat *fmt = o->ren ? SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888) : NULL;

    while (!done) {
        if (o->ren) {
            void *px;
            int pitch;
            if (SDL_LockTexture(o->tex, NULL, &px, &pitch) == 0) {
                draw((Uint32 *)px, pitch / 4, o->tw, o->th, frame, fmt);
                SDL_UnlockTexture(o->tex);
            }
            SDL_RenderClear(o->ren);
            SDL_RenderCopy(o->ren, o->tex, NULL, NULL);
            SDL_RenderPresent(o->ren);
            SDL_GetRendererOutputSize(o->ren, &o->drawn_w, &o->drawn_h);
        } else {
            SDL_Surface *s = SDL_GetWindowSurface(win);
            if (!s) {
                printf("  SDL_GetWindowSurface: %s\n", SDL_GetError());
                break;
            }
            o->drawn_w = s->w;
            o->drawn_h = s->h;
            if (s->format->BytesPerPixel == 4) {
                SDL_LockSurface(s);
                draw((Uint32 *)s->pixels, s->pitch / 4, s->w, s->h, frame, s->format);
                SDL_UnlockSurface(s);
            } else {
                SDL_FillRect(s, NULL, SDL_MapRGB(s->format, 255, 0, 255));
            }
            SDL_UpdateWindowSurface(win);
        }
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                if (fmt) SDL_FreeFormat(fmt);
                return -1;
            }
            if (wait_key && (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN)) {
                done = 1;
            }
        }
        if (!wait_key && SDL_GetTicks() - t0 >= ms) {
            done = 1;
        }
        frame++;
        SDL_Delay(20);
    }
    if (fmt) SDL_FreeFormat(fmt);
    return 0;
}

static void check_full(const char *what, SDL_Window *win, Out *o, int desktop)
{
    char line[256];
    SDL_DisplayMode cm;
    Scene sc;
    look("full", &sc);
    SDL_zero(cm);
    SDL_GetCurrentDisplayMode(0, &cm);
    SDL_snprintf(line, sizeof(line), "front %dx%d%s, display mode %dx%d, drawn %dx%d",
                 sc.front_w, sc.front_h, sc.front_is_wb ? " (Workbench)" : " (own screen)",
                 cm.w, cm.h, o->drawn_w, o->drawn_h);
    if (desktop) {
        report(what, sc.front_w == sc.wb_w && sc.front_h == sc.wb_h &&
                     o->drawn_w == sc.wb_w && o->drawn_h == sc.wb_h, line);
    } else {
        report(what, !sc.front_is_wb && sc.front_w == cm.w && sc.front_h == cm.h, line);
    }
}

static void check_window(const char *what, int w, int h, int before)
{
    char line[320];
    Scene sc;
    look("windowed", &sc);
    SDL_snprintf(line, sizeof(line),
                 "front %s, mode 0x%08lx (Workbench's 0x%08lx), %d SDL screens left, %d screens (%d before), "
                 "window %s, %s, inside %dx%d (want %dx%d)",
                 sc.front_is_wb ? "the Workbench" : "NOT the Workbench",
                 (unsigned long)sc.front_mode, (unsigned long)sc.wb_mode, sc.sdl_screens, sc.screens, before,
                 sc.on_wb ? "on the Workbench" : "NOT on the Workbench",
                 sc.bordered ? "with its border" : "WITHOUT its border",
                 sc.inner_w, sc.inner_h, w, h);
    report(what, sc.front_is_wb && sc.front_mode == sc.wb_mode && sc.sdl_screens == 0 &&
                 sc.screens == before && sc.on_wb && sc.bordered &&
                 sc.inner_w == w && sc.inner_h == h, line);
}

static int go_full(SDL_Window *win, Uint32 flags, const char *name)
{
    if (SDL_SetWindowFullscreen(win, flags) < 0) {
        report(name, 0, SDL_GetError());
        return -1;
    }
    describe(win, name);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *mode = argc > 1 ? argv[1] : "surface";
    int w = argc > 3 ? SDL_atoi(argv[2]) : 640;
    int h = argc > 3 ? SDL_atoi(argv[3]) : 400;
    Uint32 ms = (Uint32)(argc > 4 ? SDL_atoi(argv[4]) : 3) * 1000;
    char line[200];
    Scene sc;
    int before;
    SDL_Window *win;
    Out o;

    if (strcmp(mode, "surface") != 0 && strcmp(mode, "renderer") != 0) {
        printf("usage: fsrestore [surface|renderer] [W H] [SECONDS]\n");
        return 20;
    }
    wait_key = SDL_getenv("FSRESTORE_WAIT") != NULL;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    printf("fsrestore: %s %dx%d, %s, video driver %s\n", mode, w, h,
           wait_key ? "each step until a key" : "each step for SECONDS", SDL_GetCurrentVideoDriver());
    look("before", &sc);
    before = sc.screens;

    step("window");
    win = SDL_CreateWindow(TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
    if (!win) {
        printf("SDL_CreateWindow: %s\n", SDL_GetError());
        return 20;
    }
    SDL_zero(o);
    if (strcmp(mode, "renderer") == 0) {
        SDL_RendererInfo info;
        o.ren = SDL_CreateRenderer(win, -1, 0);
        if (!o.ren) {
            printf("SDL_CreateRenderer: %s\n", SDL_GetError());
            return 20;
        }
        SDL_GetRendererInfo(o.ren, &info);
        printf("  renderer %s\n", info.name);
        o.tw = w;
        o.th = h;
        o.tex = SDL_CreateTexture(o.ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    }
    describe(win, "window");
    if (show(win, &o, ms) < 0) goto quit;
    check_window("window-0", w, h, before);

    step("SDL_SetWindowFullscreen(SDL_WINDOW_FULLSCREEN)");
    if (go_full(win, SDL_WINDOW_FULLSCREEN, "full-1") == 0) {
        if (show(win, &o, ms) < 0) goto quit;
        check_full("full-1", win, &o, 0);
    }

    step("SDL_SetWindowFullscreen(0): back to the window");
    SDL_SetWindowFullscreen(win, 0);
    describe(win, "window-1");
    if (show(win, &o, ms) < 0) goto quit;
    check_window("window-1", w, h, before);

    step("SDL_SetWindowFullscreen(SDL_WINDOW_FULLSCREEN_DESKTOP)");
    if (go_full(win, SDL_WINDOW_FULLSCREEN_DESKTOP, "desktop-2") == 0) {
        if (show(win, &o, ms) < 0) goto quit;
        check_full("desktop-2", win, &o, 1);
    }

    step("SDL_SetWindowFullscreen(0): back to the window");
    SDL_SetWindowFullscreen(win, 0);
    describe(win, "window-2");
    if (show(win, &o, ms) < 0) goto quit;
    check_window("window-2", w, h, before);

    step("SDL_SetWindowFullscreen(SDL_WINDOW_FULLSCREEN), then quit from it");
    if (go_full(win, SDL_WINDOW_FULLSCREEN, "full-3") == 0) {
        if (show(win, &o, ms) < 0) goto quit;
        check_full("full-3", win, &o, 0);
    }

quit:
    step("SDL_Quit with the window as it is (full screen)");
    /* no SDL_DestroyWindow: SDL_Quit destroys it, full screen, as a game
       that quits from full screen does */
    SDL_Quit();
    look("after", &sc);
    SDL_snprintf(line, sizeof(line),
                 "front %s, mode 0x%08lx (Workbench's 0x%08lx), %d SDL screens left, %d screens (%d before)",
                 sc.front_is_wb ? "the Workbench" : "NOT the Workbench",
                 (unsigned long)sc.front_mode, (unsigned long)sc.wb_mode, sc.sdl_screens, sc.screens, before);
    report("quit", sc.front_is_wb && sc.front_mode == sc.wb_mode && sc.sdl_screens == 0 &&
                   sc.screens == before, line);
    printf("fsrestore: %s\n", failures ? "FAILED" : "all passed");
    fflush(stdout);
    return failures ? 10 : 0;
}
