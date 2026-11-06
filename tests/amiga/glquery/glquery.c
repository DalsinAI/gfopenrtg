/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GLQuery: what Mesa (GL.module) gives a program, through virgl on the
 * host's graphics chip or softpipe on the 68k. For each of a GLES context
 * and a GL core context, the highest the display has: GL_VERSION,
 * GL_SHADING_LANGUAGE_VERSION, GL_RENDERER, the extension count and the
 * ones compute work needs, and the compute, SSBO and image limits. Then,
 * where the context has compute shaders, one is run: 256 invocations each
 * write a word of a shader storage buffer, read back and checked.
 *   GLQuery [CPU] [ES|CORE] [ALL]
 *     CPU: softpipe (else virgl where there is one); ES or CORE: only that
 *     context; ALL: every extension, one a line.
 * Returns 0 when each context asked for was made (and its compute run, if
 * it has compute, was right), 5 otherwise, 20 when GL didn't start. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <gla/gla_core.h>
#ifdef __amigaos__
#include <gla/os3/gla_virgl_os3.h>
#endif
#include <GL/gl.h>

/* GL 3.0-4.3 / GLES 3.0-3.1 names, here so no glext.h is needed. */
#define Q_NUM_EXTENSIONS                         0x821D
#define Q_MAJOR_VERSION                          0x821B
#define Q_MINOR_VERSION                          0x821C
#define Q_SHADING_LANGUAGE_VERSION               0x8B8C
#define Q_MAX_COMPUTE_WORK_GROUP_INVOCATIONS     0x90EB
#define Q_MAX_COMPUTE_WORK_GROUP_COUNT           0x91BE
#define Q_MAX_COMPUTE_WORK_GROUP_SIZE            0x91BF
#define Q_MAX_COMPUTE_SHARED_MEMORY_SIZE         0x8262
#define Q_MAX_COMPUTE_SHADER_STORAGE_BLOCKS      0x90DB
#define Q_MAX_COMPUTE_IMAGE_UNIFORMS             0x91BD
#define Q_MAX_SHADER_STORAGE_BUFFER_BINDINGS     0x90DD
#define Q_MAX_SHADER_STORAGE_BLOCK_SIZE          0x90DE
#define Q_MAX_COMBINED_SHADER_STORAGE_BLOCKS     0x90DC
#define Q_MAX_IMAGE_UNITS                        0x8F38
#define Q_MAX_COMBINED_IMAGE_UNIFORMS            0x90CF
#define Q_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS     0x90DA
#define Q_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS     0x92DC
#define Q_COMPUTE_SHADER                         0x91B9
#define Q_COMPILE_STATUS                         0x8B81
#define Q_LINK_STATUS                            0x8B82
#define Q_SHADER_STORAGE_BUFFER                  0x90D2
#define Q_DYNAMIC_COPY                           0x88EA
#define Q_MAP_READ_BIT                           0x0001
#define Q_SHADER_STORAGE_BARRIER_BIT             0x00002000
#define Q_BUFFER_UPDATE_BARRIER_BIT              0x00000200

typedef const GLubyte *(*fn_getstringi)(GLenum, GLuint);
typedef void (*fn_getintegeri)(GLenum, GLuint, GLint *);
typedef GLuint (*fn_createshader)(GLenum);
typedef void (*fn_shadersource)(GLuint, GLsizei, const char *const *, const GLint *);
typedef void (*fn_uint)(GLuint);
typedef void (*fn_getiv)(GLuint, GLenum, GLint *);
typedef void (*fn_getlog)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (*fn_createprogram)(void);
typedef void (*fn_attach)(GLuint, GLuint);
typedef void (*fn_genbuffers)(GLsizei, GLuint *);
typedef void (*fn_bindbuffer)(GLenum, GLuint);
typedef void (*fn_bindbufferbase)(GLenum, GLuint, GLuint);
typedef void (*fn_bufferdata)(GLenum, long, const void *, GLenum);
typedef void (*fn_dispatch)(GLuint, GLuint, GLuint);
typedef void (*fn_barrier)(GLbitfield);
typedef void *(*fn_mapbufferrange)(GLenum, long, long, GLbitfield);
typedef GLboolean (*fn_unmapbuffer)(GLenum);

