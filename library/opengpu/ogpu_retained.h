/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The retained surface's core (gfopengpu.library 0.25, include/gfopengpu/retained.h):
 * which 32x32 tiles of a surface changed since they were last uploaded, and
 * the OGPU stream that uploads them into the surface's texture and draws the
 * whole texture once into the target. Plain C with no C library and no
 * Amiga headers, so tests/test_retained.c runs it on the host, through the
 * CPU core.
 *
 * The surface (the program's pixels, in CPU memory) is authoritative. Its
 * texture is a copy the back end keeps; only tiles that changed are copied
 * into it. A present finds them one of two ways:
 *   - the caller marked rectangles (ogpu_rs_mark) since the last present:
 *     the tiles they touch, and nothing is hashed that present;
 *   - otherwise each tile is hashed and compared with its hash when it was
 *     last uploaded.
 * The first present (and one after ogpu_rs_all) uploads every tile and
 * hashes each as it goes. A tile uploaded because it was marked has no hash
 * (none was taken), so a later present that hashes uploads it once more and
 * keeps its hash from then on: a program that always marks never pays for
 * hashing, and one that mixes the two pays one extra upload of the tiles it
 * marked. */
#ifndef OGPU_RETAINED_H
#define OGPU_RETAINED_H

#define OGPU_RS_TILE        32
#define OGPU_RS_TILE_SHIFT  5

/* A tile's state bits (ogpu_rs.state[]). */
#define OGPU_RS_DIRTY       1   /* to be uploaded at this present */
#define OGPU_RS_NOHASH      2   /* hash[] doesn't hold the tile's hash as uploaded */

struct ogpu_rs {
    const unsigned char *pixels;    /* row 0 of the surface */
    long bpr;                       /* its bytes a row */
    int w, h, bpp;                  /* pixels, and bytes a pixel (1, 2 or 4) */
    int tx, ty;                     /* tiles across and down */
    long ntiles;
    unsigned long *hash;            /* ntiles: each tile's hash as last uploaded */
    unsigned char *state;           /* ntiles: OGPU_RS_ bits */
    int marked;                     /* rectangles marked since the last present */
    int all;                        /* every tile at the next present */
    long dirty;                     /* tiles to upload at this present (after ogpu_rs_collect) */
    unsigned long hashed;           /* tiles hashed so far (statistics) */
};

/* Bytes of memory ogpu_rs_init needs for a w x h surface (hash[] and state[]). */
long ogpu_rs_mem(int w, int h);
/* mem: ogpu_rs_mem(w, h) bytes, aligned on 4. Every tile is uploaded at the first present. */
void ogpu_rs_init(struct ogpu_rs *s, const void *pixels, long bpr, int w, int h, int bpp, void *mem);
/* The caller changed x, y, w, h (clipped to the surface); w or h below 0:
 * the whole surface. 0 changes nothing. */
void ogpu_rs_mark(struct ogpu_rs *s, long x, long y, long w, long h);
/* Upload every tile at the next present (the texture is lost or stale). */
void ogpu_rs_all(struct ogpu_rs *s);
/* The hash of tile t (row-major): every byte of the tile inside the surface. */
unsigned long ogpu_rs_tile_hash(const struct ogpu_rs *s, long t);
/* At a present: sets OGPU_RS_DIRTY on the tiles to upload, and returns how many. */
long ogpu_rs_collect(struct ogpu_rs *s);
/* The next run of dirty tiles in a row from tile *at: its rectangle in
 * pixels (clipped to the surface), *at moved past it; the run's tiles, or 0
 * when there is none left. */
int ogpu_rs_next_run(const struct ogpu_rs *s, long *at, int *x, int *y, int *w, int *h);
/* The runs ogpu_rs_next_run will give (to size a stream). */
long ogpu_rs_runs(const struct ogpu_rs *s);
/* After the upload went: the dirty tiles are in the texture. */
void ogpu_rs_done(struct ogpu_rs *s);
/* After it failed: everything again at the next present. */
void ogpu_rs_fail(struct ogpu_rs *s);

/* The GPU path's batch: what goes where. Addresses as the back end sees them. */
struct ogpu_rs_present {
    unsigned long src;              /* the surface (slot 0) */
    unsigned long tex, tex_bpr;     /* its texture (slot 1), the surface's size and format */
    unsigned long palette;          /* INDEX8: 256 ARGB32 words, read when the texture is drawn; else 0 */
    int format;                     /* the surface's and texture's OGPU_FMT_ */
    unsigned long dst, dst_bpr;     /* the target (slot 2) */
    int dst_w, dst_h, dst_format;
    int x, y, w, h;                 /* where the whole texture is drawn in the target (scaled to w x h) */
    const short *clips;             /* nclips rectangles x0, y0, x1, y1 (x1, y1 inclusive), target pixels; */
    int nclips;                     /* 0: the target's whole area; below 0: none (the tiles are uploaded, nothing drawn) */
    unsigned long comp_flags;       /* COMPOSITE's flags: 0 (nearest) or OGPU_COMP_BILINEAR */
    unsigned long fence;            /* the FENCE id at the end */
};

/* Words the batch takes at most for this present (after ogpu_rs_collect). */
long ogpu_rs_present_words(const struct ogpu_rs *s, int nclips);
/* Writes the batch (big-endian words) into buf, cap words long: the dirty
 * tiles' runs copied from slot 0 into slot 1, then slot 1 drawn into slot
 * 2 once for each clip. Returns its words, or -1 when it doesn't fit. */
long ogpu_rs_build_present(const struct ogpu_rs *s, const struct ogpu_rs_present *p, unsigned char *buf, long cap);

#endif
