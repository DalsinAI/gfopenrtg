/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The mixer's channels and music voice (mix_core.h). Nothing here mixes:
 * AHI does, each channel on a voice of its own. */
#include "mix_core.h"

#include <string.h>

uint32_t mc_ticks(int ms)
{
    uint32_t t;
    if (ms <= 0) {
        return 1;
    }
    t = ((uint32_t)ms * MC_TICK_HZ + 999u) / 1000u;
    return t ? t : 1;
}

void mc_init(struct mc_core *c, const struct mc_backend *be, int nvoices)
{
    int k;
    memset(c, 0, sizeof *c);
    c->be = *be;
    c->nvoices = nvoices > MC_MAX_CHANS ? MC_MAX_CHANS : nvoices;
    c->nchans = c->nvoices < 8 ? c->nvoices : 8;   /* SDL_mixer's default of 8 */
    for (k = 0; k < MC_MAX_CHANS; k++) {
        c->chan[k].volume = 128;
        c->chan[k].left = c->chan[k].right = 255;
    }
    c->mus.volume = 128;
}

/* A fade's level now, Q8 (256 full). */
static int32_t fade_level(int fade, uint32_t total, uint32_t left)
{
    if (fade == MC_NO_FADING || total == 0) {
        return 256;
    }
    if (fade == MC_FADING_OUT) {
        return (int32_t)(((uint64_t)left * 256u) / total);
    }
    return (int32_t)(((uint64_t)(total - left) * 256u) / total);
}

/* Channel ch's gains to its voice. */
static void chan_vol(struct mc_core *c, int ch)
{
    struct mc_chan *h = &c->chan[ch];
    int32_t base, f;
    if (!h->snd) {
        return;
    }
    base = (int32_t)h->volume * (int32_t)h->snd->volume;            /* Q14 */
    f = fade_level(h->fade, h->fade_total, h->fade_left);
    base = (base * f) >> 8;
    c->be.vol(c->be.ctx, ch, (base * h->left) / 255, (base * h->right) / 255, h->snd->channels == 2);
}

static void chan_free(struct mc_core *c, int ch, int tell)
{
    struct mc_chan *h = &c->chan[ch];
    if (!h->snd) {
        return;
    }
    h->snd = NULL;
    h->ending = 0;
    h->paused = 0;
    h->fade = MC_NO_FADING;
    h->fade_total = h->fade_left = 0;
    h->expire = 0;
    if (tell) {
        c->finished |= 1u << ch;
    }
}

/* What comes after the play now on channel ch's voice: the same sound
 * again while loops are left, else AHI_NOSOUND. */
static void chan_queue_next(struct mc_core *c, int ch)
{
    struct mc_chan *h = &c->chan[ch];
    if (h->loops != 0) {
        c->be.sound(c->be.ctx, ch, h->snd->data, h->snd->frames, h->snd->channels, 0);
        h->ending = 0;
    } else {
        c->be.sound(c->be.ctx, ch, NULL, 0, 0, 0);
        h->ending = 1;
    }
}

static int valid(const struct mc_core *c, int ch)
{
    return ch >= 0 && ch < c->nchans && ch < c->nvoices;
}

int mc_play(struct mc_core *c, int ch, const mc_sound *s, int loops, int expire_ms, int fade_in_ms)
{
    struct mc_chan *h;
    int k;
    if (!s || !s->frames || !s->data) {
        return -1;
    }
    if (ch < 0) {
        ch = -1;
        for (k = c->reserved; k < c->nchans && k < c->nvoices; k++) {
            if (!c->chan[k].snd) {
                ch = k;
                break;
            }
        }
        if (ch < 0) {
            return -1;
        }
    } else if (!valid(c, ch)) {
        return -1;
    }
    h = &c->chan[ch];
    if (h->snd) {
        chan_free(c, ch, 1);          /* SDL_mixer: the one cut short has finished */
    }
    h->snd = s;
    h->loops = loops < -1 ? -1 : loops;
    h->paused = 0;
    h->expire = expire_ms > 0 ? mc_ticks(expire_ms) : 0;
    if (fade_in_ms > 0) {
        h->fade = MC_FADING_IN;
        h->fade_total = h->fade_left = mc_ticks(fade_in_ms);
    } else {
        h->fade = MC_NO_FADING;
        h->fade_total = h->fade_left = 0;
    }
    chan_vol(c, ch);
    c->be.freq(c->be.ctx, ch, s->rate);
    c->be.sound(c->be.ctx, ch, s->data, s->frames, s->channels, 1);
    chan_queue_next(c, ch);           /* loops counts the plays after this one */
    return ch;
}

