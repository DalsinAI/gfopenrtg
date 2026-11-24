/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL_mixer 1.2's calls on AHI (include/SDL_mixer.h), for SDL 1.2
 * programs: libSDL_mixer.a in the GFOpenGPU developer kit.
 *
 *   - Mix_OpenAudio opens ahi.device's AHI_NO_UNIT for AHI's library calls
 *     and takes voices with AHI_AllocAudio: on ACAHI's Host mix mode
 *     (0x00AC0001, acaudio.audio) when it is there, so the board mixes on
 *     the PC, else on AHI's default mode, where AHI mixes. 17 voices (16
 *     channels and the music), or 9, or 5 when fewer are free. Two "all of
 *     memory" sounds (address 0, length 0xFFFFFFFF: AHI_SetSound's offset
 *     is where the frames are), 16-bit mono and stereo, as OpenAL.module's
 *     Voices: a sound plays straight from where it was loaded, at its own
 *     rate; AHI resamples.
 *   - AHI's SoundFunc (a voice's sound ended and the one queued started)
 *     is mix_core.c's mc_sound_event, at interrupt time; its PlayerFunc, 50
 *     a second, wakes the mixer's process ("SDL_mixer", priority 5).
 *   - The process does the fades and expiries (mc_tick), tells
 *     Mix_ChannelFinished and Mix_HookMusicFinished, and streams the
 *     music: it opens it, reads it ahead a piece at a time (half a second,
 *     MC_SEGS of them in flight) and hands the pieces to the music voice.
 *     A music from a datatype is opened and read only in the process, as a
 *     datatype's service port belongs to the task that opened it.
 *   - Files: WAV (8/16-bit PCM, IMA ADPCM) read here; anything else
 *     through the sound datatypes with OIA_Reader (datatypes/gfopenimage.h):
 *     gfopensound.datatype (Ogg, MP3, FLAC... by media.decode/1 on the
 *     services card, AkikoPlus or a paired Cradle, else this CPU) and
 *     gfopenmodule.datatype (tracker modules). When OIA_DecodedBy says this
 *     CPU and name.wav is beside name.ogg (the installer's fallback), the
 *     WAV plays instead; a game asking for name.wav gets name.ogg when only
 *     that is there (mix_wav.c's mw_choose).
 *
 * The program's calls change channels under Disable() (the SoundFunc runs
 * at interrupt time); music calls go to the process and wait for it. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AHIBase mix_AHIBase
#define DataTypesBase mix_DataTypesBase

#include <exec/exec.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <utility/hooks.h>
#include <devices/ahi.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/soundclass.h>
#include <intuition/classusr.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/ahi.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>

#include "SDL_mixer.h"
#include "mix_core.h"
#include "mix_wav.h"

struct Library *mix_AHIBase;
struct Library *mix_DataTypesBase;

/* datatypes/gfopenimage.h's, for a build without the GFOpen SDK's headers */
#ifndef OIA_Dummy
#define OIA_Dummy (TAG_USER + 0x0DA15000)
#endif
#ifndef OIA_DecodedBy
#define OIA_DecodedBy (OIA_Dummy + 1)
#endif
#ifndef OIA_Reader
#define OIA_Reader (OIA_Dummy + 7)
#endif
#ifndef OIA_Channels
#define OIA_Channels (OIA_Dummy + 10)
#endif
#ifndef OIM_READSOUND
#define OIM_READSOUND (TAG_USER + 0x0DA15903)
struct oiReadSound {
    ULONG MethodID;
    struct GadgetInfo *oirs_GInfo;
    WORD *oirs_Buffer;
    ULONG oirs_Frames;
};
#endif

#define MIX_HOSTMIX 0x00AC0001ul        /* ACAHI: Host mix 16 bit stereo++ */
#define MIX_HOSTRATE 44100
#define MIX_PRIORITY 5
#define MIX_STACK 32768
#define SEG_BYTES (96 * 1024)           /* a music piece's buffer */
#define PATH_MAX_ 512

/* ---- State ------------------------------------------------------------------------------ */

struct mix_chunk {
    Mix_Chunk pub;
    mc_sound snd;
};

enum { MK_PCM, MK_ADPCM, MK_DT };

struct _Mix_Music {
    int kind;
    Mix_MusicType type;
    char path[PATH_MAX_];               /* MK_DT: opened again in the process */
    uint8_t *file;                      /* MK_ADPCM: the WAV */
    struct mw_info info;
    int16_t *pcm;                       /* MK_PCM: the sound */
    uint32_t frames, rate;
    int channels;
    const char *by;                     /* OIA_DecodedBy, or "WAV" */
    char by_copy[96];
};

static struct mc_core core;
static int opened, hostmix, nvoices;
static ULONG mixfreq;
static struct MsgPort ahiport;
static struct AHIRequest *ahireq;
static struct AHIAudioCtrl *ctrl;
static struct Hook sound_hook, player_hook;
static void (*channel_finished)(int);
static void (*music_finished)(void);

/* the process */
static struct Task *volatile proc;
static volatile BYTE proc_sig = -1;     /* ticks, sound events, commands */
static volatile int proc_quit;
static struct Task *starter;
static BYTE starter_sig;

/* music: the program's view, and the command to the process */
static struct SignalSemaphore cmd_lock;
enum { CMD_NONE, CMD_PLAY, CMD_HALT, CMD_POS };
static struct {
    int type, loops, fade_ms, result;
    Mix_Music *music;
    double pos;
    struct Task *waiter;
    BYTE sig;
    volatile int pending;
} cmd;
static volatile int mus_on;             /* Mix_PlayingMusic */
static volatile int mus_paused;
static Mix_Music *volatile mus_cur;     /* the music on the voice */

/* the process's own */
static int16_t *segbuf[MC_SEGS];
static uint32_t segfree;                /* buffers free, 1 << k */
static int mus_loops;                   /* plays left after this one; -1 for ever */
static uint32_t mus_pos;                /* MK_PCM, MK_ADPCM: the next frame (ADPCM: block * frames) */
static Object *mus_dto;                 /* MK_DT: the reader */

/* ---- AHI ---------------------------------------------------------------------------------- */

static void be_sound(void *ctx, int v, const int16_t *data, uint32_t frames, int channels, int imm)
{
    (void)ctx;
    if (!data) {
        AHI_SetSound((UWORD)v, AHI_NOSOUND, 0, 0, ctrl, imm ? AHISF_IMM : AHISF_NONE);
        return;
    }
    AHI_SetSound((UWORD)v, channels == 2 ? 1 : 0, (ULONG)data / (ULONG)(channels * 2), (LONG)frames, ctrl,
                 imm ? AHISF_IMM : AHISF_NONE);
}

static void be_vol(void *ctx, int v, int32_t gl, int32_t gr, int stereo)
{
    int32_t vol, pan;
    (void)ctx;
    mc_volpan(gl, gr, stereo, hostmix, &vol, &pan);
    AHI_SetVol((UWORD)v, vol, pan, ctrl, AHISF_IMM);
}

static void be_freq(void *ctx, int v, uint32_t f)
{
    (void)ctx;
    AHI_SetFreq((UWORD)v, f, ctrl, AHISF_IMM);
}

static void wake(void)
{
    struct Task *t = proc;
    if (t) {
        Signal(t, 1UL << proc_sig);
    }
}

/* SoundFunc and PlayerFunc, through amiga.lib's HookEntry: interrupt time. */
static ULONG sound_func(struct Hook *h, struct AHIAudioCtrl *a, struct AHISoundMessage *m)
{
    (void)h;
    (void)a;
    mc_sound_event(&core, m->ahism_Channel);
    wake();
    return 0;
}

static ULONG player_func(struct Hook *h, struct AHIAudioCtrl *a, APTR m)
{
    (void)h;
    (void)a;
    (void)m;
    wake();
    return 0;
}

static void set_hook(struct Hook *h, APTR fn)
{
    memset(h, 0, sizeof *h);
    h->h_Entry = (HOOKFUNC)HookEntry;
    h->h_SubEntry = (HOOKFUNC)fn;
}

static int mode_is_hostmix(ULONG id, struct AHIAudioCtrl *a)
{
    char name[32];
    ULONG got = AHI_INVALID_ID;
    struct TagItem tags[4];
    int i;
    name[0] = 0;
    tags[0].ti_Tag = AHIDB_Driver;    tags[0].ti_Data = (ULONG)name;
    tags[1].ti_Tag = AHIDB_BufferLen; tags[1].ti_Data = sizeof name;
    tags[2].ti_Tag = AHIDB_AudioID;   tags[2].ti_Data = (ULONG)&got;
    tags[3].ti_Tag = TAG_DONE;        tags[3].ti_Data = 0;
    if (!AHI_GetAudioAttrsA(id, a, tags)) {
        return 0;
    }
    name[sizeof name - 1] = 0;
    for (i = 0; name[i]; i++) {
        if (name[i] >= 'A' && name[i] <= 'Z') {
            name[i] += 'a' - 'A';
        }
    }
    if (a) {
        id = got;
    }
    return id == MIX_HOSTMIX && !strcmp(name, "acaudio");
}

static struct AHIAudioCtrl *alloc_audio(ULONG id, ULONG freq, int n)
{
    struct TagItem tags[11];
    int nt = 0;
    tags[nt].ti_Tag = AHIA_AudioID;       tags[nt++].ti_Data = id;
    tags[nt].ti_Tag = AHIA_MixFreq;       tags[nt++].ti_Data = freq;
    tags[nt].ti_Tag = AHIA_Channels;      tags[nt++].ti_Data = (ULONG)n;
    tags[nt].ti_Tag = AHIA_Sounds;        tags[nt++].ti_Data = 2;
    tags[nt].ti_Tag = AHIA_SoundFunc;     tags[nt++].ti_Data = (ULONG)&sound_hook;
    tags[nt].ti_Tag = AHIA_PlayerFunc;    tags[nt++].ti_Data = (ULONG)&player_hook;
    tags[nt].ti_Tag = AHIA_PlayerFreq;    tags[nt++].ti_Data = (ULONG)MC_TICK_HZ << 16;
    tags[nt].ti_Tag = AHIA_MinPlayerFreq; tags[nt++].ti_Data = (ULONG)MC_TICK_HZ << 16;
    tags[nt].ti_Tag = AHIA_MaxPlayerFreq; tags[nt++].ti_Data = (ULONG)MC_TICK_HZ << 16;
    tags[nt].ti_Tag = TAG_DONE;           tags[nt].ti_Data = 0;
    return AHI_AllocAudioA(tags);
}

static void ahi_close(void)
{
    if (ctrl) {
        struct TagItem tags[2];
        tags[0].ti_Tag = AHIC_Play; tags[0].ti_Data = FALSE;
        tags[1].ti_Tag = TAG_DONE;  tags[1].ti_Data = 0;
        AHI_ControlAudioA(ctrl, tags);
        AHI_UnloadSound(0, ctrl);
        AHI_UnloadSound(1, ctrl);
        AHI_FreeAudio(ctrl);
        ctrl = NULL;
    }
    if (ahireq) {
        CloseDevice((struct IORequest *)ahireq);
        DeleteIORequest((struct IORequest *)ahireq);
        ahireq = NULL;
        mix_AHIBase = NULL;
    }
}

static int ahi_open(void)
{
    static const int tries[] = { MC_MAX_CHANS + 1, 9, 5 };
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin = me->pr_WindowPtr;
    struct AHIEffMasterVolume mv;
    struct TagItem tags[3];
    unsigned t;
    int k;
    LONG err;

    memset(&ahiport, 0, sizeof ahiport);
    ahiport.mp_Node.ln_Type = NT_MSGPORT;
    ahiport.mp_Flags = PA_IGNORE;
    NewList(&ahiport.mp_MsgList);
    ahireq = (struct AHIRequest *)CreateIORequest(&ahiport, sizeof(struct AHIRequest));
    if (!ahireq) {
        return 0;
    }
    ahireq->ahir_Version = 4;
    me->pr_WindowPtr = (APTR)-1L;
    err = OpenDevice((CONST_STRPTR)AHINAME, AHI_NO_UNIT, (struct IORequest *)ahireq, 0);
    me->pr_WindowPtr = oldwin;
    if (err) {
        DeleteIORequest((struct IORequest *)ahireq);
        ahireq = NULL;
        return 0;
    }
    mix_AHIBase = (struct Library *)ahireq->ahir_Std.io_Device;
    set_hook(&sound_hook, (APTR)sound_func);
    set_hook(&player_hook, (APTR)player_func);
    for (t = 0; !ctrl && t < sizeof tries / sizeof tries[0]; t++) {
        if (mode_is_hostmix(MIX_HOSTMIX, NULL)) {
            ctrl = alloc_audio(MIX_HOSTMIX, MIX_HOSTRATE, tries[t]);
        }
        if (!ctrl) {
            ctrl = alloc_audio(AHI_DEFAULT_ID, AHI_DEFAULT_FREQ, tries[t]);
        }
        if (ctrl) {
            nvoices = tries[t];
        }
    }
    if (!ctrl) {
        ahi_close();
        return 0;
    }
    hostmix = mode_is_hostmix(AHI_INVALID_ID, ctrl);
    for (k = 0; k < 2; k++) {
        struct AHISampleInfo si;
        si.ahisi_Type = k ? AHIST_S16S : AHIST_M16S;
        si.ahisi_Address = NULL;
        si.ahisi_Length = 0xFFFFFFFFul;
        if (AHI_LoadSound((UWORD)k, AHIST_DYNAMICSAMPLE, &si, ctrl) != AHIE_OK) {
            ahi_close();
            return 0;
        }
    }
    /* Host mix: n / 2, which the driver's 2 / n makes unity, so a voice can
     * be up to 2.0; AHI's mixers: n, each voice at most 1.0 (mc_volpan). */
    mv.ahie_Effect = AHIET_MASTERVOLUME;
    mv.ahiemv_Volume = (Fixed)(hostmix ? (ULONG)nvoices * 0x8000ul : (ULONG)nvoices * 0x10000ul);
    AHI_SetEffect(&mv, ctrl);
    for (k = 0; k < nvoices; k++) {
        AHI_SetFreq((UWORD)k, 0, ctrl, AHISF_IMM);
        AHI_SetVol((UWORD)k, 0, 0x8000, ctrl, AHISF_IMM);
        AHI_SetSound((UWORD)k, AHI_NOSOUND, 0, 0, ctrl, AHISF_IMM);
    }
    mixfreq = MIX_HOSTRATE;
    tags[0].ti_Tag = AHIC_MixFreq_Query; tags[0].ti_Data = (ULONG)&mixfreq;
    tags[1].ti_Tag = TAG_DONE;           tags[1].ti_Data = 0;
    AHI_ControlAudioA(ctrl, tags);
    return 1;
}

static int ahi_play(void)
{
    struct TagItem tags[2];
    tags[0].ti_Tag = AHIC_Play; tags[0].ti_Data = TRUE;
    tags[1].ti_Tag = TAG_DONE;  tags[1].ti_Data = 0;
    return AHI_ControlAudioA(ctrl, tags) == AHIE_OK;
}

/* ---- Files -------------------------------------------------------------------------------- */

/* A string into a buffer of n bytes, cut to fit, always ended. */
static void copy_str(char *out, const char *s, size_t n)
{
    size_t l = strlen(s);
    if (l > n - 1) {
        l = n - 1;
    }
    memcpy(out, s, l);
    out[l] = 0;
}

static int exists(const char *name)
{
    BPTR l = Lock((CONST_STRPTR)name, SHARED_LOCK);
    if (l) {
        UnLock(l);
        return 1;
    }
    return 0;
}

/* The whole name, so the process finds the file from its own directory. */
static void full_name(const char *name, char *out, size_t n)
{
    BPTR l = Lock((CONST_STRPTR)name, SHARED_LOCK);
    if (!l || !NameFromLock(l, (STRPTR)out, (LONG)n)) {
        copy_str(out, name, n);
    }
    if (l) {
        UnLock(l);
    }
}

static uint8_t *read_file(const char *name, uint32_t *size)
{
    FILE *f = fopen(name, "rb");
    uint8_t *d = NULL;
    long len;
    if (!f) {
        SDL_SetError("Mix: can't open %s", name);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
        d = malloc((size_t)len);
        if (d && fread(d, 1, (size_t)len, f) != (size_t)len) {
            free(d);
            d = NULL;
        }
        *size = (uint32_t)len;
    }
    fclose(f);
    if (!d) {
        SDL_SetError("Mix: can't read %s", name);
    }
    return d;
}

static int dt_open_lib(void)
{
    if (!mix_DataTypesBase) {
        mix_DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 39);
    }
    return mix_DataTypesBase != NULL;
}

