/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL_mixer 1.2's calls for SDL 1.2 programs on GFOS, the Team's own
 * (library/modules/sdl12/mixer): every sound on an AHI voice of its own, so
 * AHI mixes (on ACAHI's Host mix, the PC does), music streamed onto one
 * more voice from the system's sound datatypes (gfopensound.datatype:
 * Ogg, MP3, FLAC...; gfopenmodule.datatype: tracker modules), decoded by
 * the services card, AkikoPlus or a paired Cradle where there is one. A
 * WAV beside an Ogg (the installer's fallback) is played instead when the
 * decoding would be on this CPU. README.md there says it all.
 *
 * Link with -lSDL_mixer before -lSDL. */
#ifndef SDL_MIXER_H_
#define SDL_MIXER_H_

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_MIXER_MAJOR_VERSION 1
#define SDL_MIXER_MINOR_VERSION 2
#define SDL_MIXER_PATCHLEVEL 12
#define SDL_MIXER_VERSION(X) { (X)->major = SDL_MIXER_MAJOR_VERSION; (X)->minor = SDL_MIXER_MINOR_VERSION; (X)->patch = SDL_MIXER_PATCHLEVEL; }
#define MIX_MAJOR_VERSION SDL_MIXER_MAJOR_VERSION
#define MIX_MINOR_VERSION SDL_MIXER_MINOR_VERSION
#define MIX_PATCHLEVEL SDL_MIXER_PATCHLEVEL

#define MIX_CHANNELS 8
#define MIX_DEFAULT_FREQUENCY 22050
#define MIX_DEFAULT_FORMAT AUDIO_S16SYS
#define MIX_DEFAULT_CHANNELS 2
#define MIX_MAX_VOLUME 128
#define MIX_CHANNEL_POST (-2)
#define MIX_EFFECTSMAXSPEED "MIX_EFFECTSMAXSPEED"

/* abuf: the sound as AHI plays it, 16-bit signed in the 68k's order, at
 * its own rate and with its own channels (Mix_QuerySpec's are the mix's). */
typedef struct Mix_Chunk {
    int allocated;
    Uint8 *abuf;
    Uint32 alen;
    Uint8 volume;
} Mix_Chunk;

typedef enum { MIX_NO_FADING, MIX_FADING_OUT, MIX_FADING_IN } Mix_Fading;
typedef enum { MUS_NONE, MUS_CMD, MUS_WAV, MUS_MOD, MUS_MID, MUS_OGG, MUS_MP3, MUS_MP3_MAD, MUS_FLAC } Mix_MusicType;
typedef struct _Mix_Music Mix_Music;

typedef void (*Mix_EffectFunc_t)(int chan, void *stream, int len, void *udata);
typedef void (*Mix_EffectDone_t)(int chan, void *udata);

const SDL_version *Mix_Linked_Version(void);

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize);
void Mix_CloseAudio(void);
int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels);
int Mix_AllocateChannels(int numchans);
int Mix_ReserveChannels(int num);

/* Effects: WAV (8/16-bit PCM, IMA ADPCM), and what the sound datatypes
 * read (Ogg, MP3, FLAC, AIFF...). A file read whole through stdio, not
 * SDL_RWops; Mix_LoadWAV_RW reads a RIFF file by the length its header
 * gives, so a stream that can't seek or see its end works. */
Mix_Chunk *Mix_LoadWAV(const char *file);
Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc);
Mix_Chunk *Mix_QuickLoad_RAW(Uint8 *mem, Uint32 len);
void Mix_FreeChunk(Mix_Chunk *chunk);
int Mix_VolumeChunk(Mix_Chunk *chunk, int volume);

#define Mix_PlayChannel(channel, chunk, loops) Mix_PlayChannelTimed(channel, chunk, loops, -1)
#define Mix_FadeInChannel(channel, chunk, loops, ms) Mix_FadeInChannelTimed(channel, chunk, loops, ms, -1)
int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks);
int Mix_FadeInChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ms, int ticks);
int Mix_Volume(int channel, int volume);
int Mix_SetPanning(int channel, Uint8 left, Uint8 right);
int Mix_HaltChannel(int channel);
int Mix_ExpireChannel(int channel, int ticks);
int Mix_FadeOutChannel(int channel, int ms);
void Mix_Pause(int channel);
void Mix_Resume(int channel);
int Mix_Paused(int channel);
int Mix_Playing(int channel);
Mix_Fading Mix_FadingChannel(int which);
Mix_Chunk *Mix_GetChunk(int channel);
void Mix_ChannelFinished(void (*channel_finished)(int channel));
/* Effects on the mix: not here (AHI mixes). 0, with an error. */
int Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *arg);
int Mix_UnregisterAllEffects(int channel);

/* Music: one at a time, streamed. */
Mix_Music *Mix_LoadMUS(const char *file);
void Mix_FreeMusic(Mix_Music *music);
Mix_MusicType Mix_GetMusicType(const Mix_Music *music);
int Mix_PlayMusic(Mix_Music *music, int loops);
int Mix_FadeInMusic(Mix_Music *music, int loops, int ms);
int Mix_FadeInMusicPos(Mix_Music *music, int loops, int ms, double position);
int Mix_VolumeMusic(int volume);
int Mix_HaltMusic(void);
int Mix_FadeOutMusic(int ms);
Mix_Fading Mix_FadingMusic(void);
void Mix_PauseMusic(void);
void Mix_ResumeMusic(void);
void Mix_RewindMusic(void);
int Mix_PausedMusic(void);
int Mix_PlayingMusic(void);
/* Seconds from the start (a tracker module: not here, -1). */
int Mix_SetMusicPosition(double position);
void Mix_HookMusicFinished(void (*music_finished)(void));

/* For a status line: what decodes the music playing ("media.decode/1 on
 * the services card (Mitchy's host)", "this CPU (...)", "WAV"). */
const char *Mix_MusicDecodedBy(void);

#define Mix_SetError SDL_SetError
#define Mix_GetError SDL_GetError

#ifdef __cplusplus
}
#endif

#endif
