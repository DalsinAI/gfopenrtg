/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ACVirgl's host side: the request blocks of OGPU_OP_VIRGL
 * (include/gfopengpu/virgl.h) carried out with virglrenderer, so Mesa's virgl
 * driver on the Amiga draws GL and GLES on the host's graphics chip. The
 * Cradle's runtime calls it from its ring (amicradle's acrtg.c, as its
 * `virgl` hook), in ring order.
 *
 * virglrenderer (MIT) is opened with dlopen, so a runtime without it still
 * starts: acvirgl_create says why not, and OGPU_OP_VIRGL stays unknown. It
 * makes its own GL context (EGL, surfaceless) on the thread that calls
 * acvirgl_run; every call must come from that thread. Desktop GL first,
 * else GLES (a Pi's V3D); AC_VIRGL_GLES=1 or 0 picks one, and
 * AC_VIRGL_DRIVER names Mesa's driver (zink: GL over the host's Vulkan). */
#ifndef ACVIRGL_H
#define ACVIRGL_H

#include <stdint.h>

struct acvirgl;

/* lib: the libvirglrenderer.so.1 to open (a path), or 0 for the
 * environment's ACVIRGL_LIB. Returns 0 with the reason in err. */
struct acvirgl *acvirgl_create(const char *lib, char *err, int errlen);
void acvirgl_destroy(struct acvirgl *v);

/* One request block of `bytes` at Amiga address addr, reached through map.
 * Returns 0, or an OGPU_ERR_ when the block itself can't be read; each
 * command's own result is in its result word. */
int acvirgl_run(struct acvirgl *v, uint8_t *(*map)(void *map_user, uint32_t address, uint32_t length), void *map_user,
                uint32_t addr, uint32_t bytes);

/* What it has done, for the runtime's log. */
struct acvirgl_stats {
    uint64_t blocks, submits, submit_words, puts, put_bytes, gets, get_bytes, errors;
    uint64_t frame_get_bytes;                       /* GETs of display targets: frames read back into Amiga memory */
    uint64_t presents, present_rects, present_pixels;   /* ACV_PRESENT: frames written straight into a bitmap */
    uint64_t present_ns;                            /* time reading them back and writing them */
    uint64_t deferred, deferred_ready, deferred_forced; /* presents left to finish; finished after their fence, or before */
};
void acvirgl_stats(const struct acvirgl *v, struct acvirgl_stats *s);

/* Presents left to finish (7 Nov 2026). ACV_PRESENT reads the frame back
 * from the GPU, which waits until the GPU has drawn it: on a busy GPU that
 * is milliseconds, and the caller (the Cradle's emulator thread, so the
 * whole 68k) stood still for it. With deferral on, a present whose target
 * ok() allows (the runtime: video RAM) puts a fence in the frame's
 * context and returns at once, with its result (the pixels it will
 * write); its pixels are read back and written later, on the same thread:
 * once the fence has signalled (acvirgl_settle with ACVIRGL_READY, and the
 * start of every block), and always before the context's next command, the
 * resource's unref, a present over the same rectangles, or
 * acvirgl_settle(ACVIRGL_ALL), which the runtime calls before anything
 * else draws there. Off until this is called; needs virglrenderer's
 * per-context fences (1.0 and later), else presents stay as they were. */
#define ACVIRGL_HAS_DEFER 1  /* these calls and the stats' deferred fields are here (a runtime may build with an older acvirgl) */
void acvirgl_defer_presents(struct acvirgl *v, int (*ok)(void *map_user, uint32_t addr, uint32_t bytes));
#define ACVIRGL_READY 0     /* those whose fences have signalled */
#define ACVIRGL_ALL   1     /* all, now */
#define ACVIRGL_DROP  2     /* none: forget them (a reset; their memory may be gone) */
/* Returns how many were finished. */
int acvirgl_settle(struct acvirgl *v, uint8_t *(*map)(void *map_user, uint32_t address, uint32_t length), void *map_user, int how);
int acvirgl_pending(const struct acvirgl *v);

/* "GL" or "GLES": the kind of context virglrenderer is drawing with. */
const char *acvirgl_api(const struct acvirgl *v);
/* The GPU it draws on, the host's GL_RENDERER: "V3D 7.1", "zink Vulkan 1.3(V3D 7.1 ...)", "unknown". */
const char *acvirgl_renderer(const struct acvirgl *v);

#endif
