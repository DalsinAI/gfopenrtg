/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * blendcheck: SDL 2's ARGB8888 blend blit (BlitRGBtoRGBPixelAlpha) on
 * AmigaOS 3, with and without hostlib's SDL2_BLEND_ARGB
 * (library/modules/sdl2/src/video/amigaos3/SDL_os3blend.c), one line each.
 *
 * Each case blits the same source onto the same destination three times:
 * with SDL_OS3_HOSTLIB_BLEND "0" (SDL's own loop, the reference), "force"
 * (the hostlib call even where the probe says no: the 68k body off
 * AmiCradle, the host on it) and unset (what a program gets: the probe
 * decides). The destination after each must be the reference's, byte for
 * byte. The cases:
 *
 *   small     17x13 into a 32x32 surface at (5,7)
 *   full      640x480, more rows than one call's 64 KB of state: in strips
 *   pitch     320x200 out of a 1024-wide source into an 800-wide surface
 *   wide      16500x2, a row over 64 KB: SDL's loop does it all
 *   xrgb      200x100 onto RGB888 (no alpha in the destination)
 *
 * Then "time" blits 640x480 100 times with "0" and unset and prints the
 * milliseconds of each (no pass or fail: on AmiCradle the second is the
 * host's). The source has fully clear, fully opaque and partly clear
 * pixels. Run on an Amiga with SDL2.module (blendcheck.m) or linked
 * statically (blendcheck). Exit 0 when every case passes. */
#include "SDL.h"
#include <stdio.h>
#include <string.h>

#define HINT "SDL_OS3_HOSTLIB_BLEND"

static int failures = 0;
static Uint32 seed = 0x2468ace1u;

static Uint32 rnd(void)
{
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

static void fill(SDL_Surface *s, int alpha_mix)
{
    int x, y;
    for (y = 0; y < s->h; y++) {
        Uint32 *p = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
        for (x = 0; x < s->w; x++) {
            Uint32 v = rnd() >> 8 & 0xffffff, a = rnd() >> 24;
            if (alpha_mix) {
                switch (rnd() >> 30) {
                case 0: a = 0; break;
                case 1: a = 255; break;
                default: break;
                }
            }
            p[x] = a << 24 | v;
        }
    }
}

static Uint32 fnv(const SDL_Surface *s)
{
    Uint32 h = 2166136261u;
    size_t i, n = (size_t)s->pitch * s->h;
    const Uint8 *p = s->pixels;
    for (i = 0; i < n; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

static void blit(const char *mode, SDL_Surface *src, SDL_Rect *sr, SDL_Surface *dst, const SDL_Surface *orig, int x, int y)
{
    SDL_Rect dr;
    dr.x = x;
    dr.y = y;
    dr.w = sr->w;
    dr.h = sr->h;
    SDL_SetHint(HINT, mode);
    memcpy(dst->pixels, orig->pixels, (size_t)dst->pitch * dst->h);
    if (SDL_BlitSurface(src, sr, dst, &dr) < 0) {
        printf("  blit failed: %s\n", SDL_GetError());
    }
}

static void run(const char *name, Uint32 dfmt, int sw, int sh, int rx, int ry, int rw, int rh, int dw, int dh, int x, int y)
{
    static const char *const modes[2] = { "force", NULL };
    static const char *const label[2] = { "force", "probe" };
    SDL_Surface *src = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, dfmt);
    SDL_Surface *orig = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, dfmt);
    SDL_Surface *ref = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, dfmt);
    SDL_Rect sr;
    char detail[160];
    int i, ok = 1, changed;
    if (!src || !dst || !orig || !ref) {
        printf("%-8s FAIL  no memory for the surfaces\n", name);
        failures++;
        goto out;
    }
    SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
    fill(src, 1);
    fill(orig, 0);
    sr.x = rx;
    sr.y = ry;
    sr.w = rw;
    sr.h = rh;
    blit("0", src, &sr, ref, orig, x, y);
    changed = memcmp(ref->pixels, orig->pixels, (size_t)ref->pitch * ref->h) != 0;
    snprintf(detail, sizeof detail, "%dx%d, SDL's loop %08lx", rw, rh, (unsigned long)fnv(ref));
    for (i = 0; i < 2; i++) {
        blit(modes[i], src, &sr, dst, orig, x, y);
        if (memcmp(dst->pixels, ref->pixels, (size_t)dst->pitch * dst->h)) {
            ok = 0;
            snprintf(detail + strlen(detail), sizeof detail - strlen(detail), ", %s %08lx DIFFERS", label[i], (unsigned long)fnv(dst));
        }
    }
    if (!changed) {
        ok = 0;
        snprintf(detail + strlen(detail), sizeof detail - strlen(detail), ", the blit wrote nothing");
    }
    printf("%-8s %s  %s\n", name, ok ? "pass" : "FAIL", detail);
    fflush(stdout);
    failures += !ok;
out:
    SDL_FreeSurface(src);
    SDL_FreeSurface(dst);
    SDL_FreeSurface(orig);
    SDL_FreeSurface(ref);
}

static void timing(void)
{
    static const char *const modes[2] = { "0", NULL };
    SDL_Surface *src = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    Uint32 ms[2];
    int i, n;
    if (!src || !dst) {
        printf("time     FAIL  no memory for the surfaces\n");
        failures++;
        SDL_FreeSurface(src);
        SDL_FreeSurface(dst);
        return;
    }
    SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
    fill(src, 1);
    fill(dst, 0);
    for (i = 0; i < 2; i++) {
        Uint32 t;
        SDL_SetHint(HINT, modes[i]);
        t = SDL_GetTicks();
        for (n = 0; n < 100; n++) {
            SDL_BlitSurface(src, NULL, dst, NULL);
        }
        ms[i] = SDL_GetTicks() - t;
    }
    printf("time     ----  640x480 x100: SDL's loop %lu ms, probe's choice %lu ms\n", (unsigned long)ms[0], (unsigned long)ms[1]);
    fflush(stdout);
    SDL_FreeSurface(src);
    SDL_FreeSurface(dst);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (SDL_Init(SDL_INIT_TIMER) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    run("small", SDL_PIXELFORMAT_ARGB8888, 17, 13, 0, 0, 17, 13, 32, 32, 5, 7);
    run("full", SDL_PIXELFORMAT_ARGB8888, 640, 480, 0, 0, 640, 480, 640, 480, 0, 0);
    run("pitch", SDL_PIXELFORMAT_ARGB8888, 1024, 240, 100, 20, 320, 200, 800, 300, 33, 50);
    run("wide", SDL_PIXELFORMAT_ARGB8888, 16500, 2, 0, 0, 16500, 2, 16500, 2, 0, 0);
    run("xrgb", SDL_PIXELFORMAT_RGB888, 200, 100, 0, 0, 200, 100, 256, 128, 10, 10);
    timing();
    SDL_Quit();
    printf("%s\n", failures ? "blendcheck: FAIL" : "blendcheck: all pass");
    return failures ? 5 : 0;
}
