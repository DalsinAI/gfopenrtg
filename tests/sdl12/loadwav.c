/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL 1.2's RWops on SDL2.module (library/modules/sdl12, patches/0002):
 * SDL_LoadWAV from a file and from memory, and a file RWops read, sought and
 * closed by the program itself. Before patches/0002 the first of them never
 * came back. Writes a short 8-bit mono WAV to T:, prints one line for each
 * check and "loadwav: PASS" or "loadwav: FAIL"; return code 0 or 10.
 *   loadwav [FILE.wav]   also loads FILE.wav */
#include <stdio.h>
#include <string.h>
#include "SDL.h"

#define FRAMES 2205
#define NAME "T:sdl12-loadwav.wav"

static int failed;

static void check(int ok, const char *what)
{
    printf("loadwav: %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failed = 1;
}

static void le32(unsigned char *p, unsigned long v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void le16(unsigned char *p, unsigned v)
{
    p[0] = v; p[1] = v >> 8;
}

/* RIFF WAVE, PCM, 8-bit mono, 22050 Hz, FRAMES frames of a ramp. */
static unsigned long make_wav(unsigned char *w)
{
    unsigned long i;
    memcpy(w, "RIFF", 4); le32(w + 4, 36 + FRAMES); memcpy(w + 8, "WAVEfmt ", 8);
    le32(w + 16, 16); le16(w + 20, 1); le16(w + 22, 1); le32(w + 24, 22050);
    le32(w + 28, 22050); le16(w + 32, 1); le16(w + 34, 8);
    memcpy(w + 36, "data", 4); le32(w + 40, FRAMES);
    for (i = 0; i < FRAMES; i++)
        w[44 + i] = (unsigned char)i;
    return 44 + FRAMES;
}

static void loaded(SDL_AudioSpec *spec, Uint8 *buf, Uint32 len, const char *what)
{
    char line[96];
    sprintf(line, "%s (%d Hz, format 0x%04x, %d channel(s), %lu bytes)", what,
            spec ? spec->freq : 0, spec ? spec->format : 0, spec ? spec->channels : 0, (unsigned long)len);
    check(spec && spec->freq == 22050 && spec->format == AUDIO_U8 && spec->channels == 1
          && len == FRAMES && buf && buf[100] == 100, line);
    if (spec)
        SDL_FreeWAV(buf);
}

int main(int argc, char **argv)
{
    static unsigned char wav[44 + FRAMES];
    unsigned char head[12];
    unsigned long n = make_wav(wav);
    SDL_AudioSpec spec, *got;
    SDL_RWops *rw;
    Uint8 *buf;
    Uint32 len;
    FILE *f;

    if (SDL_Init(SDL_INIT_NOPARACHUTE) < 0) {
        printf("loadwav: SDL_Init: %s\nloadwav: FAIL\n", SDL_GetError());
        return 10;
    }
    f = fopen(NAME, "wb");
    check(f && fwrite(wav, 1, n, f) == n && fclose(f) == 0, "write " NAME);

    got = SDL_LoadWAV(NAME, &spec, &buf, &len);
    loaded(got, buf, len, "SDL_LoadWAV from a file");

    got = SDL_LoadWAV_RW(SDL_RWFromMem(wav, (int)n), 1, &spec, &buf, &len);
    loaded(got, buf, len, "SDL_LoadWAV_RW from memory");

    rw = SDL_RWFromFile(NAME, "rb");
    check(rw != NULL, "SDL_RWFromFile");
    if (rw) {
        check(SDL_RWread(rw, head, 4, 3) == 3 && !memcmp(head, "RIFF", 4) && !memcmp(head + 8, "WAVE", 4),
              "SDL_RWread of the header");
        check(SDL_RWseek(rw, 0, RW_SEEK_END) == (int)n, "SDL_RWseek to the end");
        check(SDL_RWseek(rw, 144, RW_SEEK_SET) == 144 && SDL_RWread(rw, head, 1, 1) == 1 && head[0] == 100,
              "SDL_RWseek and SDL_RWread in the data");
        check(SDL_RWclose(rw) == 0, "SDL_RWclose");
    }

    if (argc > 1) {
        got = SDL_LoadWAV(argv[1], &spec, &buf, &len);
        printf("loadwav: %s %s: %d Hz, format 0x%04x, %d channel(s), %lu bytes\n", got ? "ok  " : "FAIL",
               argv[1], got ? spec.freq : 0, got ? spec.format : 0, got ? spec.channels : 0,
               got ? (unsigned long)len : 0UL);
        if (got)
            SDL_FreeWAV(buf);
        else
            failed = 1;
    }

    remove(NAME);
    SDL_Quit();
    printf("loadwav: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 10 : 0;
}
