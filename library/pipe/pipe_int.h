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
 * Plain C with no Amiga calls: memory and the transport are the pipe's transport's (pipe_xport_*), so the same files
 * build for the PC tests and, later, a native module (GF-91). */
#ifndef OGPU_PIPE_INT_H
#define OGPU_PIPE_INT_H

#include "../../include/gfopengpu/pipe.h"
#include "../../include/gfopengpu/virgl_cmd.h"

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
};

struct ogpu_pipe_ctx {
    OGPUPipeScreen *screen;
    ULONG ctx_id;
    /* the block: pre-commands at its start, then (when sent) the arenas' PUTs, the stream's SUBMIT, the post-commands
     * and the present */
    ULONG *blk; ULONG npre, cpre;
    ULONG *post; ULONG npost, cpost;
    ULONG *cmd; ULONG ncmd, ccmd;           /* the stream, little-endian words */
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
OGPUPipeCtx *pipe_ctx_create(OGPUPipeScreen *s, ULONG ctx_id);
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

/* pipe_arena.c */
int   pipe_arenas(OGPUPipeCtx *x, ULONG va, ULONG ia, ULONG cmd);
ULONG pipe_arena_puts(OGPUPipeCtx *x, ULONG *at);   /* the arenas' PUTs into the block at *at; bytes of vertices */
void  pipe_arena_after(OGPUPipeCtx *x, ULONG va_bytes, ULONG ia_bytes);   /* the rings, after a block */
int   pipe_need(OGPUPipeCtx *x, ULONG va_bytes, ULONG ia_count, ULONG cmd_words);

#endif
