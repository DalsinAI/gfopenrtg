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
    /* (ACVIRGL_HAS_BEHIND) a present's parts: */
    uint64_t behind;                                /* presents read back one frame behind (acvirgl_behind_presents) */
    uint64_t direct;                                /* presents the GPU wrote straight into guest memory (acvirgl_direct_presents) */
    uint64_t present_gpu_ns;                        /* AC_VIRGL_PARTS=1: waiting for the GPU before a read back */
    uint64_t present_read_ns;                       /* the read back itself (transfer_read_iov, or mapping a staged frame) */
    uint64_t present_conv_ns;                       /* turning it into the target's format */
    uint64_t present_issue_ns;                      /* behind: asking the GPU for the frame's copy */
    uint64_t present_wait_ns;                       /* behind: of present_read_ns, mapping a frame not yet ready (it waited) */
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
/* Presents one frame behind (7 Nov 2026). Deferral still read the frame
 * back with transfer_read_iov, and most were forced before their fence by
 * the context's next frame. Here a present whose target ok() allows asks the
 * GPU itself to copy the frame into a staging buffer (glReadPixels into a
 * pixel-pack buffer, in the frame's own GL context: queued after its
 * drawing, no wait) and returns; the staging buffer is mapped and written
 * into the target once its fence has signalled (acvirgl_settle with
 * ACVIRGL_READY: every block, every vertical blank), and at the latest before
 * a present over the same rectangles, the context's destruction or
 * acvirgl_settle(ACVIRGL_ALL). The copy is the frame as it was when it was
 * presented, so the context's later commands need not wait for it: the
 * Amiga sees each frame up to one present later. The pixels a program reads
 * back itself (ACV_GET, glReadPixels) are the resource's, as before. Needs
 * per-context fences and virgl_renderer_resource_get_info (virglrenderer 1.0
 * and later) and EGL's GL entry points; else presents stay as they were.
 * Replaces acvirgl_defer_presents' setting (one or the other); 0 turns it off. */
#define ACVIRGL_HAS_BEHIND 1
void acvirgl_behind_presents(struct acvirgl *v, int (*ok)(void *map_user, uint32_t addr, uint32_t bytes));
/* Direct presents (8 Nov 2026). A present into 32-bit memory that ok()
 * allows is written by the GPU itself: a compute shader, in the frame's own
 * GL context, turns the frame into the guest's layout (X or A, R, G, B, either
 * way up) straight into the guest's memory, so this side neither copies nor
 * converts. Needs GL 4.3 and a way to give the GPU that memory:
 *   file() says the memory is a file, a memfd sealed against shrinking
 *     (*fd, *offset of addr in it, *size of it; returns 1): it is imported
 *     through /dev/udmabuf and GL_EXT_memory_object_fd, in windows;
 *   otherwise anonymous memory is pinned with GL_AMD_pinned_memory.
 * Both callbacks are given user, whatever the block's map_user is.
 * Anything else, or any failure, presents as before. It waits for the GPU
 * (glFinish) like an ordinary present, and ok() has the rules of
 * acvirgl_behind_presents' (not memory the runtime guards or watches). Ok 0
 * turns it off. The GPU holds the memory's pages: call acvirgl_direct_forget
 * before they change under it (a hole punched in the file, memory unmapped or
 * moved); the buffers go on the GL thread at the next direct present. Timed
 * by hand on a clear-only frame, 1920x1080: 7.7 to 1.9 ms on a Vega 8 (memfd
 * through udmabuf). */
#define ACVIRGL_HAS_DIRECT 1
void acvirgl_direct_presents(struct acvirgl *v, int (*ok)(void *user, uint32_t addr, uint32_t bytes),
                             int (*file)(void *user, uint32_t addr, int *fd, uint64_t *offset, uint64_t *size), void *user);
void acvirgl_direct_forget(struct acvirgl *v);
/* Returns how many were finished. */
int acvirgl_settle(struct acvirgl *v, uint8_t *(*map)(void *map_user, uint32_t address, uint32_t length), void *map_user, int how);
int acvirgl_pending(const struct acvirgl *v);

/* What a request block will write besides its own words (7 Nov 2026), for a
 * runtime that carries blocks out on a thread of its own while the guest's
 * CPU runs on: the block is read through map as acvirgl_run reads it and,
 * for each ACV_PRESENT, present(user, address, bytes) is told the target's
 * bytes it may write. Returns 0 when the block writes nothing else of the
 * guest's memory and reads nothing in the range [avoid, avoid + avoid_len)
 * (the runtime's video RAM, which the CPU may be writing); 1 when it may
 * (ACV_GET and ACV_CAPS write the guest's memory; a command reading that
 * range; a block it can't read or parse): such a block must run while the
 * guest's CPU is stopped, as before. Any thread; touches no virglrenderer
 * state. */
#define ACVIRGL_HAS_SCAN 1
int acvirgl_block_scan(uint8_t *(*map)(void *map_user, uint32_t address, uint32_t length), void *map_user,
                       uint32_t addr, uint32_t bytes, uint32_t avoid, uint32_t avoid_len,
                       void (*present)(void *user, uint32_t address, uint32_t bytes), void *user);

/* "GL" or "GLES": the kind of context virglrenderer is drawing with. */
const char *acvirgl_api(const struct acvirgl *v);
/* The GPU it draws on, the host's GL_RENDERER: "V3D 7.1", "zink Vulkan 1.3(V3D 7.1 ...)", "unknown". */
const char *acvirgl_renderer(const struct acvirgl *v);

#endif