static void voice_silence(struct mc_core *c, int v)
{
    c->be.sound(c->be.ctx, v, NULL, 0, 0, 1);
}

int mc_halt(struct mc_core *c, int ch)
{
    int k;
    for (k = 0; k < c->nvoices; k++) {
        if ((ch < 0 || k == ch) && c->chan[k].snd) {
            voice_silence(c, k);
            chan_free(c, k, 1);
        }
    }
    return 0;
}

void mc_pause(struct mc_core *c, int ch)
{
    int k;
    for (k = 0; k < c->nvoices; k++) {
        if ((ch < 0 || k == ch) && c->chan[k].snd && !c->chan[k].paused) {
            c->chan[k].paused = 1;
            c->be.freq(c->be.ctx, k, 0);
        }
    }
}

void mc_resume(struct mc_core *c, int ch)
{
    int k;
    for (k = 0; k < c->nvoices; k++) {
        if ((ch < 0 || k == ch) && c->chan[k].snd && c->chan[k].paused) {
            c->chan[k].paused = 0;
            c->be.freq(c->be.ctx, k, c->chan[k].snd->rate);
        }
    }
}

int mc_paused(const struct mc_core *c, int ch)
{
    int k, n = 0;
    if (ch >= 0) {
        return ch < c->nvoices && c->chan[ch].snd && c->chan[ch].paused;
    }
    for (k = 0; k < c->nvoices; k++) {
        n += c->chan[k].snd && c->chan[k].paused;
    }
    return n;
}

int mc_playing(const struct mc_core *c, int ch)
{
    int k, n = 0;
    if (ch >= 0) {
        return ch < c->nvoices && c->chan[ch].snd != NULL;
    }
    for (k = 0; k < c->nvoices; k++) {
        n += c->chan[k].snd != NULL;
    }
    return n;
}

int mc_fade_out(struct mc_core *c, int ch, int ms)
{
    int k, n = 0;
    for (k = 0; k < c->nvoices; k++) {
        struct mc_chan *h = &c->chan[k];
        if ((ch < 0 || k == ch) && h->snd && h->fade != MC_FADING_OUT) {
            uint32_t t = mc_ticks(ms);
            if (h->fade == MC_FADING_IN && h->fade_total) {
                /* from where the fade in has got to */
                uint32_t done = h->fade_total - h->fade_left;
                h->fade_left = (uint32_t)(((uint64_t)done * t) / h->fade_total);
                if (!h->fade_left) {
                    h->fade_left = 1;
                }
            } else {
                h->fade_left = t;
            }
            h->fade = MC_FADING_OUT;
            h->fade_total = t;
            chan_vol(c, k);
            n++;
        }
    }
    return n;
}

int mc_fading(const struct mc_core *c, int ch)
{
    if (ch < 0 || ch >= c->nvoices || !c->chan[ch].snd) {
        return MC_NO_FADING;
    }
    return c->chan[ch].fade;
}

int mc_expire(struct mc_core *c, int ch, int ms)
{
    int k, n = 0;
    for (k = 0; k < c->nvoices; k++) {
        if ((ch < 0 || k == ch) && c->chan[k].snd) {
            c->chan[k].expire = ms > 0 ? mc_ticks(ms) : 0;
            n++;
        }
    }
    return n;
}