#define S 16
static void present(void *user, const struct gla_frame *f) { (void)user; (void)f; }

/* The extensions compute work in GLES 3.1 programs leans on (core in ES 3.1,
 * extensions in GL before 4.3). */
static const char *const wanted[] = {
    "GL_ARB_compute_shader", "GL_ARB_shader_storage_buffer_object", "GL_ARB_shader_image_load_store",
    "GL_ARB_shader_atomic_counters", "GL_ARB_ES3_1_compatibility", "GL_ARB_ES3_compatibility",
    "GL_ARB_texture_storage", "GL_ARB_program_interface_query", "GL_ARB_draw_indirect",
    "GL_EXT_shader_image_load_store", "GL_OES_shader_image_atomic", "GL_EXT_texture_buffer",
    "GL_EXT_geometry_shader", "GL_EXT_tessellation_shader", "GL_EXT_color_buffer_float",
    "GL_KHR_debug", NULL
};

static int has_ext(fn_getstringi gs, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++) {
        const char *e = (const char *)gs(GL_EXTENSIONS, (GLuint)i);
        if (e && !strcmp(e, name)) return 1;
    }
    return 0;
}

static GLint geti(GLenum e)
{
    GLint v = -1;
    glGetIntegerv(e, &v);
    if (glGetError() != GL_NO_ERROR) v = -1;
    return v;
}

/* One compute dispatch: 4 groups of 64, v[i] = i * 3 + 1. 1 right, 0 wrong, -1 couldn't run. */
static int compute_run(int es)
{
    static const char *src_es =
        "#version 310 es\n"
        "layout(local_size_x = 64) in;\n"
        "layout(std430, binding = 0) buffer B { uint v[]; };\n"
        "void main() { v[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x * 3u + 1u; }\n";
    static const char *src_gl =
        "#version 430 core\n"
        "layout(local_size_x = 64) in;\n"
        "layout(std430, binding = 0) buffer B { uint v[]; };\n"
        "void main() { v[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x * 3u + 1u; }\n";
    fn_createshader createshader = (fn_createshader)gla_get_proc_address("glCreateShader");
    fn_shadersource shadersource = (fn_shadersource)gla_get_proc_address("glShaderSource");
    fn_uint compileshader = (fn_uint)gla_get_proc_address("glCompileShader");
    fn_getiv getshaderiv = (fn_getiv)gla_get_proc_address("glGetShaderiv");
    fn_getlog getshaderlog = (fn_getlog)gla_get_proc_address("glGetShaderInfoLog");
    fn_createprogram createprogram = (fn_createprogram)gla_get_proc_address("glCreateProgram");
    fn_attach attach = (fn_attach)gla_get_proc_address("glAttachShader");
    fn_uint linkprogram = (fn_uint)gla_get_proc_address("glLinkProgram");
    fn_getiv getprogramiv = (fn_getiv)gla_get_proc_address("glGetProgramiv");
    fn_getlog getprogramlog = (fn_getlog)gla_get_proc_address("glGetProgramInfoLog");
    fn_uint useprogram = (fn_uint)gla_get_proc_address("glUseProgram");
    fn_genbuffers genbuffers = (fn_genbuffers)gla_get_proc_address("glGenBuffers");
    fn_bindbuffer bindbuffer = (fn_bindbuffer)gla_get_proc_address("glBindBuffer");
    fn_bindbufferbase bindbufferbase = (fn_bindbufferbase)gla_get_proc_address("glBindBufferBase");
    fn_bufferdata bufferdata = (fn_bufferdata)gla_get_proc_address("glBufferData");
    fn_dispatch dispatch = (fn_dispatch)gla_get_proc_address("glDispatchCompute");
    fn_barrier barrier = (fn_barrier)gla_get_proc_address("glMemoryBarrier");
    fn_mapbufferrange mapbufferrange = (fn_mapbufferrange)gla_get_proc_address("glMapBufferRange");
    fn_unmapbuffer unmapbuffer = (fn_unmapbuffer)gla_get_proc_address("glUnmapBuffer");
    const char *src = es ? src_es : src_gl;
    GLuint sh, prog, buf;
    GLint ok = 0;
    char log[512];
    const GLuint *v;
    int i, bad = 0;

    if (!createshader || !shadersource || !compileshader || !getshaderiv || !createprogram || !attach || !linkprogram
        || !getprogramiv || !useprogram || !genbuffers || !bindbuffer || !bindbufferbase || !bufferdata || !dispatch
        || !barrier || !mapbufferrange || !unmapbuffer) {
        printf("  compute: a GL call is missing from GL.module\n");
        return -1;
    }
    sh = createshader(Q_COMPUTE_SHADER);
    shadersource(sh, 1, &src, NULL);
    compileshader(sh);
    getshaderiv(sh, Q_COMPILE_STATUS, &ok);
    if (!ok) {
        log[0] = 0;
        if (getshaderlog) getshaderlog(sh, sizeof log, NULL, log);
        printf("  compute: the shader didn't compile: %s\n", log);
        return -1;
    }
    prog = createprogram();
    attach(prog, sh);
    linkprogram(prog);
    getprogramiv(prog, Q_LINK_STATUS, &ok);
    if (!ok) {
        log[0] = 0;
        if (getprogramlog) getprogramlog(prog, sizeof log, NULL, log);
        printf("  compute: the program didn't link: %s\n", log);
        return -1;
    }
    genbuffers(1, &buf);
    bindbuffer(Q_SHADER_STORAGE_BUFFER, buf);
    bufferdata(Q_SHADER_STORAGE_BUFFER, 256 * 4, NULL, Q_DYNAMIC_COPY);
    bindbufferbase(Q_SHADER_STORAGE_BUFFER, 0, buf);
    useprogram(prog);
    dispatch(4, 1, 1);
    barrier(Q_SHADER_STORAGE_BARRIER_BIT | Q_BUFFER_UPDATE_BARRIER_BIT);
    v = (const GLuint *)mapbufferrange(Q_SHADER_STORAGE_BUFFER, 0, 256 * 4, Q_MAP_READ_BIT);
    if (!v) {
        printf("  compute: the buffer couldn't be mapped (GL error %04x)\n", (unsigned)glGetError());
        return -1;
    }
    for (i = 0; i < 256; i++)
        if (v[i] != (GLuint)i * 3u + 1u) {
            if (bad < 4) printf("  compute: v[%d] is %lu, not %lu\n", i, (unsigned long)v[i], (unsigned long)(i * 3 + 1));
            bad++;
        }
    unmapbuffer(Q_SHADER_STORAGE_BUFFER);
    printf("  compute: 4 groups of 64 ran, %d of 256 words right\n", 256 - bad);
    return bad ? 0 : 1;
}