/* A sound datatype's reader on name (DTST_FILE), or memory: its channels,
 * rate, length (0: unknown) and who decodes it. */
static Object *dt_reader(const char *name, const void *mem, uint32_t size, int *ch, uint32_t *rate,
                         uint32_t *frames, const char **by)
{
    Object *o;
    ULONG c = 0, r = 0, len = 0;
    STRPTR who = NULL;
    if (!dt_open_lib()) {
        return NULL;
    }
    if (mem) {
        o = NewDTObject(NULL, DTA_SourceType, DTST_MEMORY, DTA_SourceAddress, (ULONG)mem, DTA_SourceSize, size,
                        DTA_GroupID, GID_SOUND, OIA_Reader, TRUE, TAG_DONE);
    } else {
        o = NewDTObject((APTR)name, DTA_GroupID, GID_SOUND, OIA_Reader, TRUE, TAG_DONE);
    }
    if (!o) {
        return NULL;
    }
    if (!GetDTAttrs(o, OIA_Channels, (ULONG)&c, TAG_DONE) || c < 1 || c > 2) {
        /* not one of ours: its sound isn't streamed */
        DisposeDTObject(o);
        return NULL;
    }
    GetDTAttrs(o, SDTA_SamplesPerSec, (ULONG)&r, SDTA_SampleLength, (ULONG)&len, TAG_DONE);
    if (!r) {
        DisposeDTObject(o);
        return NULL;
    }
    GetDTAttrs(o, OIA_DecodedBy, (ULONG)&who, TAG_DONE);
    *ch = (int)c;
    *rate = r;
    *frames = len;
    *by = (const char *)who;
    return o;
}

