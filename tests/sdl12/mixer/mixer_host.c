/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * library/modules/sdl12/mixer on a PC: mix_core.c against a pretend AHI
 * that keeps AHI's rules (acaudio.doc: a sound set now starts now and calls
 * no SoundFunc; one set for later starts when the one playing ends, and
 * that calls SoundFunc; with nothing new set, the sound plays again), and
 * mix_wav.c's WAV reading and Ogg/WAV choice.
 *   tests/sdl12/mixer/test_mixer_host.sh */
#include "mix_core.h"
#include "mix_wav.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, fails;
#define CHECK(c, what)                                         \
    do {                                                       \
        checks++;                                              \
        if (!(c)) {                                            \
            fails++;                                           \
            printf("FAIL %s (line %d)\n", what, __LINE__);     \
        }                                                      \
    } while (0)

/* ---- The pretend AHI ---------------------------------------------------------------------- */

#define NV 17

struct voice {
    const int16_t *cur, *next;            /* NULL: AHI_NOSOUND */
    uint32_t cur_frames, next_frames;
    uint32_t freq;
    int32_t gl, gr;
    int stereo;
    int starts;                           /* sounds started, now or at an end */
};
static struct voice V[NV];
static struct mc_core C;

static void be_sound(void *ctx, int v, const int16_t *data, uint32_t frames, int channels, int imm)
{
    (void)ctx;
    (void)channels;
    if (imm) {
        V[v].cur = data;
        V[v].cur_frames = frames;
        V[v].next = data;                 /* plays again unless told */
        V[v].next_frames = frames;
        if (data) {
            V[v].starts++;
        }
    } else {
        V[v].next = data;
        V[v].next_frames = frames;
    }
}

static void be_vol(void *ctx, int v, int32_t gl, int32_t gr, int stereo)
{
    (void)ctx;
    V[v].gl = gl;
    V[v].gr = gr;
    V[v].stereo = stereo;
}

static void be_freq(void *ctx, int v, uint32_t f)
{
    (void)ctx;
    V[v].freq = f;
}

/* Voice v's sound reaches its end. */
static void end_of(int v)
{
    if (!V[v].cur) {
        return;
    }
    V[v].cur = V[v].next;
    V[v].cur_frames = V[v].next_frames;
    if (V[v].cur) {
        V[v].starts++;
    }
    mc_sound_event(&C, v);
}

static void reset(void)
{
    struct mc_backend be = { NULL, be_sound, be_vol, be_freq };
    memset(V, 0, sizeof V);
    mc_init(&C, &be, NV - 1);
}

static int16_t pcm_a[1000], pcm_b[2000];
static mc_sound A = { pcm_a, 1000, 22050, 1, 128 };
static mc_sound B = { pcm_b, 1000, 44100, 2, 128 };

/* ---- Channels ----------------------------------------------------------------------------- */

static void test_once(void)
{
    int ch;
    reset();
    ch = mc_play(&C, -1, &A, 0, 0, 0);
    CHECK(ch == 0, "a play takes the first free channel");
    CHECK(V[0].cur == pcm_a && V[0].freq == 22050, "it starts now at its own rate");
    CHECK(V[0].next == NULL, "AHI_NOSOUND is queued behind a single play");
    CHECK(mc_playing(&C, 0) == 1 && mc_playing(&C, -1) == 1, "it is playing");
    end_of(0);
    CHECK(mc_playing(&C, 0) == 0, "its end frees the channel");
    CHECK(mc_take_finished(&C) == 1u, "and says channel 0 finished");
    CHECK(V[0].starts == 1, "it played once");
}

static void test_loops(void)
{
    int k;
    reset();
    mc_play(&C, 3, &A, 2, 0, 0);
    for (k = 0; k < 5; k++) {
        end_of(3);
    }
    CHECK(V[3].starts == 3, "loops 2 plays three times");
    CHECK(!mc_playing(&C, 3), "and then stops");
    reset();
    mc_play(&C, 1, &A, -1, 0, 0);
    for (k = 0; k < 20; k++) {
        end_of(1);
    }
    CHECK(mc_playing(&C, 1) && V[1].starts == 21, "loops -1 goes on");
    mc_halt(&C, 1);
    CHECK(!mc_playing(&C, 1) && V[1].cur == NULL, "a halt silences it");
    CHECK(mc_take_finished(&C) == 2u, "and it has finished");
}

