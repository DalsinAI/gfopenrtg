/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Stream3D's TRIANGLES on the GPU's own graphics pipeline: its rasteriser,
 * depth test and blender, not a shader that does their work
 * (docs/design/Stream3D-GPU-Pipeline.md). ogpu_vk.c tries it first for
 * TRIANGLES when OGPU_VK_3D (or AC_VK_3D) is "gfx", then ogpu3d_rec.c's
 * compute shader, then the C core.
 *
 * Pictures are the core's within a tolerance (stream3d.h: a GPU never
 * matches an integer core bit for bit): the fragment shader does the core's
 * pixel in integers up to the alpha test, so only interpolation, the
 * texture unit's filtering, the blender's rounding and edge pixels differ.
 *
 * What a run looks like:
 *  - TRIANGLES runs ogpu_3d.c's own checks and setup up to its sink's begin
 *    (so errors, the draw area and the settings are the core's), which
 *    takes the settings and stops it; the vertices are read here.
 *  - Consecutive commands into one target (and Z buffer) are a "group": one
 *    command buffer and one render pass. Opening it copies the rows the
 *    first command draws from memory into GPU images (byte-swapped where the
 *    format needs it); a later command that draws other rows has them
 *    copied in too. Closing it (any other drawing command, the end of the
 *    run) sends it, waits, and copies the rows back, so memory is right
 *    when the run returns, as on the core.
 *  - Textures go to the GPU as RGBA8 once and are used again while their
 *    texels are the same (a 64-bit hash, checked the first time a texture
 *    is used in a run).
 * Declined, and drawn by the compute shader or the core as before: CLUT8
 * targets, logic ops, stencil, chroma test, stipple, PIXELART, a border
 * colour Vulkan has no fixed colour for, Z buffers other than Z16 and Z32,
 * and a target overlapping its Z buffer. */
#ifndef OGPU3D_GFX_H
#define OGPU3D_GFX_H

#include <vulkan/vulkan.h>
#include "../../library/opengpu/ogpu_3d.h"

struct ogpu3d_gfx;

struct ogpu3d_gfx_cfg {
    VkPhysicalDevice phys;
    VkDevice dev;
    VkQueue queue;
    uint32_t qfam;
    /* Send the back end's own work and wait for it: called before a group
     * opens, so the rows it reads from memory are the ones drawn before. */
    void (*before)(void *user);
    void *user;
    unsigned long long wait_ns;     /* a submit's wait gives up after this (0: 20 s) */
    int force_gpu;                  /* every command it can draw on the GPU, not by cost */
};

struct ogpu3d_gfx_stats {
    unsigned long draws, declined, groups, tex_uploads, pipelines;
    unsigned long routed;           /* drawn by the core here, as it was the quicker */
    /* milliseconds, all told: the back end's own work sent first (before),
     * rows into staging, the submit and its wait, rows back into memory */
    double before_ms, in_ms, wait_ms, out_ms;
};

/* 0 when the device lacks what it needs (why in why[]). */
struct ogpu3d_gfx *ogpu3d_gfx_create(const struct ogpu3d_gfx_cfg *cfg, char *why, int whylen);
void ogpu3d_gfx_destroy(struct ogpu3d_gfx *g);

/* A run starts: textures are checked again the first time each is used. */
void ogpu3d_gfx_new_run(struct ogpu3d_gfx *g);

/* A TRIANGLES command (header first, `words` long) of the 3D state t: 1
 * when done (recorded into the open group, drawn by the core here because
 * that was quicker than opening a group, or an error or nothing to draw, as
 * the core would say), its result in *result; 0 to leave it to the caller
 * (the group is closed first, so memory is right for it).
 *
 * By cost: a command that would open a group is drawn by the core when its
 * estimate (a command, its triangles and its pixels; the core's time over
 * the estimate is learned) is under the group's (its submit and wait and
 * its rows in and out, learned as groups close). One joining an open group
 * stays on the GPU. cfg.force_gpu, or OGPU_GFX_ROUTE=gpu, sends every
 * command it can draw to the GPU (OGPU_GFX_ROUTE=cost: by cost). */
int ogpu3d_gfx_triangles(struct ogpu3d_gfx *g, struct ogpu3d *t, const ogpu_u8 *cmd, long words, int *result);

/* 1 while a group is open. */
int ogpu3d_gfx_open(const struct ogpu3d_gfx *g);

/* Send the open group, wait, and put its rows back in memory. 0 if the
 * device failed (from then on everything is declined). */
int ogpu3d_gfx_close(struct ogpu3d_gfx *g);

void ogpu3d_gfx_stats(const struct ogpu3d_gfx *g, struct ogpu3d_gfx_stats *s);

#endif