static uint32_t dt_read(Object *o, int16_t *buf, uint32_t frames)
{
    struct oiReadSound m;
    m.MethodID = OIM_READSOUND;
    m.oirs_GInfo = NULL;
    m.oirs_Buffer = (WORD *)buf;
    m.oirs_Frames = frames;
    return (uint32_t)DoMethodA(o, (Msg)&m);
}

/* The whole sound from a reader, for an effect. */
static int16_t *dt_read_all(Object *o, int ch, uint32_t frames, uint32_t *got)
{
    uint32_t cap = frames ? frames + 1 : 65536, have = 0;
    int16_t *pcm = AllocVec(cap * ch * 2, MEMF_PUBLIC);
    for (;;) {
        uint32_t n;
        if (!pcm) {
            return NULL;
        }
        if (have + 4096 > cap) {
            int16_t *grow = AllocVec(cap * 2 * ch * 2, MEMF_PUBLIC);
            if (grow) {
                CopyMem(pcm, grow, have * ch * 2);
            }
            FreeVec(pcm);
            pcm = grow;
            cap *= 2;
            continue;
        }
        n = dt_read(o, pcm + have * ch, cap - have > 32768 ? 32768 : cap - have);
        if (!n) {
            break;
        }
        have += n;
    }
    *got = have;
    return pcm;
}