int mc_volume(struct mc_core *c, int ch, int volume)
{
    int k, old;
    if (ch >= MC_MAX_CHANS) {
        return 0;
    }
    if (ch < 0) {
        /* SDL_mixer: the average of them all */
        int sum = 0;
        for (k = 0; k < c->nchans; k++) {
            sum += c->chan[k].volume;
        }
        old = c->nchans ? sum / c->nchans : 128;
    } else {
        old = c->chan[ch].volume;
    }
    if (volume < 0) {
        return old;
    }
    if (volume > 128) {
        volume = 128;
    }
    for (k = 0; k < MC_MAX_CHANS; k++) {
        if (ch < 0 || k == ch) {
            c->chan[k].volume = volume;
            if (k < c->nvoices) {
                chan_vol(c, k);
            }
        }
    }
    return old;
}

int mc_panning(struct mc_core *c, int ch, int left, int right)
{
    int k;
    if (ch >= MC_MAX_CHANS) {
        return 0;
    }
    for (k = 0; k < MC_MAX_CHANS; k++) {
        if (ch < 0 || k == ch) {
            c->chan[k].left = left < 0 ? 0 : left > 255 ? 255 : left;
            c->chan[k].right = right < 0 ? 0 : right > 255 ? 255 : right;
            if (k < c->nvoices) {
                chan_vol(c, k);
            }
        }
    }
    return 1;
}

int mc_allocate(struct mc_core *c, int n)
{
    int k;
    if (n < 0) {
        return c->nchans;
    }
    if (n > MC_MAX_CHANS) {
        n = MC_MAX_CHANS;
    }
    for (k = n; k < c->nvoices; k++) {
        if (c->chan[k].snd) {
            voice_silence(c, k);
            chan_free(c, k, 1);
        }
    }
    c->nchans = n;
    if (c->reserved > n) {
        c->reserved = n;
    }
    return n;
}

int mc_reserve(struct mc_core *c, int n)
{
    if (n < 0) {
        n = 0;
    }
    if (n > c->nchans) {
        n = c->nchans;
    }
    c->reserved = n;
    return n;
}

const mc_sound *mc_sound_of(const struct mc_core *c, int ch)
{
    if (ch < 0 || ch >= c->nvoices) {
        return NULL;
    }
    return c->chan[ch].snd;
}

void mc_sound_changed(struct mc_core *c, const mc_sound *s)
{
    int k;
    for (k = 0; k < c->nvoices; k++) {
        if (c->chan[k].snd == s) {
            chan_vol(c, k);
        }
    }
}

void mc_forget(struct mc_core *c, const mc_sound *s)
{
    int k;
    for (k = 0; k < c->nvoices; k++) {
        if (c->chan[k].snd == s) {
            voice_silence(c, k);
            chan_free(c, k, 1);
        }
    }
}

/* ---- The music voice ----------------------------------------------------------------- */

static int mvoice(const struct mc_core *c)
{
    return c->nvoices;
}

static void music_vol(struct mc_core *c)
{
    struct mc_music *m = &c->mus;
    int32_t g = ((int32_t)m->volume * 128 * fade_level(m->fade, m->fade_total, m->fade_left)) >> 8;
    c->be.vol(c->be.ctx, mvoice(c), g, g, m->channels == 2);
}

static unsigned seg_count(const struct mc_music *m)
{
    return m->head - m->tail;
}

static void seg_release(struct mc_music *m, unsigned i)
{
    struct mc_seg *s = &m->seg[i % MC_SEGS];
    if (s->buf >= 0) {
        m->freed |= 1u << s->buf;
    }
    s->data = NULL;
    s->frames = 0;
    s->buf = -1;
}

static void music_queue(struct mc_core *c, unsigned i)
{
    struct mc_seg *s = &c->mus.seg[i % MC_SEGS];
    c->be.sound(c->be.ctx, mvoice(c), s->data, s->frames, c->mus.channels, 0);
}

static void music_stop(struct mc_core *c)
{
    struct mc_music *m = &c->mus;
    while (seg_count(m)) {
        seg_release(m, m->tail++);
    }
    m->playing = m->queued = m->nosound = 0;
    m->active = 0;
    m->paused = 0;
    m->fade = MC_NO_FADING;
    m->eos = 0;
}

