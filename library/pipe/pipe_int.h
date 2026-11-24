/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's own structures (stage M3 of docs/design/OpenGPU-Shared-Pipe-Plan.md): the context's block, its
 * resources and its arenas. Only library/pipe/ includes this; the front ends see the handles in pipe.h and reach
 * everything through the table.
 *
 * Taken from minigl.library's virgl route (mgl_vg_enc.c, mgl_vg_frame.c, mgl_vg_tex.c), with its names generalised:
 *   pipe_block.c   the context, its block (pre-commands, the little-endian stream, post-commands), sending
 *   pipe_res.c     resources: versions (a rename is a new version under the same OGPUPipeRes), reference counts,
 *                  PUT and GET, the deferred unref
 *   pipe_arena.c   the vertex and index arenas, and the rings in the host's buffers (never a PUT over what an
 *                  earlier block's draws read)
 *   pipe_ff.c      (M5, from mgl_vg_shader.c and w3d_vg_shader.c) the one fixed-function generator, by key
 *   pipe_state.c   (M4, from mgl_vg_state.c) state objects by key, binding only what changed, the viewport, the
 *                  scissor and the constants sent only when they change; the context's object handles
 *   pipe_frame.c   (M7, from mgl_vg_enc.c, mgl_vg_tex.c and w3d_vg.c) surfaces and sampler views, the framebuffer,
 *                  the texture units, the buffers and the draws, clears and copies
 * Plain C with no Amiga calls: memory and the transport are the pipe's transport's (pipe_xport_*), so the same files
 * build for the PC tests and, later, a native module (GF-91). */
#ifndef OGPU_PIPE_INT_H
#define OGPU_PIPE_INT_H

#include "../../include/gfopengpu/pipe.h"
#include "virgl_cmd.h"

#ifndef ADDR
#define ADDR(p) ((ULONG)(p))
#endif

/* Sizes a context starts with (mgl_vg_frame.c's). */
#define PIPE_CMD_WORDS   (64 * 1024)        /* the stream */
#define PIPE_PRE_WORDS   (8 * 1024)         /* creates and PUTs before it */
#define PIPE_POST_WORDS  1024               /* unrefs after it */
#define PIPE_BLK_SLACK   (64 + 2 * ACV_PRESENT_MAX + 16 + PIPE_POST_WORDS)
#define PIPE_VA_BYTES    (2 * 1024 * 1024)  /* vertices */
#define PIPE_IA_COUNT    (256 * 1024)       /* 16-bit indices */
#define PIPE_RING        4                  /* the host's buffers: arenas long */
#define PIPE_DRAW_ROOM   8192               /* stream words a draw may need */

/* One version of a resource on the host. Its handle is its address: virglrenderer has one namespace of resources for
 * all its clients, and an address is unique while the version lives. */
struct pipe_ver {
    ULONG handle;
    struct pipe_ver *next_dead;             /* let go of: freed after the block that unrefs it has run */
};

struct ogpu_pipe_res {
    struct OGPUPipeResDesc desc;
    struct pipe_ver *ver;                   /* the current version */
    ULONG refs;                             /* res_create's, and res_ref's */
    ULONG versions;                         /* made, the first included */
    OGPUPipeCtx *used_ctx;                  /* the context whose stream last used it ... */
    ULONG used_seq;                         /* ... and that stream's number */
    ULONG put_seq;                          /* the stream whose block has a PUT of it waiting (+1; 0 none) */
    ULONG depth, array_size, nr_samples, vflags;   /* res_create2's (version 6); 0, 0, 0, 0: res_create's 1, 1, 0, 0 */
    ULONG last_level;                       /* res_create2's, when depth isn't 0 */
    /* version 7: the context's own objects for it (pipe_frame.c), made at first use, let go of with it */
    OGPUPipeCtx *obj_ctx;                   /* the context whose objects they are (one namespace a context) */
    ULONG surf, surf_of;                    /* its surface, and the version's handle it was made for (0: none) */
    ULONG view, view_of;                    /* its sampler view, likewise */
};

/* A state object: its handle on the host (0: an empty slot). */
struct ogpu_pipe_cso {
    ULONG handle;
};

