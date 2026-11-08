/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ogg2iff: Ogg Vorbis to AIFF, IFF 8SVX, WAV or raw PCM, so a port can ship
 * its music decoded instead of decoding Vorbis on a 68040.
 *
 *     ogg2iff [options] IN.ogg|DIR ...
 *
 * Decoding is stb_vorbis (public domain / MIT); build.sh fetches
 * stb_vorbis.c at a pinned commit and checks its sha256. Everything streams: one decoded block, the resampler's window and
 * one output block are in memory, never the whole file. --normalize and
 * --trim-silence decode the file twice (once to measure, once to write).
 * Built for the PC and, with m68k-amigaos-gcc -m68040 -noixemul, for the
 * Amiga (see build.sh and README.md).
 */
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#include "stb_vorbis.c"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define OGG2IFF_VERSION "1.0"

#if defined(__amigaos__) || defined(AMIGA)
/* libnix: a larger stack than the Shell's 4 KB default; stb_vorbis is given
 * its own heap buffer (no alloca), so this is only headroom. */
unsigned long __stack = 65536;
static const char verstag[] __attribute__((used)) =
    "$VER: ogg2iff " OGG2IFF_VERSION " (07.11.2026) stb_vorbis 1.22";
#endif

enum { F_AIFF, F_8SVX, F_WAV, F_RAW };
static const char *const fmt_name[] = { "aiff", "8svx", "wav", "raw" };
static const char *const fmt_ext[] = { ".aiff", ".8svx", ".wav", ".raw" };

typedef struct {
    int fmt;
    long rate;          /* 0: keep the source rate */
    int mono;
    int bits;           /* 0: the format's default */
    int big_endian;     /* raw only; default big, the 68k's order */
    int unsigned8;      /* raw 8-bit only */
    int normalize;
    double norm_db;
    double gain_db;
    double start, end, length;  /* seconds; end/length < 0: to the end */
    int trim_silence;
    double silence_db;
    int quality;        /* 0, 1, 2: resampler length */
    int dither;
    int info;
    int verbose;
    const char *out;
} Opts;

#define BLOCK 4096
#define MAXCH 8

static void die_oom(void)
{
    fprintf(stderr, "ogg2iff: out of memory\n");
    exit(20);
}

static void *xcalloc(size_t n, size_t s)
{
    void *p = calloc(n ? n : 1, s ? s : 1);
    if (!p) die_oom();
    return p;
}

/* --- byte output, independent of the host's byte order --------------- */

