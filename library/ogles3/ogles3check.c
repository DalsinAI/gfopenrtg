/* Copyright (c) 2026 Dalsin Limited / the AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OGLES3Check: what an ogles3.library context offers, through the library's
 * dispatch table as a program built with ogles3's SDK reaches it.
 *   OGLES3Check [ES2] [WINDOW] [EXTENSIONS]
 * Prints the library's version, GL_VENDOR, GL_RENDERER, GL_VERSION,
 * GL_SHADING_LANGUAGE_VERSION, the compute limits and, for an ES 3.1
 * context, runs a compute shader (256 invocations write a buffer that is
 * read back and checked). WINDOW draws into a window of the library's own
 * and shows four frames; without it the context has no surface
 * (OGLES3_CCT_PISTORM_STATE_ONLY). EXTENSIONS lists them all. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <exec/types.h>
#include <exec/libraries.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "ogles3_slots.h"

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef long GLintptr;
typedef long GLsizeiptr;
typedef unsigned int GLbitfield;
typedef float GLfloat;

#define GL_NO_ERROR 0
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_NUM_EXTENSIONS 0x821D
#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS 0x90EB
#define GL_MAX_COMPUTE_WORK_GROUP_COUNT 0x91BE
#define GL_MAX_COMPUTE_WORK_GROUP_SIZE 0x91BF
#define GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS 0x90DB
#define GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS 0x90DD
#define GL_COMPUTE_SHADER 0x91B9
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_DYNAMIC_COPY 0x88EA
#define GL_MAP_READ_BIT 0x0001
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#define GL_COLOR_BUFFER_BIT 0x00004000

#define CCT_MIN (TAG_USER + 0x4f4c0000UL)
#define CCT_GET_WIDTH (CCT_MIN + 7)
#define CCT_GET_HEIGHT (CCT_MIN + 8)
#define CCT_PS_STATE_ONLY (CCT_MIN + 0x1001)
#define CCT_PS_WIDTH (CCT_MIN + 0x1002)
#define CCT_PS_HEIGHT (CCT_MIN + 0x1003)
#define CCT_PS_ES_VERSION (CCT_MIN + 0x1006)

struct dispatch {
    ULONG abiVersion, structSize, backendFlags, reserved;
    APTR entry[380];
};

static struct Library *OGLES3Base;
static const struct dispatch *D;
static void *ctx;

static const struct dispatch *get_dispatch(void)
{
    register const struct dispatch *d __asm("d0");
    register struct Library *a6 __asm("a6") = OGLES3Base;
    __asm__ __volatile__("jsr -30(%%a6)" : "=r"(d) : "r"(a6) : "d1", "a0", "a1", "fp0", "fp1", "cc", "memory");
    return d;
}

#define CALL(slot, type) ((type)D->entry[OGL3_SLOT_##slot])
#define S(name) ((const char *)CALL(GLGetString, const unsigned char *(*)(void *, GLenum))(ctx, name))

static GLint geti(GLenum p)
{
    GLint v = -1;
    CALL(GLGetIntegerv, void (*)(void *, GLenum, GLint *))(ctx, p, &v);
    return v;
}

static GLint getii(GLenum p, GLuint i)
{
    GLint v = -1;
    CALL(GLGetIntegeri_v, void (*)(void *, GLenum, GLuint, GLint *))(ctx, p, i, &v);
    return v;
}

static const char compute_src[] =
    "#version 310 es\n"
    "layout(local_size_x = 64) in;\n"
    "layout(std430, binding = 0) buffer B { uint v[]; };\n"
    "void main() { v[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x * 3u + 1u; }\n";

static int compute_test(void)
{
    GLuint sh, prog, buf;
    GLint ok = 0, i, bad = 0;
    const char *src = compute_src;
    char log[512];
    const GLuint *v;
    sh = CALL(GLCreateShader, GLuint (*)(void *, GLenum))(ctx, GL_COMPUTE_SHADER);
    CALL(GLShaderSource, void (*)(void *, GLuint, GLsizei, const char *const *, const GLint *))(ctx, sh, 1, &src, NULL);
    CALL(GLCompileShader, void (*)(void *, GLuint))(ctx, sh);
    CALL(GLGetShaderiv, void (*)(void *, GLuint, GLenum, GLint *))(ctx, sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        log[0] = 0;
        CALL(GLGetShaderInfoLog, void (*)(void *, GLuint, GLsizei, GLsizei *, char *))(ctx, sh, sizeof log, NULL, log);
        printf("compute: compile failed: %s\n", log);
        return 0;
    }
    prog = CALL(GLCreateProgram, GLuint (*)(void *))(ctx);
    CALL(GLAttachShader, void (*)(void *, GLuint, GLuint))(ctx, prog, sh);
    CALL(GLLinkProgram, void (*)(void *, GLuint))(ctx, prog);
    CALL(GLGetProgramiv, void (*)(void *, GLuint, GLenum, GLint *))(ctx, prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        log[0] = 0;
        CALL(GLGetProgramInfoLog, void (*)(void *, GLuint, GLsizei, GLsizei *, char *))(ctx, prog, sizeof log, NULL, log);
        printf("compute: link failed: %s\n", log);
        return 0;
    }
    CALL(GLGenBuffers, void (*)(void *, GLsizei, GLuint *))(ctx, 1, &buf);
    CALL(GLBindBuffer, void (*)(void *, GLenum, GLuint))(ctx, GL_SHADER_STORAGE_BUFFER, buf);
    CALL(GLBufferData, void (*)(void *, GLenum, GLsizeiptr, const void *, GLenum))(ctx, GL_SHADER_STORAGE_BUFFER, 256 * 4, NULL, GL_DYNAMIC_COPY);
    CALL(GLBindBufferBase, void (*)(void *, GLenum, GLuint, GLuint))(ctx, GL_SHADER_STORAGE_BUFFER, 0, buf);
    CALL(GLUseProgram, void (*)(void *, GLuint))(ctx, prog);
    CALL(GLDispatchCompute, void (*)(void *, GLuint, GLuint, GLuint))(ctx, 4, 1, 1);
    CALL(GLMemoryBarrier, void (*)(void *, GLbitfield))(ctx, GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    v = CALL(GLMapBufferRange, void *(*)(void *, GLenum, GLintptr, GLsizeiptr, GLbitfield))(ctx, GL_SHADER_STORAGE_BUFFER, 0, 256 * 4, GL_MAP_READ_BIT);
    if (!v) {
        printf("compute: glMapBufferRange gave NULL (error 0x%x)\n", (unsigned)CALL(GLGetError, GLenum (*)(void *))(ctx));
        return 0;
    }
    for (i = 0; i < 256; i++)
        if (v[i] != (GLuint)i * 3u + 1u) bad++;
    printf("compute: 4 x 64 invocations, %ld of 256 values right (v[255] = %lu)\n", (long)(256 - bad), (unsigned long)v[255]);
    CALL(GLUnmapBuffer, GLuint (*)(void *, GLenum))(ctx, GL_SHADER_STORAGE_BUFFER);
    return bad == 0;
}

int main(int argc, char **argv)
{
    int i, es2 = 0, window = 0, exts = 0, rc = RETURN_OK;
    ULONG err = 0, w = 0, h = 0;
    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "ES2")) es2 = 1;
        else if (!strcasecmp(argv[i], "WINDOW")) window = 1;
        else if (!strcasecmp(argv[i], "EXTENSIONS")) exts = 1;
    }
    if (!(OGLES3Base = OpenLibrary((CONST_STRPTR) "ogles3.library", 1))) {
        printf("OGLES3Check: no ogles3.library\n");
        return RETURN_FAIL;
    }
    D = get_dispatch();
    printf("ogles3.library %u.%u, %s", OGLES3Base->lib_Version, OGLES3Base->lib_Revision,
           OGLES3Base->lib_IdString ? (const char *)OGLES3Base->lib_IdString : "\n");
    if (!D || D->abiVersion != 1 || D->structSize < sizeof(struct dispatch)) {
        printf("OGLES3Check: dispatch table %p of another ABI\n", (void *)D);
        CloseLibrary(OGLES3Base);
        return RETURN_FAIL;
    }
    printf("dispatch: ABI %lu, %lu bytes, backend flags 0x%lx\n", (unsigned long)D->abiVersion, (unsigned long)D->structSize,
           (unsigned long)D->backendFlags);
    {
        struct TagItem tags[] = {
            { CCT_PS_ES_VERSION, es2 ? 20 : 31 },
            { window ? TAG_IGNORE : CCT_PS_STATE_ONLY, 1 },
            { CCT_PS_WIDTH, 320 }, { CCT_PS_HEIGHT, 240 },
            { CCT_GET_WIDTH, (ULONG)&w }, { CCT_GET_HEIGHT, (ULONG)&h },
            { TAG_DONE, 0 },
        };
        fflush(stdout);                 /* GL.module's own line (softpipe, and why) comes after ours */
        ctx = CALL(aglCreateContext2, void *(*)(ULONG *, struct TagItem *))(&err, tags);
    }
    if (!ctx) {
        printf("OGLES3Check: aglCreateContext2 failed, error %lu%s\n", (unsigned long)err,
               err == 2 ? " (no GL: GL.module's line above says why; ENV:GFOpenGPU/GLDriver gpu refuses softpipe)" : "");
        CloseLibrary(OGLES3Base);
        return RETURN_FAIL;
    }
    CALL(aglMakeCurrent, void (*)(void *))(ctx);
    printf("context: %lux%lu\nGL_VENDOR: %s\nGL_RENDERER: %s\nGL_VERSION: %s\nGL_SHADING_LANGUAGE_VERSION: %s\n",
           (unsigned long)w, (unsigned long)h, S(GL_VENDOR), S(GL_RENDERER), S(GL_VERSION), S(GL_SHADING_LANGUAGE_VERSION));
    /* Which driver Mesa got: its renderer names it ("virgl (...)", "softpipe") */
    {
        const char *r = S(GL_RENDERER);
        printf("driver: %s\n", !strncmp(r, "virgl", 5) ? "virgl (the host's graphics chip)"
                               : !strncmp(r, "softpipe", 8) ? "softpipe (the 68k; GL.module's line above says why)" : r);
    }
    if (!es2) {
        GLint n = geti(GL_NUM_EXTENSIONS);
        printf("GL_NUM_EXTENSIONS: %ld\n", (long)n);
        if (exts)
            for (i = 0; i < n; i++)
                printf("  %s\n", (const char *)CALL(GLGetStringi, const unsigned char *(*)(void *, GLenum, GLuint))(ctx, GL_EXTENSIONS, i));
        printf("GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS: %ld\n", (long)geti(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS));
        printf("GL_MAX_COMPUTE_WORK_GROUP_SIZE: %ld %ld %ld\n", (long)getii(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0),
               (long)getii(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1), (long)getii(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2));
        printf("GL_MAX_COMPUTE_WORK_GROUP_COUNT: %ld %ld %ld\n", (long)getii(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0),
               (long)getii(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1), (long)getii(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2));
        printf("GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS: %ld\n", (long)geti(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS));
        if (!compute_test()) rc = RETURN_WARN;
    } else if (exts) {
        printf("GL_EXTENSIONS: %s\n", S(GL_EXTENSIONS));
    }
    if (window) {
        for (i = 0; i < 4; i++) {
            CALL(GLClearColor, void (*)(void *, GLfloat, GLfloat, GLfloat, GLfloat))(ctx, 0.2f * i, 0.4f, 0.8f - 0.2f * i, 1.0f);
            CALL(GLClear, void (*)(void *, GLbitfield))(ctx, GL_COLOR_BUFFER_BIT);
            CALL(aglSwapBuffers, void (*)(void *))(ctx);
            Delay(25);
        }
    }
    printf("glGetError: 0x%x\n", (unsigned)CALL(GLGetError, GLenum (*)(void *))(ctx));
    CALL(aglDestroyContext, void (*)(void *))(ctx);
    CloseLibrary(OGLES3Base);
    return rc;
}