/* Which file, as mw_choose says: WAV or a datatype (whose reader is
 * returned open in *dto when dt_keep, else probed and closed). */
struct pick {
    int use;                            /* MW_USE_ */
    char wav[PATH_MAX_], dt[PATH_MAX_];
    Object *dto;
    int ch;
    uint32_t rate, frames;
    const char *by;
};

static int pick_file(const char *asked, struct pick *p, int dt_keep)
{
    int is_wav = mw_is_wav_name(asked), there = exists(asked), wav_there, dt_ok = 0, dt_68k = 0;
    memset(p, 0, sizeof *p);
    if (is_wav) {
        copy_str(p->wav, asked, sizeof p->wav);
        wav_there = there;
        if (!there && mw_sibling(asked, ".ogg", p->dt, sizeof p->dt) && exists(p->dt)) {
            p->dto = dt_reader(p->dt, NULL, 0, &p->ch, &p->rate, &p->frames, &p->by);
        }
    } else {
        copy_str(p->dt, asked, sizeof p->dt);
        wav_there = mw_sibling(asked, ".wav", p->wav, sizeof p->wav) && exists(p->wav);
        if (there) {
            p->dto = dt_reader(p->dt, NULL, 0, &p->ch, &p->rate, &p->frames, &p->by);
        }
    }
    if (p->dto) {
        dt_ok = 1;
        dt_68k = mw_on_68k(p->by);
    }
    p->use = mw_choose(is_wav, there, wav_there, dt_ok, dt_68k);
    if (p->dto && (p->use != MW_USE_DT || !dt_keep)) {
        DisposeDTObject(p->dto);
        p->dto = NULL;
    }
    if (p->use == MW_NONE) {
        SDL_SetError("Mix: can't play %s", asked);
    }
    return p->use;
}

/* ---- The process -------------------------------------------------------------------------- */

static void music_close_source(void)
{
    if (mus_dto) {
        DisposeDTObject(mus_dto);
        mus_dto = NULL;
    }
}

static void segs_reclaim(void)
{
    int k;
    segfree = 0;
    for (k = 0; k < MC_SEGS; k++) {
        if (segbuf[k]) {
            segfree |= 1u << k;
        }
    }
}

static int segs_alloc(void)
{
    int k;
    for (k = 0; k < MC_SEGS; k++) {
        if (!segbuf[k] && !(segbuf[k] = AllocVec(SEG_BYTES, MEMF_PUBLIC))) {
            return 0;
        }
    }
    return 1;
}

/* Opens the music's source at frame pos. In the process. */
static int music_open_source(Mix_Music *m, uint32_t pos)
{
    music_close_source();
    mus_pos = 0;
    if (m->kind == MK_DT) {
        int ch;
        uint32_t rate, frames;
        const char *by;
        mus_dto = dt_reader(m->path, NULL, 0, &ch, &rate, &frames, &by);
        if (!mus_dto) {
            return 0;
        }
        if (by) {
            copy_str(m->by_copy, by, sizeof m->by_copy);
            m->by = m->by_copy;
        }
        while (pos > 0) {               /* no seek: read up to it */
            uint32_t n = SEG_BYTES / (2 * m->channels), got;
            if (n > pos) {
                n = pos;
            }
            got = dt_read(mus_dto, segbuf[0], n);
            if (!got) {
                break;
            }
            pos -= got;
        }
        return 1;
    }
    if (m->kind == MK_ADPCM) {
        pos -= pos % m->info.block_frames;
    }
    mus_pos = pos < m->frames ? pos : m->frames;
    return 1;
}

/* One piece of the music into buffer k (MK_PCM: no buffer). frames, or 0 at its end. */
static uint32_t music_piece(Mix_Music *m, int k, const int16_t **data)
{
    uint32_t cap = SEG_BYTES / (2 * (uint32_t)m->channels), want = m->rate / 2;
    if (want > cap) {
        want = cap;
    }
    if (m->kind == MK_PCM) {
        uint32_t n = m->frames - mus_pos;
        if (n > m->rate) {
            n = m->rate;                /* a second a piece, straight from the sound */
        }
        *data = m->pcm + mus_pos * m->channels;
        mus_pos += n;
        return n;
    }
    *data = segbuf[k];
    if (m->kind == MK_ADPCM) {
        uint32_t bf = m->info.block_frames, n = 0;
        uint32_t blocks = m->info.data_len / m->info.block_align;
        while (n + bf <= cap && n < want && mus_pos / bf < blocks) {
            mw_adpcm_block(&m->info, mus_pos / bf, segbuf[k] + n);
            n += bf;
            mus_pos += bf;
        }
        return n;
    }
    return mus_dto ? dt_read(mus_dto, segbuf[k], want) : 0;
}

/* Tops the music voice up. In the process, with cmd_lock held. */
static void music_fill(void)
{
    Mix_Music *m = mus_cur;
    int restarts = 0;
    if (!m) {
        return;
    }
    for (;;) {
        int room, k = -1;
        uint32_t n;
        const int16_t *data = NULL;
        Disable();
        room = mc_music_room(&core);
        Enable();
        if (!room) {
            return;
        }
        if (m->kind != MK_PCM) {
            for (k = 0; k < MC_SEGS && !(segfree & (1u << k)); k++) {
            }
            if (k == MC_SEGS) {
                return;
            }
        }
        n = music_piece(m, k, &data);
        if (n) {
            if (k >= 0) {
                segfree &= ~(1u << k);
            }
            Disable();
            mc_music_put(&core, data, n, k);
            Enable();
            continue;
        }
        /* its end: again, or done */
        if (mus_loops != 0 && restarts++ < 2 && music_open_source(m, 0)) {
            if (mus_loops > 0) {
                mus_loops--;
            }
            continue;
        }
        Disable();
        mc_music_end(&core);
        Enable();
        return;
    }
}

