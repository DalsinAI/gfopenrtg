/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * What virglrenderer hands the Amiga on this machine: acvirgl_create opens
 * it as the runtime does (EGL, surfaceless; desktop GL, else GLES), then
 * capset 2 (struct virgl_caps_v2, virglrenderer's virgl_hw.h, compiled in
 * from its pinned source) is read and the parts GLES 3.1 with compute needs
 * are printed: the host's renderer (the GPU virgl draws on), the GLSL level,
 * the compute, SSBO, image and atomic limits and the capability bits. Mesa's
 * virgl driver on the Amiga gives what these allow.
 *   virgl_caps_here [libvirglrenderer.so.1]     (else ACVIRGL_LIB)
 * Exit 0, 2 with no virglrenderer here, 1 when GLES 3.1's compute can't be
 * given (no compute shaders, SSBOs or images in compute) or the renderer is
 * software (llvmpipe, softpipe) unless ACVIRGL_SOFTWARE_OK=1. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "virgl_hw.h"
#include "../../host/virgl/acvirgl.h"

int main(int argc, char **argv)
{
    const char *lib = argc > 1 ? argv[1] : getenv("ACVIRGL_LIB");
    char err[256];
    struct acvirgl *v;
    void *dl;
    void (*get_cap_set)(uint32_t, uint32_t *, uint32_t *);
    void (*fill_caps)(uint32_t, uint32_t, void *);
    union virgl_caps *caps;
    struct virgl_caps_v2 *c;
    uint32_t mv = 0, ms = 0;
    int bad = 0, soft;

    if (!lib) { printf("virgl_caps_here: no virglrenderer named (ACVIRGL_LIB)\n"); return 2; }
    if (!(v = acvirgl_create(lib, err, sizeof err))) { printf("virgl_caps_here: %s\n", err); return 2; }
    /* the same library, already loaded: its own calls for the caps */
    if (!(dl = dlopen(lib, RTLD_NOW | RTLD_NOLOAD))) { printf("virgl_caps_here: %s\n", dlerror()); return 2; }
    *(void **)&get_cap_set = dlsym(dl, "virgl_renderer_get_cap_set");
    *(void **)&fill_caps = dlsym(dl, "virgl_renderer_fill_caps");
    if (!get_cap_set || !fill_caps) { printf("virgl_caps_here: no caps calls\n"); return 2; }
    get_cap_set(2, &mv, &ms);
    caps = calloc(1, ms > sizeof *caps ? ms : sizeof *caps);
    fill_caps(2, mv, caps);
    c = &caps->v2;
    printf("virgl: %s context, renderer \"%.64s\", capset 2 version %u, %u bytes\n",
           acvirgl_api(v), c->renderer, (unsigned)mv, (unsigned)ms);
    printf("  glsl_level %u, max_texture_2d_size %u\n", (unsigned)c->v1.glsl_level, (unsigned)c->max_texture_2d_size);
    printf("  compute shader: %s\n", c->capability_bits & VIRGL_CAP_COMPUTE_SHADER ? "yes" : "no");
    printf("  max_compute_work_group_invocations %u\n", (unsigned)c->max_compute_work_group_invocations);
    printf("  max_compute_grid_size %u %u %u, max_compute_block_size %u %u %u\n",
           (unsigned)c->max_compute_grid_size[0], (unsigned)c->max_compute_grid_size[1], (unsigned)c->max_compute_grid_size[2],
           (unsigned)c->max_compute_block_size[0], (unsigned)c->max_compute_block_size[1], (unsigned)c->max_compute_block_size[2]);
    printf("  max_compute_shared_memory_size %u\n", (unsigned)c->max_compute_shared_memory_size);
    printf("  SSBOs: frag/compute %u, vertex %u, combined %u\n", (unsigned)c->max_shader_buffer_frag_compute,
           (unsigned)c->max_shader_buffer_other_stages, (unsigned)c->max_combined_shader_buffers);
    printf("  images: frag/compute %u, vertex %u\n", (unsigned)c->max_shader_image_frag_compute,
           (unsigned)c->max_shader_image_other_stages);
    printf("  atomic counters: %u, buffers %u\n", (unsigned)c->max_combined_atomic_counters,
           (unsigned)c->max_combined_atomic_counter_buffers);
    printf("  texture multisample: %s, indirect draw: %s\n",
           c->v1.bset.texture_multisample ? "yes" : "no", c->v1.bset.has_indirect_draw ? "yes" : "no");
    printf("  capability_bits %08x, capability_bits_v2 %08x\n", (unsigned)c->capability_bits, (unsigned)c->capability_bits_v2);
    if (!(c->capability_bits & VIRGL_CAP_COMPUTE_SHADER)) { printf("FAIL: no compute shaders\n"); bad++; }
    if (c->max_shader_buffer_frag_compute < 4) { printf("FAIL: fewer than 4 SSBOs in compute (GLES 3.1 asks for 4)\n"); bad++; }
    if (c->max_shader_image_frag_compute < 4) { printf("FAIL: fewer than 4 images in compute (GLES 3.1 asks for 4)\n"); bad++; }
    if (c->max_compute_work_group_invocations < 128) { printf("FAIL: under 128 invocations a group (GLES 3.1 asks for 128)\n"); bad++; }
    if (c->v1.glsl_level < 310) { printf("FAIL: GLSL level %u, under 310\n", (unsigned)c->v1.glsl_level); bad++; }
    soft = strstr(c->renderer, "llvmpipe") || strstr(c->renderer, "softpipe") || strstr(c->renderer, "lavapipe");
    if (soft) {
        const char *ok = getenv("ACVIRGL_SOFTWARE_OK");
        printf("%s: virgl draws in software (%.64s)\n", ok && !strcmp(ok, "1") ? "note" : "FAIL", c->renderer);
        if (!(ok && !strcmp(ok, "1"))) bad++;
    }
    acvirgl_destroy(v);
    printf(bad ? "virgl_caps_here: %d failed\n" : "virgl_caps_here: GLES 3.1 with compute can be given\n", bad);
    return bad ? 1 : 0;
}
