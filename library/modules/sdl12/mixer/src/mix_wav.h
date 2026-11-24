/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The mixer's own readers, with no Amiga in them: RIFF WAVE files (8 and
 * 16-bit PCM, and mono IMA ADPCM, the music amiga/tools/ogg2wav.py and the
 * installer's WAV fallback make), and which file of a pair to play (an Ogg
 * and the WAV made from it beside it). tests/sdl12/mixer tests them. */
#ifndef MIX_WAV_H
#define MIX_WAV_H

#include <stddef.h>
#include <stdint.h>

#define MW_PCM 1
#define MW_ADPCM 0x11

struct mw_info {
    int format;                   /* MW_PCM or MW_ADPCM */
    int channels;                 /* 1 or 2 (ADPCM: 1) */
    uint32_t rate;
    int bits;                     /* PCM: 8 or 16 */
    uint32_t block_align;         /* ADPCM: bytes a block */
    uint32_t block_frames;        /* ADPCM: frames a block */
    const uint8_t *data;
    uint32_t data_len;
    uint32_t frames;              /* in the file */
};

/* Reads the header of a WAV in memory. 0 and info, or an error's words. */
const char *mw_parse(const uint8_t *file, uint32_t size, struct mw_info *info);

/* The size a RIFF file says it is, from its first 12 bytes (0: not RIFF WAVE). */
uint32_t mw_riff_size(const uint8_t head[12]);

/* PCM frames to 16-bit signed in the machine's order, as many channels as
 * the file (out holds info->frames * channels). */
void mw_pcm16(const struct mw_info *info, int16_t *out);

/* ADPCM block b (of info->data_len / block_align) to block_frames frames. */
void mw_adpcm_block(const struct mw_info *info, uint32_t b, int16_t *out);

/* Which file to play.
 *   asked: the name the game gave;
 *   exists(name): whether a file is there.
 * mw_sibling writes asked with its extension changed to ext (".wav",
 * ".ogg") into out (size n); 0 when it doesn't fit.
 * mw_on_68k: whether OIA_DecodedBy's words say this Amiga's own CPU decodes
 * (gfopensound.datatype: "this CPU (...)"; others "this Amiga's CPU, ..."). */
int mw_sibling(const char *asked, const char *ext, char *out, size_t n);
int mw_is_wav_name(const char *name);
int mw_on_68k(const char *decoded_by);

/* A name as a port's C library reads it, for AmigaDOS: "PROGDIR:/data/x"
 * (a Unix-minded join of PROGDIR: and /data/x), which AmigaDOS would read
 * as PROGDIR:'s parent, becomes "PROGDIR:data/x". Only the slashes right
 * after the colon go. 1 and out when the name changed and fits in n, else 0.
 * The mixer tries the name as given first, and this only when that isn't
 * there. */
int mw_dos_name(const char *asked, char *out, size_t n);

/* The choice, once what is there is known:
 *   asked_there: the file asked for exists;
 *   wav_there:   name.wav beside it exists (asked itself, for a .wav);
 *   dt_ok:       the datatype opened the compressed one (asked, or name.ogg
 *                for a .wav that isn't there);
 *   dt_68k:      and decodes it on this CPU.
 * MW_USE_WAV plays the WAV, MW_USE_DT the datatype's, MW_NONE neither. */
enum { MW_NONE, MW_USE_WAV, MW_USE_DT };
int mw_choose(int asked_is_wav, int asked_there, int wav_there, int dt_ok, int dt_68k);

#endif