void mc_music_start(struct mc_core *c, uint32_t rate, int channels, int fade_in_ms)
{
    struct mc_music *m = &c->mus;
    if (m->active) {
        voice_silence(c, mvoice(c));
        music_stop(c);
    }
    m->active = 1;
    m->rate = rate;
    m->channels = channels == 2 ? 2 : 1;
    m->head = m->tail = 0;
    if (fade_in_ms > 0) {
        m->fade = MC_FADING_IN;
        m->fade_total = m->fade_left = mc_ticks(fade_in_ms);
    } else {
        m->fade = MC_NO_FADING;
        m->fade_total = m->fade_left = 0;
    }
    music_vol(c);
    c->be.freq(c->be.ctx, mvoice(c), rate);
}

int mc_music_room(const struct mc_core *c)
{
    return c->mus.active && !c->mus.eos && seg_count(&c->mus) < MC_SEGS;
}

void mc_music_put(struct mc_core *c, const int16_t *data, uint32_t frames, int buf)
{
    struct mc_music *m = &c->mus;
    struct mc_seg *s;
    if (!m->active || m->eos || seg_count(m) >= MC_SEGS || !frames) {
        if (buf >= 0) {
            m->freed |= 1u << buf;
        }
        return;
    }
    s = &m->seg[m->head % MC_SEGS];
    s->data = data;
    s->frames = frames;
    s->buf = buf;
    m->head++;
    if (!m->playing) {
        /* the first piece, or the voice ran dry: start it now */
        c->be.sound(c->be.ctx, mvoice(c), data, frames, m->channels, 1);
        m->playing = 1;
        m->queued = 0;
        m->nosound = 0;
        if (m->paused) {
            c->be.freq(c->be.ctx, mvoice(c), 0);
        }
        /* nothing behind it yet: AHI_NOSOUND, until the next piece */
        c->be.sound(c->be.ctx, mvoice(c), NULL, 0, 0, 0);
        m->nosound = 1;
    } else if (!m->queued) {
        music_queue(c, m->tail + 1);
        m->queued = 1;
        m->nosound = 0;
    }
}

void mc_music_end(struct mc_core *c)
{
    struct mc_music *m = &c->mus;
    if (!m->active) {
        return;
    }
    m->eos = 1;
    if (!m->playing && !seg_count(m)) {
        music_stop(c);
        c->music_finished = 1;
    }
}

void mc_music_halt(struct mc_core *c)
{
    if (c->mus.active) {
        voice_silence(c, mvoice(c));
        music_stop(c);
    }
}

void mc_music_pause(struct mc_core *c, int paused)
{
    struct mc_music *m = &c->mus;
    if (!m->active || m->paused == !!paused) {
        return;
    }
    m->paused = !!paused;
    c->be.freq(c->be.ctx, mvoice(c), m->paused ? 0 : m->rate);
}

int mc_music_volume(struct mc_core *c, int volume)
{
    int old = c->mus.volume;
    if (volume < 0) {
        return old;
    }
    c->mus.volume = volume > 128 ? 128 : volume;
    if (c->mus.active) {
        music_vol(c);
    }
    return old;
}

int mc_music_fade_out(struct mc_core *c, int ms)
{
    struct mc_music *m = &c->mus;
    if (!m->active) {
        return 0;
    }
    if (ms <= 0) {
        mc_music_halt(c);
        return 1;
    }
    if (m->fade != MC_FADING_OUT) {
        m->fade = MC_FADING_OUT;
        m->fade_total = m->fade_left = mc_ticks(ms);
    }
    return 1;
}

uint32_t mc_take_freed(struct mc_core *c)
{
    uint32_t f = c->mus.freed;
    c->mus.freed = 0;
    return f;
}

