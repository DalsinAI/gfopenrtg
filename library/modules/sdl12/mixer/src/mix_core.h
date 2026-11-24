/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The mixer's channels and its music voice, with no Amiga in them: what
 * each channel plays, how often, how loud and for how long, and what each
 * AHI voice is told and when. mix_amiga.c gives it AHI's calls (struct
 * mc_backend) and calls it from the program (under Disable()), from AHI's
 * SoundFunc (a voice's sound ended and the one queued started) and from the
 * mixer's process 50 times a second (mc_tick). tests/sdl12/mixer runs it on
 * a PC against a pretend AHI.
 *
 * AHI's rules, as acaudio.doc and AHI's own mixer keep them: a sound set
 * with AHISF_IMM starts now and calls no SoundFunc; one set without waits
 * for the sound playing to end, and that moment calls SoundFunc; a sound
 * with nothing queued after it plays again. So each voice always has its
 * next sound queued: the same part again while loops are left, else
 * AHI_NOSOUND, whose start says the channel has finished. */
#ifndef MIX_CORE_H
#define MIX_CORE_H

#include <stdint.h>

#define MC_MAX_CHANS 16           /* Mix_AllocateChannels' most */
#define MC_TICK_HZ 50             /* mc_tick's rate: AHI's PlayerFunc */
#define MC_SEGS 4                 /* music pieces in flight */

/* A sound in memory: 16-bit signed, the machine's byte order, mono or
 * interleaved stereo, frames at rate. volume is Mix_Chunk's (0..128). */
typedef struct mc_sound {
    const int16_t *data;
    uint32_t frames;
    uint32_t rate;
    uint8_t channels;
    uint8_t volume;
} mc_sound;

/* AHI, for one voice: what it plays (NULL: AHI_NOSOUND), how loud (each
 * side's gain, Q14: 16384 is unity; stereo says the sound is stereo) and
 * how fast (0 holds it where it is). imm: now (AHISF_IMM), else when the
 * sound playing ends. */
struct mc_backend {
    void *ctx;
    void (*sound)(void *ctx, int voice, const int16_t *data, uint32_t frames, int channels, int imm);
    void (*vol)(void *ctx, int voice, int32_t gl, int32_t gr, int stereo);
    void (*freq)(void *ctx, int voice, uint32_t freq);
};

enum { MC_NO_FADING, MC_FADING_OUT, MC_FADING_IN };

struct mc_chan {
    const mc_sound *snd;          /* NULL: free */
    int loops;                    /* plays left after the one queued; -1: for ever */
    int ending;                   /* AHI_NOSOUND is queued */
    int paused;
    int volume;                   /* 0..128 */
    int left, right;              /* Mix_SetPanning's, 0..255 */
    int fade;                     /* MC_FADING_ */
    uint32_t fade_total, fade_left;   /* ticks */
    uint32_t expire;              /* ticks left, 0: none */
};

/* A piece of music on the music voice. */
struct mc_seg {
    const int16_t *data;
    uint32_t frames;
    int buf;                      /* the producer's buffer it is in, or -1 (it points into a whole sound) */
};

struct mc_music {
    int active;                   /* a music is on the voice (playing, paused or starved) */
    int paused;
    uint32_t rate;
    int channels;
    int volume;                   /* Mix_VolumeMusic's, 0..128 */
    int fade;
    uint32_t fade_total, fade_left;
    /* the pieces: a ring the producer fills (head) and the voice plays (tail) */
    struct mc_seg seg[MC_SEGS];
    unsigned head, tail;          /* seg[tail % MC_SEGS] plays, then the ones up to head */
    int playing;                  /* the voice plays seg[tail] */
    int queued;                   /* seg[tail + 1] is queued behind it */
    int nosound;                  /* AHI_NOSOUND is queued behind it (no piece was ready) */
    int eos;                      /* the producer has no more pieces */
    uint32_t freed;               /* buffers given back (a mask of 1 << buf) */
};

struct mc_core {
    struct mc_backend be;
    int nvoices;                  /* channels on voices 0..nvoices-1; the music on voice nvoices */
    int nchans;                   /* Mix_AllocateChannels */
    int reserved;                 /* Mix_ReserveChannels */
    struct mc_chan chan[MC_MAX_CHANS];
    struct mc_music mus;
    uint32_t finished;            /* channels that finished since mc_take_finished (1 << c) */
    int music_finished;           /* the music ended by itself, or faded out */
};

void mc_init(struct mc_core *c, const struct mc_backend *be, int nvoices);

/* ms to ticks: at least one. */
uint32_t mc_ticks(int ms);

/* Channels: the SDL_mixer calls. ch -1 is every channel (or, for play,
 * the first free one past the reserved). */
int mc_play(struct mc_core *c, int ch, const mc_sound *s, int loops, int expire_ms, int fade_in_ms);
int mc_halt(struct mc_core *c, int ch);
void mc_pause(struct mc_core *c, int ch);
void mc_resume(struct mc_core *c, int ch);
int mc_paused(const struct mc_core *c, int ch);
int mc_playing(const struct mc_core *c, int ch);
int mc_fade_out(struct mc_core *c, int ch, int ms);
int mc_fading(const struct mc_core *c, int ch);
int mc_expire(struct mc_core *c, int ch, int ms);
int mc_volume(struct mc_core *c, int ch, int volume);
int mc_panning(struct mc_core *c, int ch, int left, int right);
int mc_allocate(struct mc_core *c, int n);
int mc_reserve(struct mc_core *c, int n);
const mc_sound *mc_sound_of(const struct mc_core *c, int ch);
void mc_sound_changed(struct mc_core *c, const mc_sound *s);   /* its volume: the channels playing it */
void mc_forget(struct mc_core *c, const mc_sound *s);           /* halts the channels playing it */

/* The music voice. The producer (the mixer's process) starts a music with
 * mc_music_start, adds pieces with mc_music_put while mc_music_room says
 * there is room, and says the end with mc_music_end. */
void mc_music_start(struct mc_core *c, uint32_t rate, int channels, int fade_in_ms);
int mc_music_room(const struct mc_core *c);
void mc_music_put(struct mc_core *c, const int16_t *data, uint32_t frames, int buf);
void mc_music_end(struct mc_core *c);
void mc_music_halt(struct mc_core *c);
void mc_music_pause(struct mc_core *c, int paused);
int mc_music_volume(struct mc_core *c, int volume);
int mc_music_fade_out(struct mc_core *c, int ms);
uint32_t mc_take_freed(struct mc_core *c);   /* buffers the voice is done with */

/* AHI's SoundFunc for voice v. Interrupt time: no allocation, no FPU. */
void mc_sound_event(struct mc_core *c, int v);

/* 50 a second: fades and expiries. */
void mc_tick(struct mc_core *c);

uint32_t mc_take_finished(struct mc_core *c);

/* Each side's gain to AHI_SetVol's volume and pan (16.16), as the mode
 * wants them: on ACAHI's Host mix (hostmix) the master volume is n / 2 and
 * a voice's volume can be up to 2.0; on AHI's own mixers the master is n
 * and the volume at most 1.0. Either way a sound at full volume, panned to
 * the middle, plays at its own level on both sides. */
void mc_volpan(int32_t gl, int32_t gr, int stereo, int hostmix, int32_t *vol, int32_t *pan);

#endif