static void music_stop_here(void)
{
    Disable();
    mc_music_halt(&core);
    core.music_finished = 0;
    Enable();
    music_close_source();
    segs_reclaim();
    mus_cur = NULL;
}

static int do_command(void)
{
    Mix_Music *m = cmd.music;
    uint32_t pos;
    switch (cmd.type) {
    case CMD_HALT:
        music_stop_here();
        mus_on = 0;
        return 0;
    case CMD_PLAY:
    case CMD_POS:
        if (cmd.type == CMD_POS) {
            m = mus_cur;
            if (!m) {
                return -1;
            }
        }
        music_stop_here();
        if (!segs_alloc()) {
            mus_on = 0;
            SDL_SetError("Mix: no memory for the music");
            return -1;
        }
        segs_reclaim();
        pos = cmd.pos > 0 ? (uint32_t)(cmd.pos * (double)m->rate) : 0;
        if (!music_open_source(m, pos)) {
            mus_on = 0;
            SDL_SetError("Mix: can't open the music");
            return -1;
        }
        if (cmd.type == CMD_PLAY) {
            /* SDL_mixer 1.2: loops is how many times it plays; 0 is once */
            mus_loops = cmd.loops < 0 ? -1 : cmd.loops > 1 ? cmd.loops - 1 : 0;
        }
        mus_cur = m;
        Disable();
        mc_music_start(&core, m->rate, m->channels, cmd.type == CMD_PLAY ? cmd.fade_ms : 0);
        if (mus_paused) {
            mc_music_pause(&core, 1);
        }
        Enable();
        mus_on = 1;
        music_fill();
        return 0;
    }
    return -1;
}

static int music_call(int type, Mix_Music *m, int loops, int fade_ms, double pos)
{
    int r;
    if (!opened) {
        SDL_SetError("Mix: audio not open");
        return -1;
    }
    ObtainSemaphore(&cmd_lock);
    cmd.type = type;
    cmd.music = m;
    cmd.loops = loops;
    cmd.fade_ms = fade_ms;
    cmd.pos = pos;
    if (FindTask(NULL) == proc) {
        r = do_command();               /* from a hook the process called */
    } else {
        BYTE sig = AllocSignal(-1);
        if (sig < 0) {
            ReleaseSemaphore(&cmd_lock);
            return -1;
        }
        SetSignal(0, 1UL << sig);
        cmd.waiter = FindTask(NULL);
        cmd.sig = sig;
        cmd.pending = 1;
        wake();
        Wait(1UL << sig);
        FreeSignal(sig);
        r = cmd.result;
    }
    ReleaseSemaphore(&cmd_lock);
    return r;
}

static void proc_main(void)
{
    struct Task *me = FindTask(NULL);
    BYTE sig = AllocSignal(-1);
    proc_sig = sig;
    if (sig >= 0) {
        proc = me;
    }
    Signal(starter, 1UL << starter_sig);
    while (sig >= 0 && !proc_quit) {
        uint32_t fin, freed;
        int mfin, k;
        Wait((1UL << sig) | SIGBREAKF_CTRL_C);
        if (proc_quit) {
            break;
        }
        if (cmd.pending) {
            cmd.pending = 0;
            cmd.result = do_command();
            Signal(cmd.waiter, 1UL << cmd.sig);
        }
        Disable();
        mc_tick(&core);
        fin = mc_take_finished(&core);
        freed = mc_take_freed(&core);
        mfin = core.music_finished;
        core.music_finished = 0;
        Enable();
        if (AttemptSemaphore(&cmd_lock)) {
            segfree |= freed;
            if (mfin) {
                music_close_source();
                segs_reclaim();
                mus_cur = NULL;
                mus_on = 0;
            } else {
                music_fill();
            }
            ReleaseSemaphore(&cmd_lock);
        } else {
            /* the program is in a music call, waiting for us next time round */
            Disable();
            core.mus.freed |= freed;
            core.music_finished |= mfin;
            Enable();
            mfin = 0;
        }
        for (k = 0; fin && k < MC_MAX_CHANS; k++) {
            if ((fin & (1u << k)) && channel_finished) {
                channel_finished(k);
            }
        }
        if (mfin && music_finished) {
            music_finished();
        }
    }
    Disable();
    proc = NULL;
    Enable();
    if (sig >= 0) {
        FreeSignal(sig);
    }
    while (!proc_quit) {
        Wait(SIGBREAKF_CTRL_C);
    }
    Forbid();
    Signal(starter, 1UL << starter_sig);
}

static int proc_start(void)
{
    struct Process *p;
    BYTE sig = AllocSignal(-1);
    if (sig < 0) {
        return 0;
    }
    proc_quit = 0;
    starter = FindTask(NULL);
    starter_sig = sig;
    SetSignal(0, 1UL << sig);
    p = CreateNewProcTags(NP_Entry, (ULONG)proc_main, NP_Name, (ULONG)"SDL_mixer", NP_StackSize, MIX_STACK,
                          NP_Priority, MIX_PRIORITY, NP_Input, 0, NP_Output, 0, NP_CloseInput, FALSE,
                          NP_CloseOutput, FALSE, NP_WindowPtr, (ULONG)-1L, TAG_DONE);
    if (!p) {
        FreeSignal(sig);
        return 0;
    }
    Wait(1UL << sig);
    if (!proc) {
        /* it couldn't get a signal: let it end */
        SetSignal(0, 1UL << sig);
        proc_quit = 1;
        Signal(&p->pr_Task, SIGBREAKF_CTRL_C);
        Wait(1UL << sig);
        FreeSignal(sig);
        return 0;
    }
    FreeSignal(sig);
    return 1;
}

static void proc_stop(void)
{
    struct Task *t = proc;
    BYTE sig;
    if (!t) {
        return;
    }
    sig = AllocSignal(-1);
    if (sig < 0) {
        sig = SIGBREAKB_CTRL_F;
    }
    SetSignal(0, 1UL << sig);
    Forbid();
    starter = FindTask(NULL);
    starter_sig = sig;
    proc_quit = 1;
    Signal(t, SIGBREAKF_CTRL_C);
    Permit();
    Wait(1UL << sig);
    if (sig != SIGBREAKB_CTRL_F) {
        FreeSignal(sig);
    }
}