#define PIPE_CACHE     64                   /* blend, DSA and rasterizer objects a context keeps of each */
#define PIPE_SAMPLERS  32
#define PIPE_LAYOUTS   16
#define PIPE_SHADERS   64                   /* shader variants a context keeps of each stage */
#define PIPE_UNITS     8                    /* texture units (bind_texture) */

struct pipe_cache {
    ULONG key[PIPE_CACHE][5];
    struct ogpu_pipe_cso cso[PIPE_CACHE];
};

struct ogpu_pipe_ctx {
    OGPUPipeScreen *screen;
    ULONG ctx_id;
    /* the block: pre-commands at its start, then (when sent) the arenas' PUTs, the stream's SUBMIT, the post-commands
     * and the present */
    ULONG *blk; ULONG npre, cpre;
    ULONG *post; ULONG npost, cpost;
    ULONG *cmd; ULONG ncmd, ccmd;           /* the stream, little-endian words */
    ULONG cut;                              /* its words already among the pre-commands as SUBMITs (stream_cut) */
    ULONG seq;                              /* streams sent: the one being built is number seq */
    ULONG errors;                           /* commands the host refused in the last block */
    struct pipe_ver *dead;                  /* unrefs in this block's post: freed after it runs */
    /* the arenas (pipe_arena.c) */
    UBYTE *va; ULONG nva, cva;
    UWORD *ia; ULONG nia, cia;
    OGPUPipeRes *vbuf, *ibuf;
    ULONG vb_off, vb_size, ib_base, ib_size;
    ULONG grow_va, grow_ia, grow_cmd;
    ULONG changed;                          /* OGPU_PIPE_CH_: what a front end that emits its own bindings must set */
    /* state (pipe_state.c) */
    ULONG next_obj, objects;                /* object handles: the context's one namespace */
    struct pipe_cache c_blend, c_dsa, c_rs;
    ULONG samp_key[PIPE_SAMPLERS][6]; struct ogpu_pipe_cso samp[PIPE_SAMPLERS]; int nsamp;
    ULONG lay_key[PIPE_LAYOUTS]; struct ogpu_pipe_cso lay[PIPE_LAYOUTS]; int nlay;
    ULONG b_blend, b_dsa, b_rs, b_ve;       /* bound (0: nothing known) */
    ULONG vp[6]; LONG sc[4]; int vp_known, sc_known;   /* the viewport as float bits */
    ULONG consts[2][OGPU_CONST_MAX * 4]; ULONG nconst[2];   /* as last sent, by stage */
    /* shaders (pipe_ff.c) */
    char *text;                             /* a shader's text, being made */
    ULONG vs_key[PIPE_SHADERS][2], fs_key[PIPE_SHADERS][2], vs_h[PIPE_SHADERS], fs_h[PIPE_SHADERS];
    int nvs, nfs;
    struct ogpu_pipe_shader cur;            /* ff_shader's answer */
    ULONG b_vs, b_fs;
    /* the frame's bindings (pipe_frame.c, version 7) */
    ULONG caps_bits;                        /* the screen's (the views' format) */
    OGPUPipeRes *u_res[PIPE_UNITS]; ULONG u_samp[PIPE_UNITS]; ULONG nunits;   /* bound; units 0 .. nunits - 1 go */
    ULONG s_view[PIPE_UNITS], s_samp[PIPE_UNITS]; int s_views, s_samps;      /* as last sent, and how many (0: not known) */
    ULONG s_vb[3]; int s_vb_known;          /* handle, offset, stride */
    ULONG s_ib[2]; int s_ib_known;          /* handle, offset */
    ULONG s_sref, s_bcol[4], s_stip[32]; int s_sref_known, s_bcol_known, s_stip_known;
    ULONG d_stride;                         /* the last draw_buf's: draw's */
    /* counts */
    ULONG blocks, words, puts, renames, va_bytes;
    int sending;                            /* in pipe_send: a send asked for from inside it waits */
};

/* pipe_block.c */
ULONG *pipe_pre(OGPUPipeCtx *x, ULONG words);       /* room before the stream (sends first when none) */
ULONG *pipe_cmd(OGPUPipeCtx *x, ULONG words);       /* room in the stream (sends first when none); 0: too big */
ULONG *pipe_post(OGPUPipeCtx *x, ULONG words);      /* room after it (sends first when none) */
LONG  pipe_send(OGPUPipeCtx *x, const struct OGPUPresentHook *hook);
LONG  pipe_run(OGPUPipeCtx *x, ULONG *blk, ULONG words);