static int query(struct gla_display *d, int profile, int all)
{
    struct gla_config cfg = { 0, 0, 0, 0, 0, 0, 0 };
    struct gla_present hook = { present, NULL };
    struct gla_context *c;
    struct gla_buffer *b;
    fn_getstringi getstringi;
    fn_getintegeri getintegeri;
    const char *const *w;
    GLint n, cnt[3] = { -1, -1, -1 }, sz[3] = { -1, -1, -1 }, inv;
    int i, es = profile == GLA_ES2, fails = 0;

    cfg.profile = profile;
    c = gla_context_create(d, &cfg, NULL);
    b = c ? gla_buffer_create(d, &cfg, S, S, &hook) : NULL;
    if (!b || !gla_make_current(c, b)) {
        printf("%s: no context\n", es ? "GLES" : "GL core");
        if (b) gla_buffer_destroy(b);
        if (c) gla_context_destroy(c);
        return 1;
    }
    printf("%s context\n", es ? "GLES" : "GL core");
    printf("  GL_VERSION: %s\n", (const char *)glGetString(GL_VERSION));
    printf("  GL_SHADING_LANGUAGE_VERSION: %s\n", (const char *)glGetString(Q_SHADING_LANGUAGE_VERSION));
    printf("  GL_RENDERER: %s\n", (const char *)glGetString(GL_RENDERER));
    printf("  GL_VENDOR: %s\n", (const char *)glGetString(GL_VENDOR));
    getstringi = (fn_getstringi)gla_get_proc_address("glGetStringi");
    getintegeri = (fn_getintegeri)gla_get_proc_address("glGetIntegeri_v");
    n = geti(Q_NUM_EXTENSIONS);
    printf("  extensions: %d\n", (int)n);
    if (getstringi && n > 0) {
        for (w = wanted; *w; w++) printf("    %-40s %s\n", *w, has_ext(getstringi, n, *w) ? "yes" : "no");
        if (all)
            for (i = 0; i < n; i++) printf("    = %s\n", (const char *)getstringi(GL_EXTENSIONS, (GLuint)i));
    }
    inv = geti(Q_MAX_COMPUTE_WORK_GROUP_INVOCATIONS);
    if (getintegeri)
        for (i = 0; i < 3; i++) {
            getintegeri(Q_MAX_COMPUTE_WORK_GROUP_COUNT, (GLuint)i, &cnt[i]);
            getintegeri(Q_MAX_COMPUTE_WORK_GROUP_SIZE, (GLuint)i, &sz[i]);
        }
    glGetError();
    printf("  GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS: %d\n", (int)inv);
    printf("  GL_MAX_COMPUTE_WORK_GROUP_COUNT: %d %d %d\n", (int)cnt[0], (int)cnt[1], (int)cnt[2]);
    printf("  GL_MAX_COMPUTE_WORK_GROUP_SIZE: %d %d %d\n", (int)sz[0], (int)sz[1], (int)sz[2]);
    printf("  GL_MAX_COMPUTE_SHARED_MEMORY_SIZE: %d\n", (int)geti(Q_MAX_COMPUTE_SHARED_MEMORY_SIZE));
    printf("  GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS: %d\n", (int)geti(Q_MAX_COMPUTE_SHADER_STORAGE_BLOCKS));
    printf("  GL_MAX_COMPUTE_IMAGE_UNIFORMS: %d\n", (int)geti(Q_MAX_COMPUTE_IMAGE_UNIFORMS));
    printf("  GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS: %d\n", (int)geti(Q_MAX_SHADER_STORAGE_BUFFER_BINDINGS));
    printf("  GL_MAX_SHADER_STORAGE_BLOCK_SIZE: %d\n", (int)geti(Q_MAX_SHADER_STORAGE_BLOCK_SIZE));
    printf("  GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS: %d\n", (int)geti(Q_MAX_COMBINED_SHADER_STORAGE_BLOCKS));
    printf("  GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS: %d\n", (int)geti(Q_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS));
    printf("  GL_MAX_IMAGE_UNITS: %d\n", (int)geti(Q_MAX_IMAGE_UNITS));
    printf("  GL_MAX_COMBINED_IMAGE_UNIFORMS: %d\n", (int)geti(Q_MAX_COMBINED_IMAGE_UNIFORMS));
    printf("  GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS: %d\n", (int)geti(Q_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS));
    if (inv > 0 && compute_run(es) != 1) fails++;
    else if (inv <= 0) printf("  compute: none in this context\n");
    gla_make_current(NULL, NULL);
    gla_buffer_destroy(b);
    gla_context_destroy(c);
    return fails;
}