static void put16le(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void put32le(unsigned char *p, unsigned long v)
{ p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16be(unsigned char *p, unsigned v) { p[0] = v >> 8; p[1] = v; }
static void put32be(unsigned char *p, unsigned long v)
{ p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

/* An integer sample rate as the 80-bit IEEE extended AIFF's COMM wants. */
static void put_ext80(unsigned char *p, unsigned long rate)
{
    int msb = 31;
    memset(p, 0, 10);
    if (!rate) return;
    while (!(rate & (1UL << msb))) msb--;
    put16be(p, 16383 + msb);
    /* mantissa: the rate shifted so its top bit is bit 63 */
    put32be(p + 2, (rate << (31 - msb)) & 0xffffffffUL);
}

/* --- writer ---------------------------------------------------------- */

typedef struct {
    const Opts *o;
    FILE *f, *right;    /* right: 8SVX stereo's second channel, appended at the end */
    char *path, *right_path;
    int fmt, ch, bits;
    long rate;
    unsigned long frames;
    unsigned long clipped;
    unsigned long rng;
    unsigned char *buf;
    long hdr;           /* bytes before the sample data */
} Writer;

static unsigned long rng_next(unsigned long *s)
{
    unsigned long x = *s;   /* xorshift32 */
    x ^= (x << 13) & 0xffffffffUL;
    x ^= x >> 17;
    x ^= (x << 5) & 0xffffffffUL;
    return *s = x & 0xffffffffUL;
}

/* TPDF: the difference of two uniform values, -1..+1 LSB, triangular. */
static float tpdf(unsigned long *s)
{
    float a = (float)(rng_next(s) >> 8) * (1.0f / 16777216.0f);
    float b = (float)(rng_next(s) >> 8) * (1.0f / 16777216.0f);
    return a - b;
}

static int write_header(Writer *w, int final)
{
    unsigned char h[128];
    unsigned long frames = w->frames;
    unsigned long data = frames * w->ch * (w->bits / 8);
    unsigned long pad = data & 1;
    long n = 0;

    if (w->fmt == F_RAW) return 0;  /* no header, so no seek: a pipe or NIL: works */
    memset(h, 0, sizeof h);
    switch (w->fmt) {
    case F_WAV:
        memcpy(h, "RIFF", 4); put32le(h + 4, 36 + data + pad);
        memcpy(h + 8, "WAVEfmt ", 8); put32le(h + 16, 16);
        put16le(h + 20, 1); put16le(h + 22, w->ch);
        put32le(h + 24, w->rate); put32le(h + 28, w->rate * w->ch * (w->bits / 8));
        put16le(h + 32, w->ch * (w->bits / 8)); put16le(h + 34, w->bits);
        memcpy(h + 36, "data", 4); put32le(h + 40, data);
        n = 44;
        break;
    case F_AIFF:
        memcpy(h, "FORM", 4); put32be(h + 4, 4 + 26 + 16 + data + pad);
        memcpy(h + 8, "AIFFCOMM", 8); put32be(h + 16, 18);
        put16be(h + 20, w->ch); put32be(h + 22, frames);
        put16be(h + 26, w->bits); put_ext80(h + 28, w->rate);
        memcpy(h + 38, "SSND", 4); put32be(h + 42, 8 + data);
        put32be(h + 46, 0); put32be(h + 50, 0);
        n = 54;
        break;
    case F_8SVX: {
        static const char anno[] = "ogg2iff " OGG2IFF_VERSION; /* 11 bytes, padded to 12 */
        long body;
        memcpy(h, "FORM", 4);
        memcpy(h + 8, "8SVXVHDR", 8); put32be(h + 16, 20);
        put32be(h + 20, frames);    /* oneShotHiSamples, per channel */
        put32be(h + 24, 0);         /* repeatHiSamples */
        put32be(h + 28, 0);         /* samplesPerHiCycle */
        put16be(h + 32, w->rate > 65535 ? 65535 : (unsigned)w->rate);
        h[34] = 1;                  /* ctOctave */
        h[35] = 0;                  /* sCompression: none */
        put32be(h + 36, 0x10000UL); /* volume: Unity */
        n = 40;
        memcpy(h + n, "ANNO", 4); put32be(h + n + 4, sizeof anno - 1);
        memcpy(h + n + 8, anno, sizeof anno - 1);
        n += 8 + ((sizeof anno - 1 + 1) & ~1);
        if (w->ch == 2) {
            memcpy(h + n, "CHAN", 4); put32be(h + n + 4, 4);
            put32be(h + n + 8, 6);  /* RIGHT|LEFT: stereo */
            n += 12;
        }
        memcpy(h + n, "BODY", 4); put32be(h + n + 4, data);
        n += 8;
        body = n;
        put32be(h + 4, body - 8 + data + pad);
        break;
    }
    case F_RAW:
        n = 0;
        break;
    }
    if (final && fseek(w->f, 0, SEEK_SET)) return -1;
    if (n && fwrite(h, 1, n, w->f) != (size_t)n) return -1;
    w->hdr = n;
    return 0;
}

static int writer_open(Writer *w, const Opts *o, const char *path, int ch, long rate)
{
    memset(w, 0, sizeof *w);
    w->o = o;
    w->fmt = o->fmt;
    w->ch = ch;
    w->rate = rate;
    w->bits = o->bits ? o->bits : (o->fmt == F_8SVX ? 8 : 16);
    w->rng = 0x2545f491UL;
    w->path = strdup(path);
    w->f = fopen(path, "wb");
    if (!w->path || !w->f) {
        fprintf(stderr, "ogg2iff: can't write %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (w->fmt == F_8SVX && ch == 2) {
        size_t l = strlen(path);
        w->right_path = xcalloc(l + 8, 1);
        memcpy(w->right_path, path, l);
        strcpy(w->right_path + l, ".right");
        w->right = fopen(w->right_path, "w+b");
        if (!w->right) {
            fprintf(stderr, "ogg2iff: can't write %s: %s\n", w->right_path, strerror(errno));
            return -1;
        }
    }
    w->buf = xcalloc((size_t)BLOCK * ch * 2, 1);
    return write_header(w, 0);
}

static int quant(Writer *w, float x, int bits)
{
    float s;
    int v, lo, hi;
    if (bits == 8) {
        s = x * 128.0f;
        if (w->o->dither) s += tpdf(&w->rng);
        lo = -128; hi = 127;
    } else {
        s = x * 32768.0f;
        lo = -32768; hi = 32767;
    }
    s = (float)floor(s + 0.5f);
    if (s < lo) { w->clipped++; return lo; }
    if (s > hi) { w->clipped++; return hi; }
    v = (int)s;
    return v;
}

/* n frames, one array per channel, at most BLOCK */
static int writer_put(Writer *w, float *const *in, int n)
{
    unsigned char *p = w->buf;
    int i, c;
    int be = w->fmt == F_AIFF || w->fmt == F_8SVX || (w->fmt == F_RAW && w->o->big_endian);
    int u8 = w->fmt == F_WAV || (w->fmt == F_RAW && w->o->unsigned8);

    if (w->fmt == F_8SVX && w->ch == 2) {
        /* 8SVX stereo: all of the left channel, then all of the right */
        for (c = 0; c < 2; c++) {
            for (i = 0; i < n; i++) p[i] = (unsigned char)(signed char)quant(w, in[c][i], 8);
            if (fwrite(p, 1, n, c ? w->right : w->f) != (size_t)n) return -1;
        }
        w->frames += n;
        return 0;
    }
    for (i = 0; i < n; i++)
        for (c = 0; c < w->ch; c++) {
            int v = quant(w, in[c][i], w->bits);
            if (w->bits == 8) {
                *p++ = u8 ? (unsigned char)(v + 128) : (unsigned char)(signed char)v;
            } else if (be) {
                put16be(p, (unsigned)v & 0xffff); p += 2;
            } else {
                put16le(p, (unsigned)v & 0xffff); p += 2;
            }
        }
    w->frames += n;
    return fwrite(w->buf, 1, p - w->buf, w->f) == (size_t)(p - w->buf) ? 0 : -1;
}

static int writer_close(Writer *w, int ok)
{
    int r = ok ? 0 : -1;
    unsigned long data = w->frames * w->ch * (w->bits / 8);

    if (r == 0 && w->right) {
        size_t got;
        if (fseek(w->right, 0, SEEK_SET)) r = -1;
        while (r == 0 && (got = fread(w->buf, 1, (size_t)BLOCK * 2, w->right)) > 0)
            if (fwrite(w->buf, 1, got, w->f) != got) r = -1;
    }
    if (r == 0 && (data & 1) && w->fmt != F_RAW) {
        if (fputc(0, w->f) == EOF) r = -1;  /* IFF/RIFF chunks are even-sized */
    }
    if (r == 0) r = write_header(w, 1);
    if (w->right) { fclose(w->right); remove(w->right_path); }
    if (w->f && fclose(w->f)) r = -1;
    if (r) {
        fprintf(stderr, "ogg2iff: error writing %s\n", w->path);
        remove(w->path);
    }
    free(w->right_path);
    free(w->path);
    free(w->buf);
    return r;
}

/* --- resampler ------------------------------------------------------- */
/* A polyphase windowed sinc (Kaiser), any ratio. Output sample k sits at
 * input time k*in/out; its 2H taps come from a table of the kernel at P
 * points per input sample, interpolated linearly between phases. The
 * cut-off is set so the stopband starts at the lower Nyquist frequency. */

typedef struct {
    int ch, H, P;
    long in_rate, out_rate;     /* reduced by their gcd */
    float *tbl;                 /* 2*H*P + 1 kernel values */
    float *coef;                /* 2*H, this output's taps */
    float *bank;                /* out_rate sets of 2*H taps, when few phases */
    float *buf[MAXCH];          /* input, with H zeros before sample 0 */
    long cap, fill, pos, frac;
    unsigned long n_in, n_out;
    float *obuf[MAXCH];
    int on;
} Resampler;

static double bessel_i0(double x)
{
    double s = 1, t = 1, q = x * x / 4;
    int k;
    for (k = 1; k < 64; k++) {
        t *= q / ((double)k * k);
        s += t;
        if (t < s * 1e-12) break;
    }
    return s;
}

static long gcd(long a, long b)
{
    while (b) { long t = a % b; a = b; b = t; }
    return a;
}

/* The taps for the current phase (frac), summing to exactly 1 */
static void rs_taps(const Resampler *r, float *coef)
{
    int H = r->H, P = r->P, k;
    float fp = (float)r->frac / r->out_rate * P, a, sum = 0;
    int ip = (int)fp;
    a = fp - ip;
    for (k = 0; k < 2 * H; k++) {
        int b = (k + 1) * P - ip;
        float v = r->tbl[b] * (1 - a) + r->tbl[b - 1] * a;
        coef[k] = v;
        sum += v;
    }
    sum = sum != 0 ? 1 / sum : 1;
    for (k = 0; k < 2 * H; k++) coef[k] *= sum;
}

static void rs_init(Resampler *r, int ch, long in, long out, int quality)
{
    static const double Z[] = { 8, 16, 32 }, beta[] = { 6, 8.6, 10 };
    double ratio = (double)out / in, A, cut, k;
    long g = gcd(in, out), i;
    int c;

    memset(r, 0, sizeof *r);
    r->ch = ch;
    r->in_rate = in / g;
    r->out_rate = out / g;
    r->P = 128;
    A = beta[quality] / 0.1102 + 8.7;   /* stopband attenuation, dB */
    /* Kaiser: transition width (A-8)/(2.285 N) rad, N = 2Z/cut taps */
    k = (A - 8) / (2.285 * 4 * Z[quality] * M_PI);
    cut = (ratio < 1 ? ratio : 1) / (1 + k);
    r->H = (int)ceil(Z[quality] / cut);
    r->tbl = xcalloc((size_t)2 * r->H * r->P + 1, sizeof(float));
    for (i = 0; i <= 2L * r->H * r->P; i++) {
        double t = -r->H + (double)i / r->P, x = t / r->H, s, w;
        s = t == 0 ? 1 : sin(M_PI * cut * t) / (M_PI * cut * t);
        w = fabs(x) >= 1 ? 0 : bessel_i0(beta[quality] * sqrt(1 - x * x)) / bessel_i0(beta[quality]);
        r->tbl[i] = (float)(cut * s * w);
    }
    r->coef = xcalloc(2 * r->H, sizeof(float));
    /* Few phases (44100 to 22050 or 11025 has one): make each phase's taps
     * once instead of for every output sample. */
    if (r->out_rate * 2 * r->H <= 65536) {
        long ph;
        r->bank = xcalloc((size_t)r->out_rate * 2 * r->H, sizeof(float));
        for (ph = 0; ph < r->out_rate; ph++) {
            r->frac = ph;
            rs_taps(r, r->bank + ph * 2 * r->H);
        }
        r->frac = 0;
    }
    r->cap = BLOCK + 4L * r->H;
    for (c = 0; c < ch; c++) {
        r->buf[c] = xcalloc(r->cap, sizeof(float));
        r->obuf[c] = xcalloc(BLOCK, sizeof(float));
    }
    r->fill = r->H;     /* H zeros: sample 0 is at buf index H */
    r->pos = r->H;
}

static void rs_free(Resampler *r)
{
    int c;
    free(r->bank);
    free(r->tbl);
    free(r->coef);
    for (c = 0; c < r->ch; c++) { free(r->buf[c]); free(r->obuf[c]); }
}

/* Make outputs from what is buffered; limit: stop after this many outputs
 * in all (flushing), or 0 for no limit. */
static int rs_run(Resampler *r, Writer *w, unsigned long limit)
{
    int H = r->H, c, k;
    while (r->pos + H < r->fill && (!limit || r->n_out < limit)) {
        const float *coef;
        if (r->bank) {
            coef = r->bank + r->frac * 2 * H;
        } else {
            rs_taps(r, r->coef);
            coef = r->coef;
        }
        for (c = 0; c < r->ch; c++) {
            const float *x = r->buf[c] + r->pos - H + 1;
            float acc = 0;
            for (k = 0; k < 2 * H; k++) acc += x[k] * coef[k];
            r->obuf[c][r->on] = acc;
        }
        r->n_out++;
        if (++r->on == BLOCK) {
            if (writer_put(w, r->obuf, r->on)) return -1;
            r->on = 0;
        }
        r->frac += r->in_rate;
        r->pos += r->frac / r->out_rate;
        r->frac %= r->out_rate;
    }
    /* keep only what later outputs still need */
    {
        long drop = r->pos - H + 1;
        if (drop > r->fill) drop = r->fill;
        if (drop > 0) {
            for (c = 0; c < r->ch; c++)
                memmove(r->buf[c], r->buf[c] + drop, (r->fill - drop) * sizeof(float));
            r->fill -= drop;
            r->pos -= drop;
        }
    }
    return 0;
}

static int rs_put(Resampler *r, Writer *w, float *const *in, int n)
{
    int c, done = 0;
    while (done < n) {
        int m = n - done;
        if (m > r->cap - r->fill) m = (int)(r->cap - r->fill);
        for (c = 0; c < r->ch; c++)
            memcpy(r->buf[c] + r->fill, in[c] + done, m * sizeof(float));
        r->fill += m;
        r->n_in += m;
        done += m;
        if (rs_run(r, w, 0)) return -1;
    }
    return 0;
}

static int rs_flush(Resampler *r, Writer *w)
{
    /* outputs whose time falls inside the input: ceil(n_in * out / in) */
    unsigned long target = (unsigned long)(((double)r->n_in * r->out_rate + r->in_rate - 1) / r->in_rate);
    int c;
    while (r->n_out < target) {
        long m = r->cap - r->fill;
        for (c = 0; c < r->ch; c++) memset(r->buf[c] + r->fill, 0, m * sizeof(float));
        r->fill += m;
        if (rs_run(r, w, target)) return -1;
    }
    if (r->on && writer_put(w, r->obuf, r->on)) return -1;
    r->on = 0;
    return 0;
}

/* --- downmix --------------------------------------------------------- */
/* Vorbis channel orders (spec 4.3.9): L/R/C weights per source channel. */

static void make_mix(float mix[2][MAXCH], int src, int out)
{
    static const char *const order[] = {
        "C", "LR", "LCR", "LRLR", "LCRLR", "LCRLRF", "LCRLRCF", "LCRLRLRF"
    };
    float wl[MAXCH], wr[MAXCH], sl = 0, sr = 0;
    int i;
    for (i = 0; i < src; i++) {
        char p = order[src - 1][i];
        wl[i] = p == 'L' ? 1 : p == 'C' ? 0.7071f : 0;
        wr[i] = p == 'R' ? 1 : p == 'C' ? 0.7071f : 0;
        if (src == 1) wl[i] = wr[i] = 1;
        sl += wl[i];
        sr += wr[i];
    }
    for (i = 0; i < src; i++) {
        if (out == 1) {
            mix[0][i] = (wl[i] / sl + wr[i] / sr) * 0.5f;
        } else {
            mix[0][i] = wl[i] / sl;
            mix[1][i] = wr[i] / sr;
        }
    }
    if (src == 2 && out == 2) {
        mix[0][0] = 1; mix[0][1] = 0; mix[1][0] = 0; mix[1][1] = 1;
    }
}

/* --- one file -------------------------------------------------------- */

static stb_vorbis *open_vorbis(const char *path, stb_vorbis_alloc *al)
{
    int err = 0, size;
    stb_vorbis *v;
    /* stb_vorbis takes all its memory from one buffer (no malloc/alloca
     * per frame); grow it until the stream's setup fits. */
    for (size = 256 * 1024; size <= 16 * 1024 * 1024; size *= 2) {
        al->alloc_buffer = malloc(size);
        al->alloc_buffer_length_in_bytes = size;
        if (!al->alloc_buffer) die_oom();
        v = stb_vorbis_open_filename(path, &err, al);
        if (v) return v;
        free(al->alloc_buffer);
        al->alloc_buffer = NULL;
        if (err != VORBIS_outofmem) break;
    }
    fprintf(stderr, "ogg2iff: %s: not an Ogg Vorbis file I can read (stb_vorbis error %d)\n", path, err);
    return NULL;
}

static unsigned long secs_to_frames(double s, long rate)
{
    return s <= 0 ? 0 : (unsigned long)(s * rate + 0.5);
}

static float db_to_lin(double db) { return (float)pow(10.0, db / 20.0); }

static int convert(const Opts *o, const char *in, const char *outpath)
{
    stb_vorbis_alloc al = { 0 };
    stb_vorbis *v;
    stb_vorbis_info vi;
    unsigned long total, first, last, pos;
    float mix[2][MAXCH];
    float *dec[MAXCH], *mixed[2];
    int sch, och, c, i, n, r = 0, pct = -1;
    long orate;
    float gain = db_to_lin(o->gain_db);
    Writer w;
    Resampler rs;
    int resample;

    if (!(v = open_vorbis(in, &al))) return -1;
    vi = stb_vorbis_get_info(v);
    sch = vi.channels;
    if (sch < 1 || sch > MAXCH) {
        fprintf(stderr, "ogg2iff: %s: %d channels, I take 1 to %d\n", in, sch, MAXCH);
        stb_vorbis_close(v); free(al.alloc_buffer);
        return -1;
    }
    total = stb_vorbis_stream_length_in_samples(v);
    och = (o->mono || sch == 1) ? 1 : 2;
    orate = o->rate ? o->rate : (long)vi.sample_rate;
    resample = orate != (long)vi.sample_rate;

    if (o->info) {
        stb_vorbis_comment cm = stb_vorbis_get_comment(v);
        printf("%s: Ogg Vorbis, %d channel%s, %u Hz, %lu frames (%.2f s)\n",
               in, sch, sch == 1 ? "" : "s", vi.sample_rate, total,
               vi.sample_rate ? (double)total / vi.sample_rate : 0.0);
        printf("  decoder memory: %u setup + %u temp bytes, max frame %d\n",
               vi.setup_memory_required, vi.temp_memory_required, vi.max_frame_size);
        if (cm.vendor) printf("  vendor: %s\n", cm.vendor);
        for (i = 0; i < cm.comment_list_length; i++) printf("  %s\n", cm.comment_list[i]);
        stb_vorbis_close(v); free(al.alloc_buffer);
        return 0;
    }

    first = secs_to_frames(o->start, vi.sample_rate);
    last = total ? total : (unsigned long)-1;  /* length unknown: to the end */
    if (o->end > 0) last = secs_to_frames(o->end, vi.sample_rate);
    if (o->length > 0) last = first + secs_to_frames(o->length, vi.sample_rate);
    if (last > total && total) last = total;
    if (first > last) first = last;

    make_mix(mix, sch, och);
    for (c = 0; c < sch; c++) dec[c] = NULL;
    for (c = 0; c < och; c++) mixed[c] = xcalloc(BLOCK, sizeof(float));

    /* pass 1: peak and the first and last loud frames, after the mix */
    if (o->normalize || o->trim_silence) {
        float peak = 0, thr = db_to_lin(o->silence_db);
        unsigned long lo = last, hi = first;
        pos = 0;
        {
            float *b[MAXCH];
            for (c = 0; c < sch; c++) b[c] = xcalloc(BLOCK, sizeof(float));
            while (pos < last && (n = stb_vorbis_get_samples_float(v, sch, b, BLOCK)) > 0) {
                for (i = 0; i < n; i++, pos++) {
                    float loud = 0;
                    if (pos < first || pos >= last) continue;
                    for (c = 0; c < och; c++) {
                        int s;
                        float x = 0;
                        for (s = 0; s < sch; s++) x += mix[c][s] * b[s][i];
                        x = fabsf(x * gain);
                        if (x > loud) loud = x;
                    }
                    if (loud > peak) peak = loud;
                    if (loud > thr) {
                        if (pos < lo) lo = pos;
                        hi = pos + 1;
                    }
                }
            }
            for (c = 0; c < sch; c++) free(b[c]);
        }
        if (o->trim_silence) {
            if (lo >= hi) lo = hi = first;   /* all silent */
            first = lo;
            last = hi;
        }
        if (o->normalize && peak > 0) {
            gain *= db_to_lin(o->norm_db) / peak;
            if (o->verbose)
                fprintf(stderr, "ogg2iff: %s: peak %.2f dBFS, gain %+.2f dB\n", in,
                        20 * log10(peak), 20 * log10(gain));
        }
        stb_vorbis_seek_start(v);
    }

    if (writer_open(&w, o, outpath, och, orate)) {
        stb_vorbis_close(v); free(al.alloc_buffer);
        for (c = 0; c < och; c++) free(mixed[c]);
        if (w.f) writer_close(&w, 0);
        return -1;
    }
    if (resample) rs_init(&rs, och, vi.sample_rate, orate, o->quality);
    if (o->verbose)
        fprintf(stderr, "ogg2iff: %s -> %s: %s, %d-bit, %d ch, %ld Hz%s\n", in, outpath,
                fmt_name[o->fmt], w.bits, och, orate, resample ? " (resampled)" : "");

    for (c = 0; c < sch; c++) dec[c] = xcalloc(BLOCK, sizeof(float));
    pos = 0;
    if (first > 0) {
        if (stb_vorbis_seek(v, (unsigned)first)) pos = first;
        else stb_vorbis_seek_start(v);  /* decode up to it instead */
    }
    while (r == 0 && pos < last && (n = stb_vorbis_get_samples_float(v, sch, dec, BLOCK)) > 0) {
        int s0 = 0, s1 = n, m;
        if (pos < first) s0 = (int)(first - pos < (unsigned long)n ? first - pos : (unsigned long)n);
        if (pos + n > last) s1 = (int)(last - pos);
        m = s1 - s0;
        if (m > 0) {
            for (c = 0; c < och; c++) {
                float *d = mixed[c];
                for (i = 0; i < m; i++) {
                    int s;
                    float x = 0;
                    for (s = 0; s < sch; s++) x += mix[c][s] * dec[s][s0 + i];
                    d[i] = x * gain;
                }
            }
            r = resample ? rs_put(&rs, &w, mixed, m) : writer_put(&w, mixed, m);
        }
        pos += n;
        if (o->verbose && last > first) {
            int p = (int)(100.0 * (pos > last ? last - first : pos - first) / (last - first)) / 10 * 10;
            if (p != pct) { pct = p; fprintf(stderr, "\r  %3d%%", p); fflush(stderr); }
        }
    }
    if (o->verbose) fprintf(stderr, "\n");
    if (r == 0 && resample) r = rs_flush(&rs, &w);
    if (resample) rs_free(&rs);
    if (w.clipped)
        fprintf(stderr, "ogg2iff: %s: %lu samples clipped\n", in, w.clipped);
    if (o->verbose)
        fprintf(stderr, "ogg2iff: %s: %lu frames written\n", outpath, w.frames);
    r = writer_close(&w, r == 0);

    for (c = 0; c < sch; c++) free(dec[c]);
    for (c = 0; c < och; c++) free(mixed[c]);
    stb_vorbis_close(v);
    free(al.alloc_buffer);
    return r;
}

/* --- names and the command line --------------------------------------- */

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static const char *base_name(const char *p)
{
    const char *b = p, *s;
    for (s = p; *s; s++) if (*s == '/' || *s == ':' || *s == '\\') b = s + 1;
    return b;
}

static char *join(const char *dir, const char *name)
{
    size_t l = strlen(dir);
    char *s = xcalloc(l + strlen(name) + 2, 1);
    memcpy(s, dir, l);
    if (l && dir[l - 1] != '/' && dir[l - 1] != ':') s[l++] = '/';
    strcpy(s + l, name);
    return s;
}

/* name.ogg -> name.aiff, in outdir or beside the input */
static char *out_name(const Opts *o, const char *in, const char *outdir)
{
    const char *b = base_name(in), *dot = strrchr(b, '.');
    size_t stem = dot ? (size_t)(dot - b) : strlen(b);
    char *name = xcalloc(stem + 8, 1), *full;
    memcpy(name, b, stem);
    strcpy(name + stem, fmt_ext[o->fmt]);
    if (outdir) {
        full = join(outdir, name);
    } else {
        size_t d = b - in;
        full = xcalloc(d + strlen(name) + 1, 1);
        memcpy(full, in, d);
        strcpy(full + d, name);
    }
    free(name);
    return full;
}

static int has_ogg_ext(const char *n)
{
    const char *d = strrchr(n, '.');
    char e[5];
    int i;
    if (!d || strlen(d) != 4) return 0;
    for (i = 0; i < 4; i++) e[i] = (char)tolower((unsigned char)d[i]);
    e[4] = 0;
    return !strcmp(e, ".ogg") || !strcmp(e, ".oga");
}

static int one(const Opts *o, const char *in, const char *outdir, const char *outfile)
{
    char *out = outfile ? NULL : out_name(o, in, outdir);
    int r = convert(o, in, outfile ? outfile : out);
    free(out);
    return r;
}

static int batch(const Opts *o, const char *dir, const char *outdir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int r = 0, count = 0;
    if (!d) {
        fprintf(stderr, "ogg2iff: can't read directory %s\n", dir);
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char *p;
        if (!has_ogg_ext(e->d_name)) continue;
        p = join(dir, e->d_name);
        if (!is_dir(p)) {
            if (one(o, p, outdir ? outdir : dir, NULL)) r = -1;
            count++;
        }
        free(p);
    }
    closedir(d);
    if (!count) fprintf(stderr, "ogg2iff: no .ogg files in %s\n", dir);
    return r;
}

static void usage(FILE *f)
{
    fprintf(f,
"ogg2iff " OGG2IFF_VERSION ": Ogg Vorbis to AIFF, IFF 8SVX, WAV or raw PCM\n"
"usage: ogg2iff [options] IN.ogg|DIR ...\n"
"  -f, --format F      aiff (default; 16-bit big-endian, the 68k's order),\n"
"                      8svx (8-bit signed, Paula), wav (little-endian), raw\n"
"  -o, --output P      output file, or a directory for several inputs or a DIR\n"
"  -r, --rate HZ       resample, e.g. 22050, 11025, 28867\n"
"  -m, --mono          downmix to mono\n"
"  -b, --bits 8|16     sample size (8SVX is always 8)\n"
"  -e, --endian big|little   byte order of raw 16-bit (default big)\n"
"  -u, --unsigned      raw 8-bit unsigned (default signed)\n"
"  -n, --normalize[=DB]      peak to DB dBFS (default -0.1); decodes twice\n"
"  -g, --gain DB       gain before anything else\n"
"  -s, --start SEC     -t, --end SEC     -l, --length SEC\n"
"  -T, --trim-silence[=DB]   cut silence below DB dBFS (default -50) at both ends\n"
"  -q, --quality 0|1|2 resampler: 0 short, 1 normal (default), 2 long\n"
"      --no-dither     no TPDF dither on 8-bit output\n"
"  -i, --info          describe the input, write nothing\n"
"  -v, --verbose       -h, --help\n");
}

/* --name=value, --name value, -x value */
static const char *arg_val(int argc, char **argv, int *i, const char *eq)
{
    if (eq) return eq + 1;
    if (*i + 1 >= argc) {
        fprintf(stderr, "ogg2iff: %s needs a value\n", argv[*i]);
        exit(10);
    }
    return argv[++*i];
}

static double num(const char *s, const char *what)
{
    char *e;
    double d = strtod(s, &e);
    if (e == s || *e) {
        fprintf(stderr, "ogg2iff: bad %s: %s\n", what, s);
        exit(10);
    }
    return d;
}

int main(int argc, char **argv)
{
    Opts o;
    const char **ins;
    int nin = 0, i, r = 0, any_dir = 0;

    memset(&o, 0, sizeof o);
    o.fmt = F_AIFF;
    o.big_endian = 1;
    o.norm_db = -0.1;
    o.silence_db = -50;
    o.end = o.length = -1;
    o.quality = 1;
    o.dither = 1;
    ins = xcalloc(argc, sizeof *ins);

    for (i = 1; i < argc; i++) {
        char *a = argv[i], *eq = NULL;
        if (a[0] != '-' || !a[1]) { ins[nin++] = a; continue; }
        if (a[1] == '-') {
            eq = strchr(a, '=');
            if (eq) *eq = 0;
        }
#define IS(s, l) (!strcmp(a, s) || !strcmp(a, l))
        if (IS("-h", "--help")) { usage(stdout); return 0; }
        else if (IS("-f", "--format")) {
            const char *v = arg_val(argc, argv, &i, eq);
            int k;
            for (k = 0; k < 4; k++) if (!strcmp(v, fmt_name[k])) break;
            if (!strcmp(v, "iff")) k = F_8SVX;
            if (!strcmp(v, "aif")) k = F_AIFF;
            if (!strcmp(v, "pcm")) k = F_RAW;
            if (k == 4) { fprintf(stderr, "ogg2iff: unknown format %s\n", v); return 10; }
            o.fmt = k;
        }
        else if (IS("-o", "--output")) o.out = arg_val(argc, argv, &i, eq);
        else if (IS("-r", "--rate")) o.rate = (long)num(arg_val(argc, argv, &i, eq), "rate");
        else if (IS("-m", "--mono")) o.mono = 1;
        else if (IS("-b", "--bits")) o.bits = (int)num(arg_val(argc, argv, &i, eq), "bits");
        else if (IS("-e", "--endian")) {
            const char *v = arg_val(argc, argv, &i, eq);
            if (!strcmp(v, "big")) o.big_endian = 1;
            else if (!strcmp(v, "little")) o.big_endian = 0;
            else { fprintf(stderr, "ogg2iff: endian is big or little\n"); return 10; }
        }
        else if (IS("-u", "--unsigned")) o.unsigned8 = 1;
        else if (IS("-n", "--normalize")) {
            o.normalize = 1;
            if (eq) o.norm_db = num(eq + 1, "normalize level");
        }
        else if (IS("-g", "--gain")) o.gain_db = num(arg_val(argc, argv, &i, eq), "gain");
        else if (IS("-s", "--start")) o.start = num(arg_val(argc, argv, &i, eq), "start");
        else if (IS("-t", "--end")) o.end = num(arg_val(argc, argv, &i, eq), "end");
        else if (IS("-l", "--length")) o.length = num(arg_val(argc, argv, &i, eq), "length");
        else if (IS("-T", "--trim-silence")) {
            o.trim_silence = 1;
            if (eq) o.silence_db = num(eq + 1, "silence level");
        }
        else if (IS("-q", "--quality")) o.quality = (int)num(arg_val(argc, argv, &i, eq), "quality");
        else if (!strcmp(a, "--no-dither")) o.dither = 0;
        else if (IS("-i", "--info")) o.info = 1;
        else if (IS("-v", "--verbose")) o.verbose = 1;
        else { fprintf(stderr, "ogg2iff: unknown option %s\n", a); usage(stderr); return 10; }
#undef IS
    }
    if (!nin) { usage(stderr); return 10; }
    if (o.bits && o.bits != 8 && o.bits != 16) { fprintf(stderr, "ogg2iff: bits is 8 or 16\n"); return 10; }
    if (o.fmt == F_8SVX && o.bits == 16) {
        fprintf(stderr, "ogg2iff: 8SVX is 8-bit; use aiff for 16-bit\n");
        return 10;
    }
    if (o.quality < 0 || o.quality > 2) { fprintf(stderr, "ogg2iff: quality is 0, 1 or 2\n"); return 10; }
    if (o.rate && (o.rate < 1000 || o.rate > 192000)) { fprintf(stderr, "ogg2iff: rate out of range\n"); return 10; }
    if (o.fmt == F_8SVX && o.rate > 65535) { fprintf(stderr, "ogg2iff: 8SVX rates stop at 65535\n"); return 10; }

    for (i = 0; i < nin; i++) any_dir |= is_dir(ins[i]);
    if (o.info) {
        for (i = 0; i < nin; i++) if (!is_dir(ins[i]) && convert(&o, ins[i], NULL)) r = 1;
        return r ? 10 : 0;
    }
    if (o.out && (nin > 1 || any_dir) && !is_dir(o.out)) {
        if (mkdir(o.out, 0777) && !is_dir(o.out)) {
            fprintf(stderr, "ogg2iff: can't make directory %s\n", o.out);
            return 20;
        }
    }
    for (i = 0; i < nin; i++) {
        int e;
        if (is_dir(ins[i])) e = batch(&o, ins[i], o.out);
        else if (o.out && is_dir(o.out)) e = one(&o, ins[i], o.out, NULL);
        else e = one(&o, ins[i], NULL, o.out);
        if (e) r = 1;
    }
    free(ins);
    return r ? 10 : 0;
}
