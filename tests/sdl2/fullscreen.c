/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * fullscreen: SDL 2's real full-screen mode (SDL_WINDOW_FULLSCREEN, a screen
 * of the program's own in the display mode SDL picks) on AmigaOS 3.
 *
 *   fullscreen [surface|renderer|toggle|desktop] [W H] [SECONDS]
 *
 *   surface   SDL_CreateWindow(W, H, SDL_WINDOW_FULLSCREEN), drawn through
 *             SDL_GetWindowSurface and SDL_UpdateWindowSurface (as Powermanga)
 *   renderer  the same window drawn through SDL_CreateRenderer and a
 *             streaming texture of W x H (default)
 *   toggle    a W x H window with a renderer, then SDL_SetWindowFullscreen
 *             to full screen, back to the window and to full screen again
 *             (as OpenTyrian's Alt+Return)
 *   desktop   SDL_WINDOW_FULLSCREEN_DESKTOP, for comparison
 *
 * W x H defaults to 320 x 200; each picture is shown for SECONDS (default
 * 4). The picture: four coloured quarters (red, green, blue, white) inside a
 * yellow border two pixels wide, and a black bar moving down, so a cut-off,
 * smeared or stale picture is plain on a screenshot. Each step logs SDL's
 * window size, display mode and surface or output size, and the screens
 * Intuition has open, front first. The checks: while full screen, the front
 * screen is the program's own, in the display mode SDL reports, and the
 * program draws to all of it; when the program has left full screen and
 * when it ends, no screen of its own is left open. Exit 0 when every check
 * passes. */
#include "SDL.h"
#include <stdio.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <intuition/intuition.h>

static int failures = 0;

static void report(const char *what, int ok, const char *detail)
{
    printf("%-16s %s  %s\n", what, ok ? "pass" : "FAIL", detail);
    fflush(stdout);
    if (!ok) {
        failures++;
    }
}

/* The screens Intuition has open, front first; how many, and the front
   one's size. */
static int screens(const char *when, int *front_w, int *front_h)
{
    struct Screen *s;
    ULONG lock;
    int n = 0;
    char line[512];
    int len = 0;

    lock = LockIBase(0);
    for (s = IntuitionBase->FirstScreen; s; s = s->NextScreen) {
        if (n == 0) {
            if (front_w) *front_w = s->Width;
            if (front_h) *front_h = s->Height;
        }
        if (len < (int)sizeof(line) - 64) {
            struct Window *w;
            int nw = 0;
            for (w = s->FirstWindow; w; w = w->NextWindow) {
                nw++;
            }
            len += SDL_snprintf(line + len, sizeof(line) - len, "%s%dx%d \"%.12s\" (%d windows)",
                                n ? ", " : "", s->Width, s->Height,
                                s->Title ? (const char *)s->Title : "", nw);
        }
        n++;
    }
    UnlockIBase(lock);
    printf("  screens %-12s %d: %s\n", when, n, len ? line : "(none)");
    fflush(stdout);
    return n;
}

static void draw(Uint32 *px, int pitch_px, int w, int h, int frame, const SDL_PixelFormat *fmt)
{
    Uint32 red = SDL_MapRGB(fmt, 255, 0, 0), green = SDL_MapRGB(fmt, 0, 200, 0);
    Uint32 blue = SDL_MapRGB(fmt, 0, 0, 255), white = SDL_MapRGB(fmt, 255, 255, 255);
    Uint32 yellow = SDL_MapRGB(fmt, 255, 255, 0), black = SDL_MapRGB(fmt, 0, 0, 0);
    int bar = (frame * 4) % h;
    int x, y;

    for (y = 0; y < h; y++) {
        Uint32 *row = px + y * pitch_px;
        for (x = 0; x < w; x++) {
            Uint32 c;
            if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2) {
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

static void describe(SDL_Window *win, const char *when)
{
    SDL_DisplayMode wm, cm;
    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    SDL_zero(wm);
    SDL_zero(cm);
    SDL_GetWindowDisplayMode(win, &wm);
    SDL_GetCurrentDisplayMode(0, &cm);
    printf("  %-14s window %dx%d, flags 0x%x, window's mode %dx%d, display's mode %dx%d %s\n",
           when, w, h, (unsigned)SDL_GetWindowFlags(win), wm.w, wm.h, cm.w, cm.h,
           SDL_GetPixelFormatName(cm.format));
    fflush(stdout);
}

/* While full screen: the front screen is the window's, in the mode SDL
   reports, and the window covers it. */
static void check_front(const char *what, SDL_Window *win, int out_w, int out_h)
{
    char line[200];
    SDL_DisplayMode cm;
    int fw = 0, fh = 0, w, h;
    screens("full screen", &fw, &fh);
    SDL_GetCurrentDisplayMode(0, &cm);
    SDL_GetWindowSize(win, &w, &h);
    SDL_snprintf(line, sizeof(line),
                 "front screen %dx%d, display mode %dx%d, window %dx%d, drawn %dx%d",
                 fw, fh, cm.w, cm.h, w, h, out_w, out_h);
    report(what, fw == cm.w && fh == cm.h && w == cm.w && h == cm.h &&
                 out_w == cm.w && out_h == cm.h, line);
}

static int show_surface(SDL_Window *win, Uint32 ms, int *sw, int *sh)
{
    Uint32 t0 = SDL_GetTicks();
    int frame = 0;
    SDL_Event ev;
    while (SDL_GetTicks() - t0 < ms) {
        SDL_Surface *s = SDL_GetWindowSurface(win);
        if (!s) {
            printf("  SDL_GetWindowSurface: %s\n", SDL_GetError());
            return -1;
        }
        *sw = s->w;
        *sh = s->h;
        if (s->format->BytesPerPixel == 4) {
            SDL_LockSurface(s);
            draw((Uint32 *)s->pixels, s->pitch / 4, s->w, s->h, frame, s->format);
            SDL_UnlockSurface(s);
        } else {
            SDL_FillRect(s, NULL, SDL_MapRGB(s->format, 255, 0, 255));
        }
        SDL_UpdateWindowSurface(win);
        while (SDL_PollEvent(&ev)) {
        }
        frame++;
        SDL_Delay(20);
    }
    return frame;
}

static int show_renderer(SDL_Renderer *ren, SDL_Texture *tex, int tw, int th, Uint32 ms, int *ow, int *oh)
{
    Uint32 t0 = SDL_GetTicks();
    int frame = 0;
    SDL_Event ev;
    SDL_PixelFormat *fmt = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
    while (SDL_GetTicks() - t0 < ms) {
        void *px;
        int pitch;
        if (SDL_LockTexture(tex, NULL, &px, &pitch) == 0) {
            draw((Uint32 *)px, pitch / 4, tw, th, frame, fmt);
            SDL_UnlockTexture(tex);
        }
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_GetRendererOutputSize(ren, ow, oh);
        while (SDL_PollEvent(&ev)) {
        }
        frame++;
        SDL_Delay(20);
    }
    SDL_FreeFormat(fmt);
    return frame;
}

int main(int argc, char *argv[])
{
    const char *mode = argc > 1 ? argv[1] : "renderer";
    int w = argc > 3 ? SDL_atoi(argv[2]) : 320;
    int h = argc > 3 ? SDL_atoi(argv[3]) : 200;
    Uint32 ms = (Uint32)(argc > 4 ? SDL_atoi(argv[4]) : 4) * 1000;
    int before, after, ow = 0, oh = 0;
    char line[160];
    SDL_Window *win;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    printf("fullscreen: %s %dx%d, video driver %s\n", mode, w, h, SDL_GetCurrentVideoDriver());
    before = screens("before", NULL, NULL);

    if (strcmp(mode, "surface") == 0) {
        win = SDL_CreateWindow("fullscreen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
                               SDL_WINDOW_FULLSCREEN);
        if (!win) {
            printf("SDL_CreateWindow: %s\n", SDL_GetError());
            return 20;
        }
        describe(win, "created");
        show_surface(win, ms, &ow, &oh);
        describe(win, "shown");
        check_front("surface", win, ow, oh);
        SDL_DestroyWindow(win);
    } else if (strcmp(mode, "renderer") == 0 || strcmp(mode, "desktop") == 0 || strcmp(mode, "toggle") == 0) {
        int toggle = strcmp(mode, "toggle") == 0;
        int desktop = strcmp(mode, "desktop") == 0;
        Uint32 flags = toggle ? 0 : desktop ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
        SDL_Renderer *ren;
        SDL_Texture *tex;
        win = SDL_CreateWindow("fullscreen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
        if (!win) {
            printf("SDL_CreateWindow: %s\n", SDL_GetError());
            return 20;
        }
        ren = SDL_CreateRenderer(win, -1, 0);
        if (!ren) {
            printf("SDL_CreateRenderer: %s\n", SDL_GetError());
            return 20;
        }
        {
            SDL_RendererInfo info;
            SDL_GetRendererInfo(ren, &info);
            printf("  renderer %s\n", info.name);
        }
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        describe(win, "created");
        show_renderer(ren, tex, w, h, ms, &ow, &oh);
        describe(win, "shown");
        if (desktop) {
            int fw = 0, fh = 0;
            screens("desktop", &fw, &fh);
            SDL_snprintf(line, sizeof(line), "drawn %dx%d, front screen %dx%d", ow, oh, fw, fh);
            report("desktop", ow == fw && oh == fh, line);
        } else if (!toggle) {
            check_front("renderer", win, ow, oh);
        } else {
            int round;
            screens("windowed", NULL, NULL);
            for (round = 1; round <= 2; round++) {
                int n;
                if (SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN) < 0) {
                    report("toggle-full", 0, SDL_GetError());
                    break;
                }
                describe(win, "full screen");
                show_renderer(ren, tex, w, h, ms, &ow, &oh);
                check_front(round == 1 ? "toggle-full-1" : "toggle-full-2", win, ow, oh);
                SDL_SetWindowFullscreen(win, 0);
                describe(win, "windowed");
                show_renderer(ren, tex, w, h, ms, &ow, &oh);
                n = screens("windowed", NULL, NULL);
                SDL_snprintf(line, sizeof(line), "%d screens open after leaving full screen (%d before), drawn %dx%d",
                             n, before, ow, oh);
                report(round == 1 ? "toggle-window-1" : "toggle-window-2", n == before && ow == w && oh == h, line);
            }
        }
        SDL_DestroyTexture(tex);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
    } else {
        printf("usage: fullscreen [surface|renderer|toggle|desktop] [W H] [SECONDS]\n");
        return 20;
    }

    SDL_Quit();
    after = screens("after", NULL, NULL);
    SDL_snprintf(line, sizeof(line), "%d screens open after SDL_Quit (%d before)", after, before);
    report("closed", after == before, line);
    printf("fullscreen: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 10 : 0;
}