/* ---- The calls: opening ------------------------------------------------------------------- */

const SDL_version *Mix_Linked_Version(void)
{
    static SDL_version v = { SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_PATCHLEVEL };
    return &v;
}

int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize)
{
    struct mc_backend be;
    (void)frequency;
    (void)format;
    (void)channels;
    (void)chunksize;
    if (opened) {
        return 0;
    }
    InitSemaphore(&cmd_lock);
    if (!ahi_open()) {
        SDL_SetError("Mix_OpenAudio: AHI has no voices for us (ahi.device or its mode)");
        return -1;
    }
    be.ctx = NULL;
    be.sound = be_sound;
    be.vol = be_vol;
    be.freq = be_freq;
    Disable();
    mc_init(&core, &be, nvoices - 1);
    Enable();
    if (!proc_start()) {
        ahi_close();
        SDL_SetError("Mix_OpenAudio: no process");
        return -1;
    }
    if (!ahi_play()) {
        proc_stop();
        ahi_close();
        SDL_SetError("Mix_OpenAudio: AHI won't play");
        return -1;
    }
    opened = 1;
    return 0;
}

void Mix_CloseAudio(void)
{
    int k;
    if (!opened) {
        return;
    }
    Mix_HaltMusic();
    Disable();
    mc_halt(&core, -1);
    Enable();
    proc_stop();
    ahi_close();
    for (k = 0; k < MC_SEGS; k++) {
        if (segbuf[k]) {
            FreeVec(segbuf[k]);
            segbuf[k] = NULL;
        }
    }
    if (mix_DataTypesBase) {
        CloseLibrary(mix_DataTypesBase);
        mix_DataTypesBase = NULL;
    }
    opened = 0;
}

int Mix_QuerySpec(int *frequency, Uint16 *format, int *channels)
{
    if (!opened) {
        return 0;
    }
    if (frequency) {
        *frequency = (int)mixfreq;
    }
    if (format) {
        *format = AUDIO_S16SYS;
    }
    if (channels) {
        *channels = 2;
    }
    return 1;
}

int Mix_AllocateChannels(int n)
{
    int r;
    if (!opened) {
        return n < 0 ? 0 : n;
    }
    Disable();
    r = mc_allocate(&core, n);
    Enable();
    return r;
}

int Mix_ReserveChannels(int n)
{
    int r;
    Disable();
    r = mc_reserve(&core, n);
    Enable();
    return r;
}

/* ---- Effects ------------------------------------------------------------------------------- */

static Mix_Chunk *chunk_make(int16_t *pcm, uint32_t frames, uint32_t rate, int ch)
{
    struct mix_chunk *c = malloc(sizeof *c);
    if (!c) {
        FreeVec(pcm);
        SDL_SetError("Mix: out of memory");
        return NULL;
    }
    c->pub.allocated = 1;
    c->pub.abuf = (Uint8 *)pcm;
    c->pub.alen = frames * (uint32_t)ch * 2;
    c->pub.volume = MIX_MAX_VOLUME;
    c->snd.data = pcm;
    c->snd.frames = frames;
    c->snd.rate = rate;
    c->snd.channels = (uint8_t)ch;
    c->snd.volume = MIX_MAX_VOLUME;
    return &c->pub;
}

/* A WAV in memory to a chunk. */
static Mix_Chunk *chunk_wav(const uint8_t *d, uint32_t size)
{
    struct mw_info w;
    const char *err = mw_parse(d, size, &w);
    int16_t *pcm;
    if (err) {
        SDL_SetError("Mix: %s", err);
        return NULL;
    }
    pcm = AllocVec(w.frames * (uint32_t)w.channels * 2, MEMF_PUBLIC);
    if (!pcm) {
        SDL_SetError("Mix: out of memory");
        return NULL;
    }
    if (w.format == MW_PCM) {
        mw_pcm16(&w, pcm);
    } else {
        uint32_t b, blocks = w.data_len / w.block_align;
        for (b = 0; b < blocks; b++) {
            mw_adpcm_block(&w, b, pcm + b * w.block_frames);
        }
    }
    return chunk_make(pcm, w.frames, w.rate, w.channels);
}

static Mix_Chunk *chunk_dt(Object *o, int ch, uint32_t rate, uint32_t frames)
{
    uint32_t got = 0;
    int16_t *pcm = dt_read_all(o, ch, frames, &got);
    DisposeDTObject(o);
    if (!pcm || !got) {
        if (pcm) {
            FreeVec(pcm);
        }
        SDL_SetError("Mix: the datatype gave no sound");
        return NULL;
    }
    return chunk_make(pcm, got, rate, ch);
}

Mix_Chunk *Mix_LoadWAV(const char *file)
{
    struct pick p;
    uint32_t size = 0;
    uint8_t *d;
    Mix_Chunk *c;
    if (!file) {
        SDL_SetError("Mix_LoadWAV: no file");
        return NULL;
    }
    switch (pick_file(file, &p, 1)) {
    case MW_USE_DT:
        return chunk_dt(p.dto, p.ch, p.rate, p.frames);
    case MW_USE_WAV:
        d = read_file(p.wav, &size);
        if (!d) {
            return NULL;
        }
        c = chunk_wav(d, size);
        free(d);
        return c;
    }
    return NULL;
}

Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc)
{
    uint8_t head[12], *d = NULL;
    uint32_t size, have = 12, cap;
    Mix_Chunk *c = NULL;
    if (!src) {
        SDL_SetError("Mix_LoadWAV_RW: no stream");
        return NULL;
    }
    if (SDL_RWread(src, head, 12, 1) < 1) {
        SDL_SetError("Mix_LoadWAV_RW: short file");
        goto out;
    }
    size = mw_riff_size(head);
    if (size) {
        /* the length the header gives: streams that can't seek or see their end */
        if (size < 12 || size > 64u << 20 || !(d = malloc(size))) {
            SDL_SetError("Mix_LoadWAV_RW: bad size");
            goto out;
        }
        memcpy(d, head, 12);
        if (size > 12 && SDL_RWread(src, d + 12, size - 12, 1) < 1) {
            SDL_SetError("Mix_LoadWAV_RW: short file");
            goto out;
        }
        c = chunk_wav(d, size);
        goto out;
    }
    /* anything else: to its end, then the datatypes from memory */
    cap = 65536;
    if (!(d = malloc(cap))) {
        goto out;
    }
    memcpy(d, head, 12);
    for (;;) {
        int n;
        if (have == cap) {
            uint8_t *g = cap < (64u << 20) ? realloc(d, cap * 2) : NULL;
            if (!g) {
                SDL_SetError("Mix_LoadWAV_RW: too big");
                goto out;
            }
            d = g;
            cap *= 2;
        }
        n = SDL_RWread(src, d + have, 1, (int)(cap - have));
        if (n <= 0) {
            break;
        }
        have += (uint32_t)n;
    }
    {
        int ch;
        uint32_t rate, frames;
        const char *by;
        Object *o = dt_reader(NULL, d, have, &ch, &rate, &frames, &by);
        if (o) {
            c = chunk_dt(o, ch, rate, frames);
        } else {
            SDL_SetError("Mix_LoadWAV_RW: no datatype reads it");
        }
    }
out:
    free(d);
    if (freesrc) {
        SDL_RWclose(src);
    }
    return c;
}

