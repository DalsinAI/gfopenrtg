/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * mixhalt: Amoebax's music change (16 Nov 2026), on its own. One Ogg plays,
 * a second is loaded, Mix_HaltMusic, the second plays, and both are freed.
 * Each step is printed with SDL_GetTicks as it starts and ends, so a step
 * that never returns is the last line.
 *   mixhalt FIRST.ogg SECOND.ogg [SECONDS [game [SOUND.wav]]]
 * (SDL_MIXER_DECODER as usual). "game" plays as Amoebax does: music and
 * sounds at half volume, a window drawn without a pause between frames,
 * and SOUND.wav played now and then. */
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_Renderer *ren;
static Mix_Chunk *sound;

/* SECONDS of waiting: a pause, or frames drawn back to back. */
static void run(int secs)
{
    Uint32 end = SDL_GetTicks() + (Uint32)secs * 1000, frame = 0;
    SDL_Event ev;
    if (!ren) {
        SDL_Delay((Uint32)secs * 1000);
        return;
    }
    while ((Sint32)(end - SDL_GetTicks()) > 0) {
        while (SDL_PollEvent(&ev)) {
        }
        SDL_SetRenderDrawColor(ren, (Uint8)frame, 128, (Uint8)(255 - frame), 255);
        SDL_RenderClear(ren);
        SDL_RenderPresent(ren);
        if (sound && ++frame % 50 == 0) {
            Mix_PlayChannel(-1, sound, 0);
        }
    }
}

static void say(const char *what)
{
    printf("%7lu %s\n", (unsigned long)SDL_GetTicks(), what);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    Mix_Music *a, *b;
    int secs = argc > 3 ? atoi(argv[3]) : 3;

    if (argc < 3) {
        printf("usage: mixhalt FIRST.ogg SECOND.ogg [SECONDS]\n");
        return 5;
    }
    if (SDL_Init(SDL_INIT_AUDIO | (argc > 4 ? SDL_INIT_VIDEO : 0)) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 4096) < 0) {
        printf("Mix_OpenAudio: %s\n", Mix_GetError());
        return 20;
    }
    printf("audio driver %s, decoder hint %s\n", SDL_GetCurrentAudioDriver(),
           SDL_GetHint("SDL_MIXER_DECODER") ? SDL_GetHint("SDL_MIXER_DECODER") : "(auto)");
    if (argc > 4) {
        SDL_Window *w = SDL_CreateWindow("mixhalt", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 400, 300, 0);
        ren = w ? SDL_CreateRenderer(w, -1, 0) : NULL;
        Mix_Volume(-1, MIX_MAX_VOLUME / 2);
        Mix_VolumeMusic(MIX_MAX_VOLUME / 2);
        if (argc > 5) {
            sound = Mix_LoadWAV(argv[5]);
        }
        printf("game: window %s, sound %s\n", ren ? "yes" : "no", sound ? "yes" : "no");
    }
    say("LoadMUS first");
    a = Mix_LoadMUS(argv[1]);
    if (!a) {
        printf("Mix_LoadMUS: %s\n", Mix_GetError());
        return 20;
    }
    say("PlayMusic first");
    Mix_PlayMusic(a, -1);
    say("playing");
    run(secs);
    say("LoadMUS second");
    b = Mix_LoadMUS(argv[2]);
    if (!b) {
        printf("Mix_LoadMUS: %s\n", Mix_GetError());
        return 20;
    }
    say("HaltMusic");
    Mix_HaltMusic();
    say("halted");
    Mix_PlayMusic(b, -1);
    say("playing second");
    run(secs);
    say("PlayMusic first again (halts the second)");
    Mix_PlayMusic(a, 1);
    say("playing first");
    run(secs);
    say("FreeMusic");
    Mix_FreeMusic(a);
    Mix_FreeMusic(b);
    Mix_CloseAudio();
    SDL_Quit();
    say("done");
    return 0;
}