/* pipe_res.c */
OGPUPipeRes *pipe_res_create(OGPUPipeCtx *x, const struct OGPUPipeResDesc *d);
void  pipe_res_release(OGPUPipeCtx *x, OGPUPipeRes *r);
void  pipe_res_free_dead(OGPUPipeCtx *x);           /* after the block ran */
void  pipe_res_drop_all(OGPUPipeCtx *x);            /* a context going: the dead list freed, sent or not */

/* the context (pipe_block.c): the screen's first context, its block and arenas */
OGPUPipeCtx *pipe_ctx_create(OGPUPipeScreen *s, ULONG ctx_id, ULONG flags);
void  pipe_ctx_destroy(OGPUPipeCtx *x);

/* the table's members (pipe_screen.c's table names them) */
LONG  pipe_m_res_write(OGPUPipeCtx *x, OGPUPipeRes *r);
void  pipe_m_res_update(OGPUPipeCtx *x, OGPUPipeRes *r, ULONG level, const struct OGPUBox *b, const void *src,
                        ULONG stride, ULONG swap);
LONG  pipe_m_res_read(OGPUPipeCtx *x, OGPUPipeRes *r, ULONG level, const struct OGPUBox *b, void *dst, ULONG stride);
OGPUPipeRes *pipe_m_res_create(OGPUPipeCtx *x, const struct OGPUPipeResDesc *d);
void  pipe_m_res_release(OGPUPipeCtx *x, OGPUPipeRes *r);
void  pipe_m_res_ref(OGPUPipeRes *r);
ULONG pipe_m_res_handle(const OGPUPipeRes *r);
void  pipe_m_res_used(OGPUPipeCtx *x, OGPUPipeRes *r);
ULONG pipe_m_res_pending(OGPUPipeCtx *x, const OGPUPipeRes *r);
LONG  pipe_m_flush(OGPUPipeCtx *x, ULONG *fence);
LONG  pipe_m_end_frame(OGPUPipeCtx *x, const struct OGPUPresentHook *hook, ULONG *fence);
ULONG *pipe_m_cmd_room(OGPUPipeCtx *x, ULONG words);
ULONG pipe_m_sent(OGPUPipeCtx *x);
ULONG pipe_m_errors(OGPUPipeCtx *x);
ULONG pipe_m_changed(OGPUPipeCtx *x);
void  pipe_m_counts(OGPUPipeCtx *x, ULONG *v5);
void *pipe_m_vtx_room(OGPUPipeCtx *x, ULONG bytes, ULONG *first);
UWORD *pipe_m_idx_room(OGPUPipeCtx *x, ULONG count, ULONG *first);
ULONG pipe_m_vtx_put_range(OGPUPipeCtx *x, const void *mem, ULONG bytes);
LONG  pipe_m_need(OGPUPipeCtx *x, ULONG va_bytes, ULONG ia_count, ULONG cmd_words);
void  pipe_m_arena_state(OGPUPipeCtx *x, struct OGPUPipeArenas *a);
LONG  pipe_m_room(OGPUPipeCtx *x, ULONG va_bytes, ULONG ia_count, ULONG cmd_words, ULONG pre_words);
void  pipe_m_buf_put(OGPUPipeCtx *x, OGPUPipeRes *r, ULONG offset, const void *src, ULONG bytes, ULONG swap);
OGPUPipeRes *pipe_m_res_create2(OGPUPipeCtx *x, const struct OGPUPipeResDesc2 *d);
LONG  pipe_m_xfer(OGPUPipeCtx *x, OGPUPipeRes *r, const struct OGPUPipeXfer *t);
LONG  pipe_m_stream_cut(OGPUPipeCtx *x);
ULONG pipe_m_queued(OGPUPipeCtx *x);