static void music_event(struct mc_core *c)
{
    struct mc_music *m = &c->mus;
    if (!m->active || !m->playing) {
        return;
    }
    if (m->nosound) {
        /* AHI_NOSOUND started: the voice ran dry, or the music has ended */
        seg_release(m, m->tail++);
        m->playing = m->queued = m->nosound = 0;
        if (m->eos && !seg_count(m)) {
            music_stop(c);
            c->music_finished = 1;
        } else if (seg_count(m)) {
            /* a piece came in after AHI_NOSOUND was queued: on with it */
            struct mc_seg *s = &m->seg[m->tail % MC_SEGS];
            c->be.sound(c->be.ctx, mvoice(c), s->data, s->frames, m->channels, 1);
            m->playing = 1;
            if (seg_count(m) > 1) {
                music_queue(c, m->tail + 1);
                m->queued = 1;
            } else {
                c->be.sound(c->be.ctx, mvoice(c), NULL, 0, 0, 0);
                m->nosound = 1;
            }
        }
        return;
    }
    if (m->queued) {
        /* the piece queued has started: the one before it is done */
        seg_release(m, m->tail++);
        m->queued = 0;
        if (seg_count(m) > 1) {
            music_queue(c, m->tail + 1);
            m->queued = 1;
        } else {
            c->be.sound(c->be.ctx, mvoice(c), NULL, 0, 0, 0);
            m->nosound = 1;
        }
    }
}

/* ---- Events and ticks ------------------------------------------------------------------- */

void mc_sound_event(struct mc_core *c, int v)
{
    struct mc_chan *h;
    if (v == mvoice(c)) {
        music_event(c);
        return;
    }
    if (v < 0 || v >= c->nvoices) {
        return;
    }
    h = &c->chan[v];
    if (!h->snd) {
        return;
    }
    if (h->ending) {
        chan_free(c, v, 1);           /* AHI_NOSOUND started: done */
        return;
    }
    /* another play of the sound started */
    if (h->loops > 0) {
        h->loops--;
    }
    chan_queue_next(c, v);
}

void mc_tick(struct mc_core *c)
{
    int k;
    struct mc_music *m = &c->mus;
    for (k = 0; k < c->nvoices; k++) {
        struct mc_chan *h = &c->chan[k];
        if (!h->snd || h->paused) {
            continue;
        }
        if (h->expire) {
            if (--h->expire == 0) {
                voice_silence(c, k);
                chan_free(c, k, 1);
                continue;
            }
        }
        if (h->fade != MC_NO_FADING) {
            if (h->fade_left) {
                h->fade_left--;
            }
            if (!h->fade_left) {
                if (h->fade == MC_FADING_OUT) {
                    voice_silence(c, k);
                    chan_free(c, k, 1);
                    continue;
                }
                h->fade = MC_NO_FADING;
            }
            chan_vol(c, k);
        }
    }
    if (m->active && !m->paused && m->fade != MC_NO_FADING) {
        if (m->fade_left) {
            m->fade_left--;
        }
        if (!m->fade_left) {
            if (m->fade == MC_FADING_OUT) {
                mc_music_halt(c);
                c->music_finished = 1;
                return;
            }
            m->fade = MC_NO_FADING;
        }
        music_vol(c);
    }
}

uint32_t mc_take_finished(struct mc_core *c)
{
    uint32_t f = c->finished;
    c->finished = 0;
    return f;
}

void mc_volpan(int32_t gl, int32_t gr, int stereo, int hostmix, int32_t *vol, int32_t *pan)
{
    int32_t sum = gl + gr;
    int32_t v;
    if (gl < 0) {
        gl = 0;
    }
    if (gr < 0) {
        gr = 0;
    }
    sum = gl + gr;
    *pan = sum ? (int32_t)(((int64_t)gr << 16) / sum) : 0x8000;
    /* Q14 to 16.16 is times 4 */
    if (hostmix) {
        v = stereo ? sum * 2 : sum * 4;
    } else {
        v = stereo ? sum : sum * 2;
    }
    if (v > (hostmix ? 0x20000 : 0x10000)) {
        v = hostmix ? 0x20000 : 0x10000;
    }
    *vol = v;
}