int main(int argc, char **argv)
{
    struct gla_display *d = NULL;
    int i, cpu = 0, all = 0, es = 1, core = 1, fails = 0;

    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "CPU")) cpu = 1;
        else if (!strcasecmp(argv[i], "ALL")) all = 1;
        else if (!strcasecmp(argv[i], "ES")) core = 0;
        else if (!strcasecmp(argv[i], "CORE")) es = 0;
    }
#ifdef __amigaos__
    if (!cpu) {
        struct gla_virgl_transport t;
        if (gla_os3_virgl_transport(&t)) d = gla_display_create_virgl(&t);
        if (!d) printf("GLQuery: no virgl here\n");
    }
#endif
    (void)cpu;
    if (!d) d = gla_display_create();
    if (!d) {
        printf("GLQuery: GL couldn't start\n");
        return 20;
    }
    printf("GLQuery: %s gives GL %d (compat), %d (core), GLES %d\n", gla_display_driver(d),
           gla_display_version(d, GLA_COMPAT), gla_display_version(d, GLA_CORE), gla_display_version(d, GLA_ES2));
    if (es) fails += query(d, GLA_ES2, all);
    if (core) fails += query(d, GLA_CORE, all);
    gla_display_destroy(d);
    printf(fails ? "GLQuery: %d failed\n" : "GLQuery: all right\n", fails);
    return fails ? 5 : 0;
}