static void test_channels(void)
{
    reset();
    mc_reserve(&C, 2);
    CHECK(mc_play(&C, -1, &A, 0, 0, 0) == 2, "channel -1 passes the reserved");
    CHECK(mc_play(&C, 0, &A, 0, 0, 0) == 0, "a reserved one by its number");
    mc_allocate(&C, 3);
    CHECK(mc_play(&C, -1, &A, 0, 0, 0) == -1, "only a reserved one free: -1");
    CHECK(mc_play(&C, 1, &A, 0, 0, 0) == 1, "which plays by its number");
    CHECK(mc_play(&C, 5, &A, 0, 0, 0) == -1, "past Mix_AllocateChannels: -1");
    mc_play(&C, 1, &B, 0, 0, 0);
    CHECK(mc_take_finished(&C) & 2u, "a play over another finishes it");
    CHECK(mc_sound_of(&C, 1) == &B, "Mix_GetChunk's sound");
    mc_forget(&C, &A);
    CHECK(!mc_playing(&C, 0) && !mc_playing(&C, 2) && mc_playing(&C, 1), "freeing a chunk stops only its plays");
}

static void test_volume(void)
{
    int32_t vol, pan;
    reset();
    mc_play(&C, 0, &A, 0, 0, 0);
    CHECK(V[0].gl == 16384 && V[0].gr == 16384, "full volume is unity both sides");
    mc_volume(&C, 0, 64);
    CHECK(V[0].gl == 8192, "Mix_Volume 64 is half");
    mc_panning(&C, 0, 255, 0);
    CHECK(V[0].gl == 8192 && V[0].gr == 0, "panned left");
    A.volume = 64;
    mc_sound_changed(&C, &A);
    CHECK(V[0].gl == 4096, "the chunk's volume too");
    A.volume = 128;
    CHECK(mc_volume(&C, 0, -1) == 64, "volume -1 asks");
    mc_volume(&C, -1, 100);
    CHECK(mc_volume(&C, 5, -1) == 100, "channel -1 sets them all");

    mc_volpan(16384, 16384, 0, 1, &vol, &pan);
    CHECK(vol == 0x20000 && pan == 0x8000, "Host mix: a mono sound in the middle at 2.0 (master n / 2)");
    mc_volpan(16384, 16384, 0, 0, &vol, &pan);
    CHECK(vol == 0x10000 && pan == 0x8000, "AHI's mixer: at 1.0 (master n)");
    mc_volpan(16384, 16384, 1, 1, &vol, &pan);
    CHECK(vol == 0x10000 && pan == 0x8000, "Host mix: stereo at 1.0, balanced");
    mc_volpan(16384, 0, 0, 1, &vol, &pan);
    CHECK(vol == 0x10000 && pan == 0, "all left: pan 0");
    mc_volpan(0, 8192, 0, 0, &vol, &pan);
    CHECK(vol == 0x4000 && pan == 0x10000, "half, all right");
    mc_volpan(0, 0, 0, 1, &vol, &pan);
    CHECK(vol == 0 && pan == 0x8000, "silent");
}

static void test_time(void)
{
    int k;
    int32_t last;
    reset();
    CHECK(mc_ticks(100) == 5 && mc_ticks(1) == 1 && mc_ticks(0) == 1, "ms to 50 Hz ticks");
    mc_play(&C, 0, &A, -1, 100, 0);
    for (k = 0; k < 4; k++) {
        mc_tick(&C);
    }
    CHECK(mc_playing(&C, 0), "expiry: still on after 80 ms");
    mc_tick(&C);
    CHECK(!mc_playing(&C, 0) && V[0].cur == NULL, "expiry: off at 100 ms");

    reset();
    mc_play(&C, 0, &A, -1, 0, 0);
    mc_fade_out(&C, 0, 100);
    CHECK(mc_fading(&C, 0) == MC_FADING_OUT, "fading out");
    last = V[0].gl;
    for (k = 0; k < 4; k++) {
        mc_tick(&C);
        CHECK(V[0].gl < last, "fading out: quieter each tick");
        last = V[0].gl;
    }
    mc_tick(&C);
    CHECK(!mc_playing(&C, 0), "faded out: halted");

    reset();
    mc_play(&C, 0, &A, -1, 0, 100);
    CHECK(V[0].gl == 0 && mc_fading(&C, 0) == MC_FADING_IN, "a fade in starts silent");
    for (k = 0; k < 5; k++) {
        mc_tick(&C);
    }
    CHECK(V[0].gl == 16384 && mc_fading(&C, 0) == MC_NO_FADING, "and ends at full");

    reset();
    mc_play(&C, 0, &A, -1, 0, 0);
    mc_pause(&C, 0);
    CHECK(V[0].freq == 0 && mc_paused(&C, 0), "paused: frequency 0");
    mc_resume(&C, 0);
    CHECK(V[0].freq == 22050 && !mc_paused(&C, 0), "resumed: its rate again");
}

