/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * mixhalt: Amoebax's music change (16 Nov 2026), on its own. One Ogg plays,
 * a second is loaded, Mix_HaltMusic, the second plays, and both are freed.
 * Each step is printed with SDL_GetTicks as it starts and ends, so a step
 * that never returns is the last line.
 *   mixhalt FIRST.ogg SECOND.ogg [SECONDS]   (SDL_MIXER_DECODER as usual) */
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>

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
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 20;
    }
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 4096) < 0) {
        printf("Mix_OpenAudio: %s\n", Mix_GetError());
        return 20;
    }
    printf("audio driver %s, decoder hint %s\n", SDL_GetCurrentAudioDriver(),
           SDL_GetHint("SDL_MIXER_DECODER") ? SDL_GetHint("SDL_MIXER_DECODER") : "(auto)");
    say("LoadMUS first");
    a = Mix_LoadMUS(argv[1]);
    if (!a) {
        printf("Mix_LoadMUS: %s\n", Mix_GetError());
        return 20;
    }
    say("PlayMusic first");
    Mix_PlayMusic(a, -1);
    say("playing");
    SDL_Delay(secs * 1000);
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
    SDL_Delay(secs * 1000);
    say("PlayMusic first again (halts the second)");
    Mix_PlayMusic(a, 1);
    say("playing first");
    SDL_Delay(secs * 1000);
    say("FreeMusic");
    Mix_FreeMusic(a);
    Mix_FreeMusic(b);
    Mix_CloseAudio();
    SDL_Quit();
    say("done");
    return 0;
}
