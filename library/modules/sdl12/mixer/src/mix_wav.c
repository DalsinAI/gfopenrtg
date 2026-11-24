/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * WAV files and the Ogg/WAV choice (mix_wav.h). */
#include "mix_wav.h"

#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t mw_riff_size(const uint8_t head[12])
{
    uint32_t n;
    if (memcmp(head, "RIFF", 4) || memcmp(head + 8, "WAVE", 4)) {
        return 0;
    }
    n = le32(head + 4);
    return n > 0xFFFFFFF0u ? 0 : n + 8;
}

const char *mw_parse(const uint8_t *d, uint32_t size, struct mw_info *w)
{
    const uint8_t *fmt = NULL;
    uint32_t fmt_len = 0, pos = 12;
    memset(w, 0, sizeof *w);
    if (size < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) {
        return "not a WAV file";
    }
    while (pos + 8 <= size) {
        uint32_t len = le32(d + pos + 4);
        if (len > size - pos - 8) {
            len = size - pos - 8;   /* some files say more than they hold */
        }
        if (!memcmp(d + pos, "fmt ", 4) && len >= 16) {
            fmt = d + pos + 8;
            fmt_len = len;
        } else if (!memcmp(d + pos, "data", 4) && !w->data) {
            w->data = d + pos + 8;
            w->data_len = len;
        }
        if (len + (len & 1) > size - pos - 8) {
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (!fmt || !w->data) {
        return "WAV without fmt or data";
    }
    w->format = le16(fmt);
    w->channels = le16(fmt + 2);
    w->rate = le32(fmt + 4);
    w->block_align = le16(fmt + 12);
    w->bits = le16(fmt + 14);
    if (w->channels < 1 || w->channels > 2 || w->rate < 1000 || w->rate > 192000) {
        return "WAV: only mono or stereo, 1 to 192 kHz";
    }
    if (w->format == MW_PCM) {
        if (w->bits != 8 && w->bits != 16) {
            return "WAV: only 8 or 16-bit PCM";
        }
        w->frames = w->data_len / (uint32_t)(w->channels * (w->bits / 8));
    } else if (w->format == MW_ADPCM) {
        if (w->channels != 1 || w->bits != 4 || w->block_align < 5 || fmt_len < 20) {
            return "WAV: only mono IMA ADPCM";
        }
        w->block_frames = le16(fmt + 18);
        if (w->block_frames != (w->block_align - 4) * 2 + 1) {
            return "WAV: IMA ADPCM blocks of an odd size";
        }
        w->frames = (w->data_len / w->block_align) * w->block_frames;
    } else {
        return "WAV: only PCM or IMA ADPCM";
    }
    if (!w->frames) {
        return "WAV: empty";
    }
    return NULL;
}

void mw_pcm16(const struct mw_info *w, int16_t *out)
{
    uint32_t i, n = w->frames * (uint32_t)w->channels;
    const uint8_t *p = w->data;
    if (w->bits == 8) {
        for (i = 0; i < n; i++) {
            out[i] = (int16_t)(((int)p[i] - 128) * 256);
        }
    } else {
        for (i = 0; i < n; i++) {
            out[i] = (int16_t)le16(p + 2 * i);
        }
    }
}

static const int16_t step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};
static const int8_t index_table[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

void mw_adpcm_block(const struct mw_info *w, uint32_t b, int16_t *out)
{
    const uint8_t *p = w->data + b * w->block_align;
    int pred = (int16_t)le16(p);
    int index = p[2] > 88 ? 88 : p[2];
    uint32_t i;
    out[0] = (int16_t)pred;
    p += 4;
    for (i = 1; i < w->block_frames; i++) {
        int code = (i & 1) ? (*p & 15) : (*p++ >> 4);
        int step = step_table[index];
        int diff = step >> 3;
        if (code & 4) {
            diff += step;
        }
        if (code & 2) {
            diff += step >> 1;
        }
        if (code & 1) {
            diff += step >> 2;
        }
        pred += (code & 8) ? -diff : diff;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        index += index_table[code];
        index = index < 0 ? 0 : index > 88 ? 88 : index;
        out[i] = (int16_t)pred;
    }
}

/* The extension: the last '.' after the last '/' or ':'. */
static const char *ext_of(const char *name)
{
    const char *e = NULL, *p;
    for (p = name; *p; p++) {
        if (*p == '.') {
            e = p;
        } else if (*p == '/' || *p == ':') {
            e = NULL;
        }
    }
    return e;
}

int mw_sibling(const char *asked, const char *ext, char *out, size_t n)
{
    const char *e = ext_of(asked);
    size_t base = e ? (size_t)(e - asked) : strlen(asked);
    size_t el = strlen(ext);
    if (base + el + 1 > n) {
        return 0;
    }
    memcpy(out, asked, base);
    memcpy(out + base, ext, el + 1);
    return 1;
}

int mw_is_wav_name(const char *name)
{
    const char *e = ext_of(name);
    return e && (e[1] == 'w' || e[1] == 'W') && (e[2] == 'a' || e[2] == 'A') &&
           (e[3] == 'v' || e[3] == 'V') && !e[4];
}

int mw_on_68k(const char *by)
{
    return by && !strncmp(by, "this ", 5);
}

int mw_choose(int asked_is_wav, int asked_there, int wav_there, int dt_ok, int dt_68k)
{
    if (asked_is_wav) {
        if (asked_there) {
            return MW_USE_WAV;
        }
        return dt_ok ? MW_USE_DT : MW_NONE;
    }
    if (dt_ok) {
        return dt_68k && wav_there ? MW_USE_WAV : MW_USE_DT;
    }
    (void)asked_there;
    return wav_there ? MW_USE_WAV : MW_NONE;
}