/* ---- Music -------------------------------------------------------------------------------- */

static int16_t seg[6][512];

static void test_music(void)
{
    int mv = NV - 1;
    uint32_t freed;
    reset();
    mc_music_start(&C, 22050, 2, 0);
    CHECK(V[mv].freq == 22050 && V[mv].stereo, "the music voice at the music's rate");
    CHECK(mc_music_room(&C), "room for pieces");
    mc_music_put(&C, seg[0], 256, 0);
    CHECK(V[mv].cur == seg[0] && V[mv].next == NULL, "the first piece starts now");
    mc_music_put(&C, seg[1], 256, 1);
    CHECK(V[mv].next == seg[1], "the second queued behind it");
    mc_music_put(&C, seg[2], 256, 2);
    mc_music_put(&C, seg[3], 256, 3);
    CHECK(!mc_music_room(&C), "four in flight: no room");
    end_of(mv);
    freed = mc_take_freed(&C);
    CHECK(freed == 1u, "the first piece's buffer comes back when the second starts");
    CHECK(V[mv].cur == seg[1] && V[mv].next == seg[2], "and the third is queued");
    mc_music_put(&C, seg[0], 256, 0);
    end_of(mv);
    end_of(mv);
    CHECK(V[mv].cur == seg[3] && V[mv].next == seg[0], "pieces play in order, round the ring");
    end_of(mv);
    CHECK(V[mv].cur == seg[0] && V[mv].next == NULL, "the last one: AHI_NOSOUND behind it");
    mc_music_end(&C);
    CHECK(!C.music_finished, "the end comes when the last piece has played");
    end_of(mv);
    CHECK(C.music_finished && !C.mus.active, "the music has finished");
    CHECK(mc_take_freed(&C) == (1u << 1 | 1u << 2 | 1u << 3 | 1u << 0), "every buffer is back");
}

static void test_music_starved(void)
{
    int mv = NV - 1;
    reset();
    mc_music_start(&C, 44100, 1, 0);
    mc_music_put(&C, seg[0], 256, 0);
    end_of(mv);
    CHECK(V[mv].cur == NULL && !C.mus.playing && C.mus.active, "ran dry: silent, still on");
    mc_music_put(&C, seg[1], 256, 1);
    CHECK(V[mv].cur == seg[1], "the next piece starts at once");
    mc_music_put(&C, seg[2], 256, -1);
    CHECK(V[mv].next == seg[2], "a piece late for AHI_NOSOUND takes its place");
    mc_music_halt(&C);
    CHECK(!C.mus.active && V[mv].cur == NULL, "halted");
    CHECK(!C.music_finished, "a halt is not an end");
    CHECK(mc_take_freed(&C) == (1u << 0 | 1u << 1), "a piece with no buffer gives none back");
}

static void test_music_fade(void)
{
    int mv = NV - 1, k;
    reset();
    mc_music_volume(&C, 64);
    mc_music_start(&C, 22050, 1, 0);
    CHECK(V[mv].gl == 8192, "Mix_VolumeMusic 64");
    mc_music_put(&C, seg[0], 256, 0);
    mc_music_fade_out(&C, 60);
    mc_tick(&C);
    mc_tick(&C);
    CHECK(C.mus.active && V[mv].gl < 8192, "fading");
    mc_tick(&C);
    CHECK(!C.mus.active && C.music_finished, "faded out: over, and told");
    reset();
    mc_music_start(&C, 22050, 1, 40);
    CHECK(V[mv].gl == 0 && C.mus.fade == MC_FADING_IN, "music fade in from silence");
    for (k = 0; k < 2; k++) {
        mc_tick(&C);
    }
    CHECK(V[mv].gl == 16384, "to full");
    mc_music_pause(&C, 1);
    CHECK(V[mv].freq == 0, "music paused");
    mc_music_pause(&C, 0);
    CHECK(V[mv].freq == 22050, "music resumed");
}