Mix_Chunk *Mix_QuickLoad_RAW(Uint8 *mem, Uint32 len)
{
    int16_t *pcm;
    if (!mem || len < 4) {
        return NULL;
    }
    /* the mix's format: 16-bit stereo at Mix_QuerySpec's rate */
    pcm = AllocVec(len & ~3u, MEMF_PUBLIC);
    if (!pcm) {
        return NULL;
    }
    CopyMem(mem, pcm, len & ~3u);
    return chunk_make(pcm, len / 4, mixfreq ? mixfreq : MIX_HOSTRATE, 2);
}

void Mix_FreeChunk(Mix_Chunk *chunk)
{
    struct mix_chunk *c = (struct mix_chunk *)chunk;
    if (!c) {
        return;
    }
    if (opened) {
        Disable();
        mc_forget(&core, &c->snd);
        Enable();
    }
    FreeVec((APTR)c->snd.data);
    free(c);
}

int Mix_VolumeChunk(Mix_Chunk *chunk, int volume)
{
    struct mix_chunk *c = (struct mix_chunk *)chunk;
    int old;
    if (!c) {
        return -1;
    }
    old = c->pub.volume;
    if (volume >= 0) {
        c->pub.volume = (Uint8)(volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME : volume);
        c->snd.volume = c->pub.volume;
        if (opened) {
            Disable();
            mc_sound_changed(&core, &c->snd);
            Enable();
        }
    }
    return old;
}

int Mix_FadeInChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ms, int ticks)
{
    struct mix_chunk *c = (struct mix_chunk *)chunk;
    int r;
    if (!opened || !c) {
        SDL_SetError("Mix_PlayChannel: audio not open, or no chunk");
        return -1;
    }
    c->snd.volume = c->pub.volume;
    Disable();
    r = mc_play(&core, channel, &c->snd, loops, ticks, ms);
    Enable();
    if (r < 0) {
        SDL_SetError("Mix_PlayChannel: no free channel");
    }
    return r;
}

int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks)
{
    return Mix_FadeInChannelTimed(channel, chunk, loops, 0, ticks);
}

#define CHAN_CALL(expr) \
    do {                \
        if (!opened) {  \
            return 0;   \
        }               \
        Disable();      \
        r = (expr);     \
        Enable();       \
        return r;       \
    } while (0)

int Mix_Volume(int channel, int volume)
{
    int r;
    if (!opened) {
        return MIX_MAX_VOLUME;
    }
    Disable();
    r = mc_volume(&core, channel, volume);
    Enable();
    return r;
}

int Mix_SetPanning(int channel, Uint8 left, Uint8 right)
{
    int r;
    CHAN_CALL(mc_panning(&core, channel, left, right));
}

int Mix_HaltChannel(int channel)
{
    int r;
    CHAN_CALL(mc_halt(&core, channel));
}

int Mix_ExpireChannel(int channel, int ticks)
{
    int r;
    CHAN_CALL(mc_expire(&core, channel, ticks));
}

int Mix_FadeOutChannel(int channel, int ms)
{
    int r;
    CHAN_CALL(mc_fade_out(&core, channel, ms));
}

int Mix_Paused(int channel)
{
    int r;
    CHAN_CALL(mc_paused(&core, channel));
}

int Mix_Playing(int channel)
{
    int r;
    CHAN_CALL(mc_playing(&core, channel));
}

Mix_Fading Mix_FadingChannel(int which)
{
    int r;
    if (!opened) {
        return MIX_NO_FADING;
    }
    Disable();
    r = mc_fading(&core, which);
    Enable();
    return r == MC_FADING_OUT ? MIX_FADING_OUT : r == MC_FADING_IN ? MIX_FADING_IN : MIX_NO_FADING;
}

void Mix_Pause(int channel)
{
    if (opened) {
        Disable();
        mc_pause(&core, channel);
        Enable();
    }
}

void Mix_Resume(int channel)
{
    if (opened) {
        Disable();
        mc_resume(&core, channel);
        Enable();
    }
}

Mix_Chunk *Mix_GetChunk(int channel)
{
    const mc_sound *s;
    if (!opened) {
        return NULL;
    }
    Disable();
    s = mc_sound_of(&core, channel);
    Enable();
    if (!s) {
        return NULL;
    }
    return &((struct mix_chunk *)((char *)s - offsetof(struct mix_chunk, snd)))->pub;
}

void Mix_ChannelFinished(void (*fn)(int channel))
{
    channel_finished = fn;
}

int Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *arg)
{
    (void)chan;
    (void)f;
    (void)d;
    (void)arg;
    SDL_SetError("Mix_RegisterEffect: AHI mixes, so there are no effects on the mix");
    return 0;
}

int Mix_UnregisterAllEffects(int channel)
{
    (void)channel;
    return 1;
}

/* ---- Music --------------------------------------------------------------------------------- */

