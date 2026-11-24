/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's stream3d emitter (stage M7, part 1): the batch builder Warp3D (w3d_batch.c) and MiniGL
 * (mgl_out.c) each had, once. See include/gfopengpu/pipe_s3d.h. The words are written by gfopengpu's builders
 * (library/opengpu/ogpu_build.c, ogpu_build3d.c), which the front ends link, in the order the front ends wrote
 * them: tests/pipe/test_s3d_same.c holds that word for word.
 *
 * No float is touched here (the pipe's rule): RENDER3D's arguments come as the longs the stream carries. */
#include "../../include/gfopengpu/pipe_s3d.h"
#include "../../include/gfopengpu/stream3d.h"

static void s3d_init(struct OGPUPipeS3D *e, ULONG *cmd, LONG batch_words, LONG prefix_words, UBYTE *vmem, ULONG vert_bytes)
{
    e->cmd = cmd;
    e->batch_words = batch_words;
    e->prefix_words = prefix_words;
    e->vmem = vmem;
    e->vcap = vert_bytes;
    e->vused = 0;
    e->body = 0;
    e->list_at = -1;
    e->list_vend = 0;
    ogpu_batch_init(&e->b, cmd + prefix_words, batch_words - prefix_words);
}

static LONG s3d_room(const struct OGPUPipeS3D *e, LONG words, ULONG vbytes)
{
    LONG have = e->body ? e->b.words : 0;
    return !(have + words > e->batch_words - e->prefix_words || e->vused + vbytes > e->vcap);
}

static LONG s3d_begin(struct OGPUPipeS3D *e, const struct OGPUPipeS3DBuf *depth, const struct OGPUPipeS3DBuf *stencil)
{
    if (e->body) return 0;
    ogpu_batch_init(&e->b, e->cmd + e->prefix_words, e->batch_words - e->prefix_words);
    e->body = 1;                        /* (the data area empties at reset) */
    e->list_at = -1;
    if (depth) ogpu_depth(&e->b, OGPU_KIND_DEPTH, depth->address, depth->bpr, depth->w, depth->h, depth->format);
    if (stencil) ogpu_depth(&e->b, OGPU_KIND_STENCIL, stencil->address, stencil->bpr, stencil->w, stencil->h, stencil->format);
    return 1;
}

static UBYTE *s3d_data(struct OGPUPipeS3D *e, ULONG bytes)
{
    UBYTE *p;
    bytes = (bytes + 3) & ~3UL;
    if (e->vused + bytes > e->vcap) return 0;
    p = e->vmem + e->vused;
    e->vused += bytes;
    return p;
}

static void s3d_clip(struct OGPUPipeS3D *e, LONG x, LONG y, LONG w, LONG h)
{
    ogpu_clip(&e->b, x, y, w, h);
    e->list_at = -1;
}

static void s3d_render3d(struct OGPUPipeS3D *e, const unsigned long *args)
{
    ogpu_render3d(&e->b, args);
    e->list_at = -1;
}

static void s3d_texenv(struct OGPUPipeS3D *e, LONG unit, LONG mode, unsigned long colour)
{
    ogpu_texenv(&e->b, unit, mode, colour);
    e->list_at = -1;
}

static void s3d_texture(struct OGPUPipeS3D *e, LONG unit, unsigned long address, unsigned long bpr, LONG w, LONG h,
                        unsigned long format, unsigned long table, unsigned long generation, LONG min, LONG mag,
                        LONG wrap_s, LONG wrap_t, unsigned long border, LONG levels, const unsigned long *level_addrs)
{
    ogpu_texture(&e->b, unit, address, bpr, w, h, format, table, generation, min, mag, wrap_s, wrap_t, border, levels,
                 level_addrs);
    e->list_at = -1;
}

/* A list's vertex count is the second word after its header; the stream's words are big-endian. */
static void s3d_emit(struct OGPUPipeS3D *e, unsigned long vaddr, ULONG n, ULONG stride, LONG prim, LONG tex, unsigned long layout)
{
    ULONG bytes = n * stride;
    if (!n) return;
    if (prim == OGPU_LAY_LIST && e->list_at >= 0 && (ULONG)vaddr == e->list_vend) {
        UBYTE *cnt = (UBYTE *)(e->cmd + e->prefix_words) + (e->list_at + 2) * 4;
        ULONG v = (((ULONG)cnt[0] << 24) | ((ULONG)cnt[1] << 16) | ((ULONG)cnt[2] << 8) | cnt[3]) + n;
        cnt[0] = (UBYTE)(v >> 24); cnt[1] = (UBYTE)(v >> 16); cnt[2] = (UBYTE)(v >> 8); cnt[3] = (UBYTE)v;
        e->list_vend = (ULONG)vaddr + bytes;
        return;
    }
    if (prim == OGPU_LAY_LIST) { e->list_at = e->b.words; e->list_vend = (ULONG)vaddr + bytes; }
    else e->list_at = -1;
    ogpu_triangles3d(&e->b, vaddr, n, stride, 0, 0, 0, (int)tex, 0, layout | (unsigned long)prim);
}

static void s3d_fill(struct OGPUPipeS3D *e, LONG x, LONG y, LONG w, LONG h, unsigned long raw)
{
    ogpu_fill(&e->b, x, y, w, h, raw);
    e->list_at = -1;
}

static void s3d_clear3d(struct OGPUPipeS3D *e, LONG kind, LONG x, LONG y, LONG w, LONG h, unsigned long value)
{
    ogpu_clear3d(&e->b, kind, x, y, w, h, value);
    e->list_at = -1;
}

static LONG s3d_finish(struct OGPUPipeS3D *e, const struct OGPUPipeS3DBuf *surface, UBYTE **stream)
{
    struct OGPUBatch pre;
    ULONG prefix[32];
    LONG pw = e->prefix_words > 32 ? 32 : e->prefix_words, body = e->body ? e->b.words : 0;
    UBYTE *s, *p;
    LONG i;
    ogpu_batch_init(&pre, prefix, pw);
    ogpu_surface(&pre, 0, surface->address, surface->bpr, surface->w, surface->h, surface->format);
    ogpu_target(&pre, 0);
    s = (UBYTE *)(e->cmd + e->prefix_words - pre.words);
    p = (UBYTE *)prefix;
    for (i = 0; i < pre.words * 4; i++) s[i] = p[i];
    e->body = 0;
    e->list_at = -1;
    *stream = s;
    return pre.words + body;
}

static void s3d_reset(struct OGPUPipeS3D *e)
{
    e->b.words = 0;
    e->vused = 0;
}

static const struct OGPUPipeS3DOps s3d_ops = {
    OGPU_PIPE_S3D_VERSION,
    s3d_init, s3d_room, s3d_begin, s3d_data, s3d_clip, s3d_render3d, s3d_texenv, s3d_texture, s3d_emit, s3d_fill,
    s3d_clear3d, s3d_finish, s3d_reset,
};

const struct OGPUPipeS3DOps *ogpu_pipe_s3d_ops(ULONG version)
{
    return version <= OGPU_PIPE_S3D_VERSION ? &s3d_ops : 0;
}
