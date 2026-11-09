/*
  SDL 2 for AmigaOS 3.x: the standard keys every game has.
  Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
*/

#ifndef SDL_os3keys_h_
#define SDL_os3keys_h_

#include "../SDL_sysvideo.h"

/* A raw key from a window's IDCMP: non-zero when it was one of the standard
   keys, which the game then doesn't see. */
extern int OS3_StandardKey(SDL_Window *window, UWORD code, UWORD qualifier);

/* Runs what the standard keys asked for, after the window's messages have
   been read (a full-screen switch closes and opens Intuition windows). */
extern void OS3_RunStandardKeys(_THIS);

/* SDL_CreateWindow: a top-level window opens full screen or in a window as
   the player last chose (Right Amiga + F), or as the program's icon says
   (FULLSCREEN or WINDOW tooltype); otherwise as the game asks. */
extern Uint32 OS3_StartModeFlags(Uint32 flags);

/* SDL_audio.c and SDL_systimer.c (patches/amigaos3/0020) */
extern void SDL_OS3_PauseAllAudio(int pause);
extern void SDL_OS3_TimerPause(int pause);

#endif /* SDL_os3keys_h_ */