/* pipe_state.c */
ULONG pipe_m_obj_new(OGPUPipeCtx *x);
ULONG pipe_m_cso_handle(const OGPUPipeCSO *o);
void  pipe_m_state_reset(OGPUPipeCtx *x);
OGPUPipeCSO *pipe_m_blend_create(OGPUPipeCtx *x, const struct OGPUBlendKey *b);
OGPUPipeCSO *pipe_m_dsa_create(OGPUPipeCtx *x, const struct OGPUDsaKey *d);
OGPUPipeCSO *pipe_m_raster_create(OGPUPipeCtx *x, const struct OGPURasterKey *r);
OGPUPipeCSO *pipe_m_sampler_create(OGPUPipeCtx *x, const struct OGPUSamplerKey *s);
OGPUPipeCSO *pipe_m_layout_create(OGPUPipeCtx *x, const struct OGPULayout *l);
OGPUPipeCSO *pipe_m_blend_create2(OGPUPipeCtx *x, const struct OGPUBlendKey2 *b);
OGPUPipeCSO *pipe_m_dsa_create2(OGPUPipeCtx *x, const struct OGPUDsaKey2 *d);
OGPUPipeCSO *pipe_m_sampler_create2(OGPUPipeCtx *x, const struct OGPUSamplerKey2 *s);
void  pipe_m_cso_release(OGPUPipeCtx *x, OGPUPipeCSO *o);
void  pipe_m_bind_blend(OGPUPipeCtx *x, OGPUPipeCSO *o);
void  pipe_m_bind_dsa(OGPUPipeCtx *x, OGPUPipeCSO *o);
void  pipe_m_bind_raster(OGPUPipeCtx *x, OGPUPipeCSO *o);
void  pipe_m_bind_layout(OGPUPipeCtx *x, OGPUPipeCSO *o);
void  pipe_m_set_viewport(OGPUPipeCtx *x, const float v6[6]);
void  pipe_m_set_scissor(OGPUPipeCtx *x, const struct OGPUBox *b);
void  pipe_m_set_constants(OGPUPipeCtx *x, ULONG stage, const float *v, ULONG vec4s);

/* pipe_frame.c (version 7) */
void  pipe_frame_res_gone(OGPUPipeCtx *x, OGPUPipeRes *r);   /* r's objects destroyed, its units let go of */
void  pipe_m_begin_frame(OGPUPipeCtx *x);
void  pipe_m_bind_framebuffer(OGPUPipeCtx *x, OGPUPipeRes *colour, OGPUPipeRes *depth);
void  pipe_m_bind_texture(OGPUPipeCtx *x, ULONG unit, OGPUPipeRes *r, OGPUPipeCSO *sampler);
void  pipe_m_set_blend_colour(OGPUPipeCtx *x, const float rgba[4]);
void  pipe_m_set_stencil_ref(OGPUPipeCtx *x, ULONG front, ULONG back);
void  pipe_m_set_stipple(OGPUPipeCtx *x, const ULONG rows[32]);
void  pipe_m_draw(OGPUPipeCtx *x, const struct OGPUDrawInfo *d);
void  pipe_m_draw_buf(OGPUPipeCtx *x, const struct OGPUDrawBuf *d);
void  pipe_m_clear(OGPUPipeCtx *x, ULONG buffers, const float rgba[4], double depth, ULONG stencil);
void  pipe_m_copy(OGPUPipeCtx *x, const struct OGPUCopy *c);

/* pipe_ff.c */
OGPUPipeShader *pipe_m_ff_shader(OGPUPipeCtx *x, const struct OGPUFFKey *k);
void  pipe_m_bind_shader(OGPUPipeCtx *x, OGPUPipeShader *sh);
ULONG pipe_m_ff_vs_consts(const struct OGPUFFKey *k);
ULONG pipe_ff_text(const struct OGPUFFKey *k, int fragment, char *buf, ULONG cap);   /* the differential test's */

/* pipe_arena.c */
int   pipe_arenas(OGPUPipeCtx *x, ULONG va, ULONG ia, ULONG cmd);
ULONG pipe_arena_puts(OGPUPipeCtx *x, ULONG *at);   /* the arenas' PUTs into the block at *at; bytes of vertices */
void  pipe_arena_after(OGPUPipeCtx *x, ULONG va_bytes, ULONG ia_bytes);   /* the rings, after a block */
int   pipe_need(OGPUPipeCtx *x, ULONG va_bytes, ULONG ia_count, ULONG cmd_words);

#endif
