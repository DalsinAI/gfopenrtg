/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * A picture's size and whether it has alpha, read from its own bytes, for
 * the formats games load most (JPEG, PNG, lossy WebP). With it, rung 1
 * (ogpu_image.c) makes the surface first and asks media.decode/1 for the
 * pixels in one trip, instead of a PROBE trip and then a DECODE trip, each
 * carrying the whole file to the cores and back: under host load each trip
 * waits its turn there. What isn't read here (or DECODE answers with
 * another size) goes the PROBE way as before.
 *
 * Plain C, no Amiga or SDL headers: ogpu_image.c includes it, and
 * tests/sdl2/test_image_size.py checks it on the host against Pillow. */
#include <stddef.h>

static unsigned long ois_be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | p[3];
}

static unsigned ois_be16(const unsigned char *p)
{
    return ((unsigned)p[0] << 8) | p[1];
}

static unsigned ois_le16(const unsigned char *p)
{
    return ((unsigned)p[1] << 8) | p[0];
}

static int ois_tag(const unsigned char *p, const char *t)
{
    return p[0] == (unsigned char)t[0] && p[1] == (unsigned char)t[1] &&
           p[2] == (unsigned char)t[2] && p[3] == (unsigned char)t[3];
}

/* PNG: IHDR's size; alpha for grey+alpha or RGBA, or a tRNS chunk before
 * the first IDAT (a colour key, which media.decode gives as alpha).
 * Paletted PNGs never come here (rung 1 leaves them to the CPU). */
static int ois_png(const unsigned char *f, size_t n, unsigned long *w, unsigned long *h, int *alpha)
{
    size_t at = 8;
    unsigned ct;

    if (n < 33 || !ois_tag(f + 12, "IHDR")) {
        return 0;
    }
    *w = ois_be32(f + 16);
    *h = ois_be32(f + 20);
    ct = f[25];
    if (ct == 3) {
        return 0;
    }
    *alpha = (ct == 4 || ct == 6);
    for (;;) {
        unsigned long len;
        if (at + 8 > n) {
            return 0;                   /* cut short before any IDAT */
        }
        len = ois_be32(f + at);
        if (ois_tag(f + at + 4, "tRNS")) {
            *alpha = 1;
            break;
        }
        if (ois_tag(f + at + 4, "IDAT") || ois_tag(f + at + 4, "IEND")) {
            break;
        }
        if (len > n - at - 8) {
            return 0;
        }
        at += 12 + (size_t)len;
    }
    return *w && *h;
}

/* JPEG: the first frame header (SOF0..SOF15 but DHT, JPG and DAC). Never
 * alpha. EXIF rotation isn't applied by media.decode either. */
static int ois_jpeg(const unsigned char *f, size_t n, unsigned long *w, unsigned long *h, int *alpha)
{
    size_t at = 2;

    while (at + 4 <= n) {
        unsigned m, len;
        if (f[at] != 0xFF) {
            return 0;
        }
        m = f[at + 1];
        if (m == 0xFF) {
            at++;                       /* fill byte */
            continue;
        }
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
            at += 2;                    /* no length */
            continue;
        }
        if (m == 0xD9 || m == 0xDA) {
            return 0;                   /* image data or end before a frame */
        }
        len = ois_be16(f + at + 2);
        if (len < 2) {
            return 0;
        }
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
            if (at + 9 > n || len < 7) {
                return 0;
            }
            *h = ois_be16(f + at + 5);
            *w = ois_be16(f + at + 7);
            *alpha = 0;
            return *w && *h;
        }
        at += 2 + (size_t)len;
    }
    return 0;
}

/* WebP: only the simple lossy file (RIFF, WEBP, VP8 first), which has no
 * alpha. Lossless and extended files go the PROBE way. */
static int ois_webp(const unsigned char *f, size_t n, unsigned long *w, unsigned long *h, int *alpha)
{
    if (n < 30 || !ois_tag(f + 12, "VP8 ")) {
        return 0;
    }
    if (f[23] != 0x9D || f[24] != 0x01 || f[25] != 0x2A) {
        return 0;                       /* not a key frame's start code */
    }
    *w = ois_le16(f + 26) & 0x3FFF;
    *h = ois_le16(f + 28) & 0x3FFF;
    *alpha = 0;
    return *w && *h;
}

/* 1 and the picture's size and alpha when f is a JPEG, PNG or lossy WebP
 * whose header says them; else 0. */
static int ogpu_image_size(const unsigned char *f, size_t n,
                           unsigned long *w, unsigned long *h, int *alpha)
{
    *w = *h = 0;
    *alpha = 0;
    if (n >= 3 && f[0] == 0xFF && f[1] == 0xD8 && f[2] == 0xFF) {
        return ois_jpeg(f, n, w, h, alpha);
    }
    if (n >= 8 && f[0] == 0x89 && ois_tag(f + 1, "PNG\r") && f[5] == '\n' && f[6] == 0x1A && f[7] == '\n') {
        return ois_png(f, n, w, h, alpha);
    }
    if (n >= 16 && ois_tag(f, "RIFF") && ois_tag(f + 8, "WEBP")) {
        return ois_webp(f, n, w, h, alpha);
    }
    return 0;
}
