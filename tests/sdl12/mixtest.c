/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libSDL_mixer.a on the Amiga (library/modules/sdl12/mixer): what the host
 * test can't reach, AHI itself. Writes a short 16-bit mono WAV (a 440 Hz
 * tone) to T:, plays it on a channel once, three times and timed, and as
 * music; then FILE (an Ogg, say) as music for SECONDS, saying what decoded
 * it, and the WAV beside it in its place when that is what the mixer chose.
 * One line for each check, then "mixtest: PASS" or "mixtest: FAIL"; return
 * code 0 or 10.
 *   mixtest [FILE [SECONDS]] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL.h"
#include "SDL_mixer.h"

#define RATE 22050
#define FRAMES (RATE / 4)               /* a quarter of a second */
#define NAME "T:sdl12-mixtest.wav"

static int failed;
static volatile int finished_ch = -1, finished_n, music_done;

static void check(int ok, const char *what)
{
    printf("mixtest: %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failed = 1;
}

static void on_channel(int ch)
{
    finished_ch = ch;
    finished_n++;
}

static void on_music(void)
{
    music_done = 1;
}

static void le32(unsigned char *p, unsigned long v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void le16(unsigned char *p, unsigned v)
{
    p[0] = v; p[1] = v >> 8;
}

static int write_tone(void)
{
    static unsigned char d[44 + FRAMES * 2];
    FILE *f;
    int i;
    memcpy(d, "RIFF", 4); le32(d + 4, sizeof d - 8); memcpy(d + 8, "WAVEfmt ", 8);
    le32(d + 16, 16); le16(d + 20, 1); le16(d + 22, 1); le32(d + 24, RATE); le32(d + 28, RATE * 2);
    le16(d + 32, 2); le16(d + 34, 16); memcpy(d + 36, "data", 4); le32(d + 40, FRAMES * 2);
    for (i = 0; i < FRAMES; i++) {
        /* a triangle at 441 Hz: 50 frames a period */
        int ph = i % 50, v = ph < 25 ? ph * 800 - 10000 : (50 - ph) * 800 - 10000;
        le16(d + 44 + i * 2, (unsigned)(v & 0xFFFF));
    }
    if (!(f = fopen(NAME, "wb")))
        return 0;
    i = fwrite(d, 1, sizeof d, f) == sizeof d;
    fclose(f);
    return i;
}

static void wait_ms(int ms)
{
    SDL_Delay(ms);
}

int main(int argc, char **argv)
{
    Mix_Chunk *c;
    Mix_Music *m;
    int freq = 0, chans = 0, ch, t;
    Uint16 fmt = 0;

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        printf("mixtest: FAIL SDL_Init: %s\n", SDL_GetError());
        return 10;
    }
    check(write_tone(), "the tone written to " NAME);
    check(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0, "Mix_OpenAudio");
    if (failed) {
        printf("mixtest: %s\n", Mix_GetError());
        printf("mixtest: FAIL\n");
        return 10;
    }
    Mix_QuerySpec(&freq, &fmt, &chans);
    printf("mixtest: the mix at %d Hz, %d channels; %d to play on\n", freq, chans, Mix_AllocateChannels(-1));
    Mix_ChannelFinished(on_channel);
    Mix_HookMusicFinished(on_music);

    c = Mix_LoadWAV(NAME);
    check(c != NULL, "Mix_LoadWAV of the tone");
    if (c) {
        check(c->alen == FRAMES * 2, "it keeps its own rate and channels (alen)");
        ch = Mix_PlayChannel(-1, c, 0);
        check(ch >= 0 && Mix_Playing(ch), "Mix_PlayChannel: playing");
        wait_ms(450);
        check(!Mix_Playing(ch) && finished_ch == ch, "it ends by itself, and Mix_ChannelFinished is told");
        finished_n = 0;
        ch = Mix_PlayChannel(-1, c, 2);
        wait_ms(500);
        check(Mix_Playing(ch), "loops 2: still playing after two plays' time");
        wait_ms(500);
        check(!Mix_Playing(ch) && finished_n == 1, "and done after three");
        ch = Mix_PlayChannelTimed(-1, c, -1, 300);
        wait_ms(200);
        check(Mix_Playing(ch), "timed: on at 200 ms");
        wait_ms(250);
        check(!Mix_Playing(ch), "timed: off by 450 ms");
        ch = Mix_PlayChannel(-1, c, -1);
        Mix_FadeOutChannel(ch, 200);
        check(Mix_FadingChannel(ch) == MIX_FADING_OUT, "fading out");
        wait_ms(400);
        check(!Mix_Playing(ch), "faded out");
    }

    m = Mix_LoadMUS(NAME);
    check(m != NULL, "Mix_LoadMUS of the tone");
    if (m) {
        music_done = 0;
        check(Mix_PlayMusic(m, 2) == 0 && Mix_PlayingMusic(), "music: playing");
        wait_ms(900);
        check(!Mix_PlayingMusic() && music_done, "music: twice through, then Mix_HookMusicFinished");
        Mix_FreeMusic(m);
    }

    if (argc > 1) {
        int secs = argc > 2 ? atoi(argv[2]) : 5;
        m = Mix_LoadMUS(argv[1]);
        check(m != NULL, "Mix_LoadMUS of the file");
        if (m) {
            music_done = 0;
            check(Mix_PlayMusic(m, -1) == 0, "the file plays");
            printf("mixtest: decoded by: %s\n", Mix_MusicDecodedBy());
            for (t = 0; t < secs * 10 && Mix_PlayingMusic(); t++)
                wait_ms(100);
            check(Mix_PlayingMusic(), "and goes on for the seconds asked");
            Mix_FadeOutMusic(500);
            wait_ms(800);
            check(!Mix_PlayingMusic(), "faded out");
            Mix_FreeMusic(m);
        } else {
            printf("mixtest: %s\n", Mix_GetError());
        }
    }

    if (c)
        Mix_FreeChunk(c);
    Mix_CloseAudio();
    SDL_Quit();
    remove(NAME);
    printf("mixtest: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 10 : 0;
}