/* ---- WAV ---------------------------------------------------------------------------------- */

static uint32_t put32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
    return 4;
}

static uint32_t put16(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    return 2;
}

static uint32_t make_wav(uint8_t *d, int fmt, int ch, uint32_t rate, int bits, int align, int extra,
                         const uint8_t *data, uint32_t len)
{
    uint32_t n = 0, fl = extra ? 20 : 16;
    memcpy(d, "RIFF", 4);
    n = 8;
    memcpy(d + n, "WAVE", 4);
    n += 4;
    memcpy(d + n, "LIST", 4);                 /* a chunk to pass over */
    n += 4;
    n += put32(d + n, 3);
    memcpy(d + n, "abc\0", 4);                /* odd: padded */
    n += 4;
    memcpy(d + n, "fmt ", 4);
    n += 4;
    n += put32(d + n, fl);
    n += put16(d + n, fmt);
    n += put16(d + n, ch);
    n += put32(d + n, rate);
    n += put32(d + n, rate * align);
    n += put16(d + n, align);
    n += put16(d + n, bits);
    if (extra) {
        n += put16(d + n, 2);
        n += put16(d + n, extra);
    }
    memcpy(d + n, "data", 4);
    n += 4;
    n += put32(d + n, len);
    memcpy(d + n, data, len);
    n += len;
    put32(d + 4, n - 8);
    return n;
}

static void test_wav(void)
{
    uint8_t d[512], raw[16];
    struct mw_info w;
    int16_t out[16];
    uint32_t n;
    /* 16-bit stereo: (1000, -1000), (32767, -32768) */
    put16(raw, 1000);
    put16(raw + 2, (uint16_t)-1000);
    put16(raw + 4, 32767);
    put16(raw + 6, 0x8000);
    n = make_wav(d, 1, 2, 44100, 16, 4, 0, raw, 8);
    CHECK(mw_riff_size(d) == n, "the RIFF size");
    CHECK(mw_parse(d, n, &w) == NULL, "a 16-bit stereo WAV reads");
    CHECK(w.channels == 2 && w.rate == 44100 && w.frames == 2, "its channels, rate and frames");
    mw_pcm16(&w, out);
    CHECK(out[0] == 1000 && out[1] == -1000 && out[2] == 32767 && out[3] == -32768, "its samples, in this machine's order");
    /* 8-bit mono */
    raw[0] = 128;
    raw[1] = 255;
    raw[2] = 0;
    n = make_wav(d, 1, 1, 11025, 8, 1, 0, raw, 3);
    CHECK(mw_parse(d, n, &w) == NULL && w.frames == 3, "an 8-bit mono WAV reads");
    mw_pcm16(&w, out);
    CHECK(out[0] == 0 && out[1] == 127 * 256 && out[2] == -32768, "8-bit unsigned to 16-bit signed");
    /* IMA ADPCM: a block of 8 bytes (4 header + 4 data) is 9 frames */
    put16(raw, 100);
    raw[2] = 0;
    raw[3] = 0;
    raw[4] = 0x70;                            /* frames 1, 2: codes 0, 7 (low nibble first) */
    raw[5] = 0x08;                            /* codes 8, 0 */
    raw[6] = 0;
    raw[7] = 0;
    n = make_wav(d, 0x11, 1, 22050, 4, 8, 9, raw, 8);
    CHECK(mw_parse(d, n, &w) == NULL, "an IMA ADPCM WAV reads");
    CHECK(w.block_frames == 9 && w.frames == 9, "its blocks' frames");
    mw_adpcm_block(&w, 0, out);
    /* step 7: code 0 adds 7 >> 3 = 0 (index stays 0); code 7 adds 0 + 7 + 3 + 1 = 11
     * (index 8, step 16); code 8 takes 16 >> 3 = 2 (index 7, step 14); code 0 adds 1 */
    CHECK(out[0] == 100 && out[1] == 100 && out[2] == 111 && out[3] == 109 && out[4] == 110,
          "ADPCM decodes as IMA's tables say");
    /* refusals */
    n = make_wav(d, 3, 1, 22050, 32, 4, 0, raw, 8);
    CHECK(mw_parse(d, n, &w) != NULL, "float WAV refused");
    n = make_wav(d, 1, 6, 22050, 16, 12, 0, raw, 12);
    CHECK(mw_parse(d, n, &w) != NULL, "six channels refused");
    CHECK(mw_parse((const uint8_t *)"OggS\0\0\0\0\0\0\0\0", 12, &w) != NULL, "an Ogg isn't a WAV");
    CHECK(mw_riff_size((const uint8_t *)"OggS\0\0\0\0\0\0\0\0") == 0, "nor RIFF");
    n = make_wav(d, 1, 1, 22050, 16, 2, 0, raw, 8);
    put32(d + n - 12, 4000);                  /* data says more than there is */
    CHECK(mw_parse(d, n, &w) == NULL && w.frames == 4, "a short data chunk reads what is there");
}