static Mix_MusicType type_of(const char *name)
{
    const char *e = strrchr(name, '.');
    char x[6];
    int i;
    if (!e) {
        return MUS_OGG;
    }
    for (i = 0; i < 5 && e[i + 1]; i++) {
        x[i] = (char)(e[i + 1] | 0x20);
    }
    x[i] = 0;
    if (!strcmp(x, "wav")) {
        return MUS_WAV;
    }
    if (!strcmp(x, "mp3")) {
        return MUS_MP3;
    }
    if (!strcmp(x, "flac")) {
        return MUS_FLAC;
    }
    if (!strcmp(x, "mid") || !strcmp(x, "midi")) {
        return MUS_MID;
    }
    if (!strcmp(x, "mod") || !strcmp(x, "xm") || !strcmp(x, "s3m") || !strcmp(x, "it") || !strcmp(x, "med") ||
        !strcmp(x, "669") || !strcmp(x, "mtm")) {
        return MUS_MOD;
    }
    return MUS_OGG;
}

Mix_Music *Mix_LoadMUS(const char *file)
{
    struct pick p;
    Mix_Music *m;
    uint32_t size = 0;
    if (!file) {
        SDL_SetError("Mix_LoadMUS: no file");
        return NULL;
    }
    if (!(m = calloc(1, sizeof *m))) {
        SDL_SetError("Mix: out of memory");
        return NULL;
    }
    switch (pick_file(file, &p, 0)) {
    case MW_USE_DT:
        m->kind = MK_DT;
        m->type = type_of(p.dt);
        full_name(p.dt, m->path, sizeof m->path);
        m->rate = p.rate;
        m->channels = p.ch;
        m->frames = p.frames;
        if (p.by) {
            copy_str(m->by_copy, p.by, sizeof m->by_copy);
        }
        m->by = m->by_copy;
        return m;
    case MW_USE_WAV: {
        const char *err;
        m->type = MUS_WAV;
        m->by = "WAV";
        if (!(m->file = read_file(p.wav, &size))) {
            break;
        }
        if ((err = mw_parse(m->file, size, &m->info))) {
            SDL_SetError("Mix_LoadMUS: %s", err);
            break;
        }
        m->rate = m->info.rate;
        m->channels = m->info.channels;
        m->frames = m->info.frames;
        if (m->info.format == MW_ADPCM) {
            m->kind = MK_ADPCM;     /* decoded as it plays: about 11 KB a second kept */
            return m;
        }
        m->kind = MK_PCM;
        if (!(m->pcm = AllocVec(m->frames * (uint32_t)m->channels * 2, MEMF_PUBLIC))) {
            SDL_SetError("Mix_LoadMUS: out of memory");
            break;
        }
        mw_pcm16(&m->info, m->pcm);
        free(m->file);
        m->file = NULL;
        memset(&m->info, 0, sizeof m->info);
        return m;
    }
    }
    if (m->file) {
        free(m->file);
    }
    if (m->pcm) {
        FreeVec(m->pcm);
    }
    free(m);
    return NULL;
}

void Mix_FreeMusic(Mix_Music *m)
{
    if (!m) {
        return;
    }
    if (opened && mus_cur == m) {
        music_call(CMD_HALT, NULL, 0, 0, 0);
    }
    if (m->file) {
        free(m->file);
    }
    if (m->pcm) {
        FreeVec(m->pcm);
    }
    free(m);
}

Mix_MusicType Mix_GetMusicType(const Mix_Music *m)
{
    if (!m) {
        return mus_on && mus_cur ? mus_cur->type : MUS_NONE;
    }
    return m->type;
}

int Mix_FadeInMusicPos(Mix_Music *m, int loops, int ms, double position)
{
    if (!m) {
        SDL_SetError("Mix_PlayMusic: no music");
        return -1;
    }
    if (position > 0 && m->type == MUS_MOD) {
        position = 0;                   /* a pattern number: not here */
    }
    mus_paused = 0;
    return music_call(CMD_PLAY, m, loops, ms, position);
}

int Mix_FadeInMusic(Mix_Music *m, int loops, int ms)
{
    return Mix_FadeInMusicPos(m, loops, ms, 0.0);
}

int Mix_PlayMusic(Mix_Music *m, int loops)
{
    return Mix_FadeInMusicPos(m, loops, 0, 0.0);
}

int Mix_SetMusicPosition(double position)
{
    if (!mus_on || !mus_cur) {
        SDL_SetError("Mix_SetMusicPosition: no music playing");
        return -1;
    }
    if (mus_cur->type == MUS_MOD) {
        SDL_SetError("Mix_SetMusicPosition: not for a tracker module here");
        return -1;
    }
    return music_call(CMD_POS, NULL, 0, 0, position);
}

void Mix_RewindMusic(void)
{
    if (mus_on && mus_cur) {
        music_call(CMD_POS, NULL, 0, 0, 0.0);
    }
}

int Mix_HaltMusic(void)
{
    if (opened && mus_on) {
        music_call(CMD_HALT, NULL, 0, 0, 0);
    }
    return 0;
}

int Mix_FadeOutMusic(int ms)
{
    int r;
    if (!opened || !mus_on) {
        return 0;
    }
    if (ms <= 0) {
        Mix_HaltMusic();
        return 1;
    }
    Disable();
    r = mc_music_fade_out(&core, ms);
    Enable();
    return r;
}

Mix_Fading Mix_FadingMusic(void)
{
    int f;
    if (!opened || !mus_on) {
        return MIX_NO_FADING;
    }
    Disable();
    f = core.mus.active ? core.mus.fade : MC_NO_FADING;
    Enable();
    return f == MC_FADING_OUT ? MIX_FADING_OUT : f == MC_FADING_IN ? MIX_FADING_IN : MIX_NO_FADING;
}

int Mix_VolumeMusic(int volume)
{
    int r;
    if (!opened) {
        return core.mus.volume ? core.mus.volume : MIX_MAX_VOLUME;
    }
    Disable();
    r = mc_music_volume(&core, volume);
    Enable();
    return r;
}

void Mix_PauseMusic(void)
{
    if (opened) {
        mus_paused = 1;
        Disable();
        mc_music_pause(&core, 1);
        Enable();
    }
}

void Mix_ResumeMusic(void)
{
    if (opened) {
        mus_paused = 0;
        Disable();
        mc_music_pause(&core, 0);
        Enable();
    }
}

int Mix_PausedMusic(void)
{
    return mus_on && mus_paused;
}

int Mix_PlayingMusic(void)
{
    return mus_on;
}

void Mix_HookMusicFinished(void (*fn)(void))
{
    music_finished = fn;
}

const char *Mix_MusicDecodedBy(void)
{
    Mix_Music *m = mus_cur;
    return mus_on && m && m->by ? m->by : "";
}