static void test_choice(void)
{
    char out[64];
    CHECK(mw_sibling("data/sounds/music.ogg", ".wav", out, sizeof out) && !strcmp(out, "data/sounds/music.wav"),
          "music.ogg's WAV is music.wav");
    CHECK(mw_sibling("PROGDIR:snd/boom", ".wav", out, sizeof out) && !strcmp(out, "PROGDIR:snd/boom.wav"),
          "no extension: one is added");
    CHECK(mw_sibling("a.b/c", ".ogg", out, sizeof out) && !strcmp(out, "a.b/c.ogg"), "a dot in a drawer's name isn't one");
    CHECK(!mw_sibling("long-name.ogg", ".wav", out, 8), "too long: 0");
    CHECK(mw_dos_name("PROGDIR:/data/sounds/throw.wav", out, sizeof out) &&
              !strcmp(out, "PROGDIR:data/sounds/throw.wav"),
          "PROGDIR:/data/x (OpenLieroX's join) is PROGDIR:data/x");
    CHECK(mw_dos_name("Work://a/b.ogg", out, sizeof out) && !strcmp(out, "Work:a/b.ogg"), "every slash after the colon goes");
    CHECK(!mw_dos_name("PROGDIR:data/x.wav", out, sizeof out), "a name with no slash after its colon: 0");
    CHECK(!mw_dos_name("data//x.wav", out, sizeof out), "no colon: 0, left as it is");
    CHECK(!mw_dos_name("PROGDIR:/long-name.wav", out, 10), "too long: 0");
    CHECK(mw_is_wav_name("x.WAV") && mw_is_wav_name("d/x.wav") && !mw_is_wav_name("x.ogg") && !mw_is_wav_name("wav"),
          "WAV names");
    CHECK(mw_on_68k("this CPU (gfopensound-decoders.library)"), "gfopensound.datatype's own CPU");
    CHECK(mw_on_68k("this Amiga's CPU, through Paula (no AHI)"), "the other datatypes' words for it");
    CHECK(!mw_on_68k("media.decode/1 on the services card (Mitchy's host)") && !mw_on_68k(NULL),
          "the card, or nothing said, isn't the 68k");
    /* asked for an Ogg */
    CHECK(mw_choose(0, 1, 1, 1, 1) == MW_USE_WAV, "Ogg decoded on the 68k, WAV beside it: the WAV");
    CHECK(mw_choose(0, 1, 1, 1, 0) == MW_USE_DT, "Ogg decoded off the 68k: the Ogg, WAV or not");
    CHECK(mw_choose(0, 1, 0, 1, 1) == MW_USE_DT, "Ogg on the 68k, no WAV: the Ogg");
    CHECK(mw_choose(0, 0, 1, 0, 0) == MW_USE_WAV, "no Ogg, a WAV: the WAV");
    CHECK(mw_choose(0, 1, 1, 0, 0) == MW_USE_WAV, "the Ogg won't open: the WAV");
    CHECK(mw_choose(0, 0, 0, 0, 0) == MW_NONE, "neither: nothing");
    /* asked for a WAV */
    CHECK(mw_choose(1, 1, 1, 0, 0) == MW_USE_WAV, "a WAV that is there");
    CHECK(mw_choose(1, 0, 0, 1, 1) == MW_USE_DT, "a WAV that isn't, an Ogg that is: the Ogg");
    CHECK(mw_choose(1, 0, 0, 0, 0) == MW_NONE, "a WAV that isn't, and no Ogg");
}

int main(void)
{
    test_once();
    test_loops();
    test_channels();
    test_volume();
    test_time();
    test_music();
    test_music_starved();
    test_music_fade();
    test_wav();
    test_choice();
    printf("mixer_host: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
