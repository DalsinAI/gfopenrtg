/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GLInfo: what GL.module gives a GLES 3.1 port, written out to diff. For
 * virgl (the host's graphics chip, where opengpu.library carries it) and
 * then softpipe (the 68k), a context of each of GLES 2.0, 3.0, 3.1 and 3.2,
 * GL compat (the highest) and GL core 3.3, 4.3 and 4.5, on a small buffer
 * nobody sees: GL_VENDOR, GL_RENDERER, GL_VERSION, the GLSL version, every
 * extension (sorted), the ones the GLES demo shortlist needs, the limits
 * GLES 3.1 demos ask for and the compressed texture formats. Then how long
 * shaders take, timed with timer.device's E-clock: a tiny vertex and
 * fragment pair, a Shadertoy-style raymarcher (fragment, about 150 lines)
 * and a compute shader (about 100 lines), each compiled, linked and used
 * once (a draw or a dispatch and glFinish, where Mesa makes the code the
 * driver runs).
 *   GLInfo [CPU|GPU] [ONLY=<profile>] [NOTIME] [NOEXT] [TO=<file>]
 *     CPU: softpipe only; GPU: virgl only; ONLY=: one profile (es20, es30,
 *     es31, es32, compat, core33, core43, core45); NOTIME: no shader times;
 *     NOEXT: no extension list (the shortlist stays); TO=: a copy to a file.
 * Returns 0, or 20 when no display opened. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>

#include <exec/types.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>

#include <gla/gla_core.h>
#include <gla/os3/gla_virgl_os3.h>
#include <GL/gl.h>

/* GL 2.0-4.5 / GLES 2.0-3.2 names, here so no glext.h is needed. */
#define Q_NUM_EXTENSIONS                         0x821D
#define Q_SHADING_LANGUAGE_VERSION               0x8B8C
#define Q_MAX_COMPUTE_WORK_GROUP_INVOCATIONS     0x90EB
#define Q_MAX_COMPUTE_WORK_GROUP_COUNT           0x91BE
#define Q_MAX_COMPUTE_WORK_GROUP_SIZE            0x91BF
#define Q_MAX_COMPUTE_SHARED_MEMORY_SIZE         0x8262
#define Q_MAX_SHADER_STORAGE_BUFFER_BINDINGS     0x90DD
#define Q_MAX_SHADER_STORAGE_BLOCK_SIZE          0x90DE
#define Q_MAX_IMAGE_UNITS                        0x8F38
#define Q_MAX_3D_TEXTURE_SIZE                    0x8073
#define Q_MAX_ARRAY_TEXTURE_LAYERS               0x88FF
#define Q_MAX_SAMPLES                            0x8D57
#define Q_MAX_DRAW_BUFFERS                       0x8824
#define Q_MAX_UNIFORM_BLOCK_SIZE                 0x8A30
#define Q_NUM_COMPRESSED_TEXTURE_FORMATS         0x86A2
#define Q_COMPRESSED_TEXTURE_FORMATS             0x86A3
#define Q_VERTEX_SHADER                          0x8B31
#define Q_FRAGMENT_SHADER                        0x8B30
#define Q_COMPUTE_SHADER                         0x91B9
#define Q_COMPILE_STATUS                         0x8B81
#define Q_LINK_STATUS                            0x8B82
#define Q_ARRAY_BUFFER                           0x8892
#define Q_SHADER_STORAGE_BUFFER                  0x90D2
#define Q_STATIC_DRAW                            0x88E4
#define Q_DYNAMIC_COPY                           0x88EA
#define Q_SHADER_STORAGE_BARRIER_BIT             0x00002000

typedef const GLubyte *(*fn_getstringi)(GLenum, GLuint);
typedef void (*fn_getintegeri)(GLenum, GLuint, GLint *);
typedef GLuint (*fn_createshader)(GLenum);
typedef void (*fn_shadersource)(GLuint, GLsizei, const char *const *, const GLint *);
typedef void (*fn_uint)(GLuint);
typedef void (*fn_getiv)(GLuint, GLenum, GLint *);
typedef void (*fn_getlog)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (*fn_createprogram)(void);
typedef void (*fn_attach)(GLuint, GLuint);
typedef void (*fn_bindattrib)(GLuint, GLuint, const char *);
typedef GLint (*fn_getuniform)(GLuint, const char *);
typedef void (*fn_uniform2f)(GLint, GLfloat, GLfloat);
typedef void (*fn_uniform1f)(GLint, GLfloat);
typedef void (*fn_gen)(GLsizei, GLuint *);
typedef void (*fn_bindbuffer)(GLenum, GLuint);
typedef void (*fn_bindbufferbase)(GLenum, GLuint, GLuint);
typedef void (*fn_bufferdata)(GLenum, long, const void *, GLenum);
typedef void (*fn_attribptr)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void (*fn_dispatch)(GLuint, GLuint, GLuint);
typedef void (*fn_barrier)(GLbitfield);

struct Device *TimerBase;
static struct timerequest treq;
static int have_timer;
static FILE *out_file;

#define S 16
static void present(void *user, const struct gla_frame *f) { (void)user; (void)f; }

/* Every line goes to stdout and, with TO=, to the file. */
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (out_file) {
        va_start(ap, fmt);
        vfprintf(out_file, fmt, ap);
        va_end(ap);
    }
}

/* E-clock ticks; the times are worked out in integers (OS 3.2's ROM maths
 * libraries can leave the FPU in single precision). */
static unsigned long long ticks(void)
{
    struct EClockVal e;
    ReadEClock(&e);
    return ((unsigned long long)e.ev_hi << 32) | e.ev_lo;
}

static unsigned long eclock_hz(void)
{
    struct EClockVal e;
    return ReadEClock(&e);
}

/* "123.4 ms" from two tick counts, into buf. */
static const char *ms(unsigned long long t0, unsigned long long t1, char *buf)
{
    unsigned long long tenths = (t1 - t0) * 10000ULL / eclock_hz();
    sprintf(buf, "%lu.%lu ms", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
    return buf;
}

static void drain(void)
{
    int n = 0;
    while (glGetError() != GL_NO_ERROR && ++n < 16)
        ;
}

/* -1 when the context doesn't know e. */
static GLint geti(GLenum e)
{
    GLint v = -1;
    drain();
    glGetIntegerv(e, &v);
    if (glGetError() != GL_NO_ERROR) v = -1;
    return v;
}

static void say_limit(const char *name, GLint v)
{
    if (v < 0) say("  %-40s n/a\n", name);
    else say("  %-40s %ld\n", name, (long)v);
}

/* The extensions the GLES demo shortlist (gles31-demo-candidates.md) needs,
 * and the desktop ones that stand for GLES 3.1 and 3.2. */
static const char *const shortlist[] = {
    "GL_EXT_geometry_shader", "GL_OES_geometry_shader", "GL_EXT_tessellation_shader", "GL_OES_tessellation_shader",
    "GL_EXT_texture_buffer", "GL_KHR_texture_compression_astc_ldr", "GL_OES_texture_float_linear",
    "GL_EXT_color_buffer_float", "GL_EXT_shader_pixel_local_storage", "GL_KHR_debug",
    "GL_ARB_ES3_1_compatibility", "GL_ARB_ES3_2_compatibility", NULL
};

static const struct { GLint e; const char *name; } formats[] = {
    { 0x83F0, "COMPRESSED_RGB_S3TC_DXT1_EXT" }, { 0x83F1, "COMPRESSED_RGBA_S3TC_DXT1_EXT" },
    { 0x83F2, "COMPRESSED_RGBA_S3TC_DXT3_EXT" }, { 0x83F3, "COMPRESSED_RGBA_S3TC_DXT5_EXT" },
    { 0x8C4C, "COMPRESSED_SRGB_S3TC_DXT1_EXT" }, { 0x8C4D, "COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT" },
    { 0x8C4E, "COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT" }, { 0x8C4F, "COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT" },
    { 0x8D64, "ETC1_RGB8_OES" },
    { 0x9270, "COMPRESSED_R11_EAC" }, { 0x9271, "COMPRESSED_SIGNED_R11_EAC" },
    { 0x9272, "COMPRESSED_RG11_EAC" }, { 0x9273, "COMPRESSED_SIGNED_RG11_EAC" },
    { 0x9274, "COMPRESSED_RGB8_ETC2" }, { 0x9275, "COMPRESSED_SRGB8_ETC2" },
    { 0x9276, "COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2" }, { 0x9277, "COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2" },
    { 0x9278, "COMPRESSED_RGBA8_ETC2_EAC" }, { 0x9279, "COMPRESSED_SRGB8_ALPHA8_ETC2_EAC" },
    { 0x8DBB, "COMPRESSED_RED_RGTC1" }, { 0x8DBC, "COMPRESSED_SIGNED_RED_RGTC1" },
    { 0x8DBD, "COMPRESSED_RG_RGTC2" }, { 0x8DBE, "COMPRESSED_SIGNED_RG_RGTC2" },
    { 0x8E8C, "COMPRESSED_RGBA_BPTC_UNORM" }, { 0x8E8D, "COMPRESSED_SRGB_ALPHA_BPTC_UNORM" },
    { 0x8E8E, "COMPRESSED_RGB_BPTC_SIGNED_FLOAT" }, { 0x8E8F, "COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT" },
    { 0, NULL }
};

static const char *format_name(GLint e)
{
    int i;
    for (i = 0; formats[i].name; i++)
        if (formats[i].e == e) return formats[i].name;
    if (e >= 0x93B0 && e <= 0x93BD) return "COMPRESSED_RGBA_ASTC (LDR)";
    if (e >= 0x93D0 && e <= 0x93DD) return "COMPRESSED_SRGB8_ALPHA8_ASTC (LDR)";
    return "?";
}

/* ---- the shaders. Each is written once, for every GLSL the contexts
 * take: a header per language gives VIN/VOUT/FIN/FRAG and the version. */

static const char *const tiny_vs =
    "VIN vec2 pos;\n"
    "VOUT vec2 uv;\n"
    "void main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";

static const char *const tiny_fs =
    "FIN vec2 uv;\n"
    "void main() { FRAG = vec4(uv, 0.5, 1.0); }\n";

/* A Shadertoy-style raymarcher: four signed-distance shapes, normals by
 * central differences, soft shadows, ambient occlusion, fog and gamma.
 * Loops have constant bounds and break (GLSL ES 1.00 asks that). */
static const char *const march_fs =
    "uniform vec2 iResolution;\n"
    "uniform float iTime;\n"
    "FIN vec2 uv;\n"
    "\n"
    "float sdSphere(vec3 p, float r) { return length(p) - r; }\n"
    "\n"
    "float sdBox(vec3 p, vec3 b)\n"
    "{\n"
    "    vec3 q = abs(p) - b;\n"
    "    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);\n"
    "}\n"
    "\n"
    "float sdTorus(vec3 p, vec2 t)\n"
    "{\n"
    "    vec2 q = vec2(length(p.xz) - t.x, p.y);\n"
    "    return length(q) - t.y;\n"
    "}\n"
    "\n"
    "float sdPlane(vec3 p) { return p.y + 1.0; }\n"
    "\n"
    "float smin(float a, float b, float k)\n"
    "{\n"
    "    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);\n"
    "    return mix(b, a, h) - k * h * (1.0 - h);\n"
    "}\n"
    "\n"
    "mat2 rot(float a)\n"
    "{\n"
    "    float c = cos(a), s = sin(a);\n"
    "    return mat2(c, -s, s, c);\n"
    "}\n"
    "\n"
    "vec2 map(vec3 p)\n"
    "{\n"
    "    vec3 q = p;\n"
    "    q.xz = rot(iTime * 0.3) * q.xz;\n"
    "    float d1 = sdSphere(q - vec3(0.0, 0.2 * sin(iTime), 0.0), 0.6);\n"
    "    float d2 = sdBox(q - vec3(1.2, 0.0, 0.0), vec3(0.35));\n"
    "    vec3 tq = q + vec3(1.2, 0.0, 0.0);\n"
    "    tq.yz = rot(1.2) * tq.yz;\n"
    "    float d3 = sdTorus(tq, vec2(0.45, 0.15));\n"
    "    float d = smin(d1, smin(d2, d3, 0.3), 0.3);\n"
    "    float m = 1.0;\n"
    "    float dp = sdPlane(p);\n"
    "    if (dp < d) { d = dp; m = 2.0; }\n"
    "    return vec2(d, m);\n"
    "}\n"
    "\n"
    "vec3 normal(vec3 p)\n"
    "{\n"
    "    vec2 e = vec2(0.001, 0.0);\n"
    "    return normalize(vec3(map(p + e.xyy).x - map(p - e.xyy).x,\n"
    "                          map(p + e.yxy).x - map(p - e.yxy).x,\n"
    "                          map(p + e.yyx).x - map(p - e.yyx).x));\n"
    "}\n"
    "\n"
    "vec2 march(vec3 ro, vec3 rd)\n"
    "{\n"
    "    float t = 0.0;\n"
    "    float m = -1.0;\n"
    "    for (int i = 0; i < 96; i++) {\n"
    "        vec2 h = map(ro + rd * t);\n"
    "        if (h.x < 0.0005 * t) { m = h.y; break; }\n"
    "        t += h.x;\n"
    "        if (t > 30.0) break;\n"
    "    }\n"
    "    return vec2(t, m);\n"
    "}\n"
    "\n"
    "float shadow(vec3 ro, vec3 rd)\n"
    "{\n"
    "    float res = 1.0;\n"
    "    float t = 0.02;\n"
    "    for (int i = 0; i < 32; i++) {\n"
    "        float h = map(ro + rd * t).x;\n"
    "        res = min(res, 8.0 * h / t);\n"
    "        t += clamp(h, 0.02, 0.25);\n"
    "        if (res < 0.001 || t > 8.0) break;\n"
    "    }\n"
    "    return clamp(res, 0.0, 1.0);\n"
    "}\n"
    "\n"
    "float occlusion(vec3 p, vec3 n)\n"
    "{\n"
    "    float occ = 0.0;\n"
    "    float sca = 1.0;\n"
    "    for (int i = 0; i < 5; i++) {\n"
    "        float h = 0.01 + 0.12 * float(i) / 4.0;\n"
    "        float d = map(p + h * n).x;\n"
    "        occ += (h - d) * sca;\n"
    "        sca *= 0.95;\n"
    "    }\n"
    "    return clamp(1.0 - 3.0 * occ, 0.0, 1.0);\n"
    "}\n"
    "\n"
    "vec3 checker(vec3 p)\n"
    "{\n"
    "    float f = mod(floor(p.x * 2.0) + floor(p.z * 2.0), 2.0);\n"
    "    return mix(vec3(0.25), vec3(0.6), f);\n"
    "}\n"
    "\n"
    "vec3 sky(vec3 rd)\n"
    "{\n"
    "    vec3 c = mix(vec3(0.6, 0.75, 0.9), vec3(0.2, 0.35, 0.7), clamp(rd.y, 0.0, 1.0));\n"
    "    return c;\n"
    "}\n"
    "\n"
    "vec3 shade(vec3 ro, vec3 rd)\n"
    "{\n"
    "    vec2 h = march(ro, rd);\n"
    "    if (h.y < 0.0) return sky(rd);\n"
    "    vec3 p = ro + rd * h.x;\n"
    "    vec3 n = normal(p);\n"
    "    vec3 lig = normalize(vec3(0.6, 0.7, -0.4));\n"
    "    vec3 hal = normalize(lig - rd);\n"
    "    vec3 alb = h.y > 1.5 ? checker(p) : vec3(0.8, 0.35, 0.2);\n"
    "    float dif = clamp(dot(n, lig), 0.0, 1.0) * shadow(p, lig);\n"
    "    float spe = pow(clamp(dot(n, hal), 0.0, 1.0), 32.0) * dif;\n"
    "    float amb = 0.5 + 0.5 * n.y;\n"
    "    float occ = occlusion(p, n);\n"
    "    float fre = pow(clamp(1.0 + dot(n, rd), 0.0, 1.0), 2.0);\n"
    "    vec3 col = alb * (1.3 * dif * vec3(1.0, 0.9, 0.8) + 0.4 * amb * occ * vec3(0.4, 0.6, 1.0));\n"
    "    col += spe * vec3(1.0) + 0.2 * fre * occ * sky(reflect(rd, n));\n"
    "    col = mix(col, sky(rd), 1.0 - exp(-0.002 * h.x * h.x * h.x));\n"
    "    return col;\n"
    "}\n"
    "\n"
    "mat3 camera(vec3 ro, vec3 ta)\n"
    "{\n"
    "    vec3 w = normalize(ta - ro);\n"
    "    vec3 u = normalize(cross(w, vec3(0.0, 1.0, 0.0)));\n"
    "    vec3 v = cross(u, w);\n"
    "    return mat3(u, v, w);\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 p = (2.0 * uv * iResolution - iResolution) / iResolution.y;\n"
    "    vec3 ro = vec3(3.5 * sin(0.1 * iTime), 1.2, -3.5 * cos(0.1 * iTime));\n"
    "    vec3 ta = vec3(0.0, 0.0, 0.0);\n"
    "    mat3 ca = camera(ro, ta);\n"
    "    vec3 rd = ca * normalize(vec3(p, 1.8));\n"
    "    vec3 col = shade(ro, rd);\n"
    "    col = col / (1.0 + col);\n"
    "    col = pow(col, vec3(0.4545));\n"
    "    vec2 q = uv;\n"
    "    col *= 0.5 + 0.5 * pow(16.0 * q.x * q.y * (1.0 - q.x) * (1.0 - q.y), 0.15);\n"
    "    FRAG = vec4(col, 1.0);\n"
    "}\n";

/* A compute shader the way GLES 3.1 demos write them: particles moved
 * through a curl-noise field, each group summing its energy in shared
 * memory with barriers, written to shader storage buffers. */
static const char *const particles_cs =
    "layout(local_size_x = 64) in;\n"
    "\n"
    "struct Particle {\n"
    "    vec4 pos;\n"
    "    vec4 vel;\n"
    "};\n"
    "\n"
    "layout(std430, binding = 0) buffer Particles { Particle p[]; };\n"
    "layout(std430, binding = 1) buffer Energy { float energy[]; };\n"
    "\n"
    "uniform float dt;\n"
    "uniform float iTime;\n"
    "\n"
    "shared float sum[64];\n"
    "\n"
    "float hash(vec3 q)\n"
    "{\n"
    "    q = fract(q * 0.3183099 + 0.1);\n"
    "    q *= 17.0;\n"
    "    return fract(q.x * q.y * q.z * (q.x + q.y + q.z));\n"
    "}\n"
    "\n"
    "float noise(vec3 x)\n"
    "{\n"
    "    vec3 i = floor(x);\n"
    "    vec3 f = fract(x);\n"
    "    f = f * f * (3.0 - 2.0 * f);\n"
    "    return mix(mix(mix(hash(i + vec3(0, 0, 0)), hash(i + vec3(1, 0, 0)), f.x),\n"
    "                   mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),\n"
    "               mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),\n"
    "                   mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);\n"
    "}\n"
    "\n"
    "vec3 noise3(vec3 x)\n"
    "{\n"
    "    return vec3(noise(x), noise(x + vec3(31.4, 15.9, 26.5)), noise(x + vec3(-12.7, 81.3, 4.4)));\n"
    "}\n"
    "\n"
    "vec3 curl(vec3 x)\n"
    "{\n"
    "    const float e = 0.1;\n"
    "    vec3 dx = vec3(e, 0.0, 0.0);\n"
    "    vec3 dy = vec3(0.0, e, 0.0);\n"
    "    vec3 dz = vec3(0.0, 0.0, e);\n"
    "    vec3 px0 = noise3(x - dx), px1 = noise3(x + dx);\n"
    "    vec3 py0 = noise3(x - dy), py1 = noise3(x + dy);\n"
    "    vec3 pz0 = noise3(x - dz), pz1 = noise3(x + dz);\n"
    "    float cx = (py1.z - py0.z) - (pz1.y - pz0.y);\n"
    "    float cy = (pz1.x - pz0.x) - (px1.z - px0.z);\n"
    "    float cz = (px1.y - px0.y) - (py1.x - py0.x);\n"
    "    return vec3(cx, cy, cz) / (2.0 * e);\n"
    "}\n"
    "\n"
    "vec3 attract(vec3 pos)\n"
    "{\n"
    "    vec3 c = vec3(sin(iTime), 0.0, cos(iTime)) * 2.0;\n"
    "    vec3 d = c - pos;\n"
    "    float r2 = max(dot(d, d), 0.05);\n"
    "    return d * (1.0 / (r2 * sqrt(r2)));\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "    uint id = gl_GlobalInvocationID.x;\n"
    "    uint li = gl_LocalInvocationIndex;\n"
    "    vec3 pos = p[id].pos.xyz;\n"
    "    vec3 vel = p[id].vel.xyz;\n"
    "    float life = p[id].pos.w;\n"
    "\n"
    "    vec3 acc = curl(pos * 0.5 + vec3(0.0, iTime * 0.1, 0.0)) * 0.8;\n"
    "    acc += attract(pos);\n"
    "    acc -= vel * 0.2;\n"
    "    vel += acc * dt;\n"
    "    pos += vel * dt;\n"
    "    life -= dt;\n"
    "    if (life <= 0.0 || dot(pos, pos) > 400.0) {\n"
    "        float h = hash(vec3(float(id), iTime, 1.0));\n"
    "        pos = vec3(h - 0.5, hash(vec3(h, 2.0, 3.0)) - 0.5, hash(vec3(3.0, h, 2.0)) - 0.5);\n"
    "        vel = vec3(0.0);\n"
    "        life = 2.0 + 4.0 * h;\n"
    "    }\n"
    "    p[id].pos = vec4(pos, life);\n"
    "    p[id].vel = vec4(vel, 0.0);\n"
    "\n"
    "    sum[li] = 0.5 * dot(vel, vel);\n"
    "    memoryBarrierShared();\n"
    "    barrier();\n"
    /* GLSL ES 3.10 takes barrier() only outside control flow: no loop. */
    "    if (li < 32u) sum[li] += sum[li + 32u];\n"
    "    memoryBarrierShared();\n"
    "    barrier();\n"
    "    if (li < 16u) sum[li] += sum[li + 16u];\n"
    "    memoryBarrierShared();\n"
    "    barrier();\n"
    "    if (li < 8u) sum[li] += sum[li + 8u];\n"
    "    memoryBarrierShared();\n"
    "    barrier();\n"
    "    if (li < 4u) sum[li] += sum[li + 4u];\n"
    "    memoryBarrierShared();\n"
    "    barrier();\n"
    "    if (li < 2u) sum[li] += sum[li + 2u];\n"
    "    memoryBarrierShared();\n"
    "    barrier();\n"
    "    if (li == 0u) energy[gl_WorkGroupID.x] = sum[0];\n"
    "}\n";

static int lines_of(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (*s == '\n') n++;
    return n;
}

struct glsl {
    const char *vs, *fs;    /* the headers; 0 when there is no GLSL */
    const char *cs;         /* 0 when there is no compute */
};

/* The GLSL headers for a context: ES "OpenGL ES 3.1 ...", desktop "4.3 (...)". */
static void pick_glsl(int es, int major, int minor, int compute, struct glsl *g)
{
    static const char v100[] = "#version 100\nprecision highp float;\n#define VIN attribute\n#define VOUT varying\n";
    static const char f100[] = "#version 100\nprecision highp float;\n#define FIN varying\n#define FRAG gl_FragColor\n";
    static const char v300[] = "#version 300 es\nprecision highp float;\n#define VIN in\n#define VOUT out\n";
    static const char f300[] = "#version 300 es\nprecision highp float;\n#define FIN in\nout vec4 fragColor;\n#define FRAG fragColor\n";
    static const char c310[] = "#version 310 es\nprecision highp float;\n";
    static const char v120[] = "#version 120\n#define VIN attribute\n#define VOUT varying\n";
    static const char f120[] = "#version 120\n#define FIN varying\n#define FRAG gl_FragColor\n";
    static const char v330[] = "#version 330 core\n#define VIN in\n#define VOUT out\n";
    static const char f330[] = "#version 330 core\n#define FIN in\nout vec4 fragColor;\n#define FRAG fragColor\n";
    static const char c430[] = "#version 430\n";
    int v = major * 10 + minor;

    g->vs = g->fs = g->cs = NULL;
    if (es) {
        if (v >= 30) { g->vs = v300; g->fs = f300; }
        else if (v >= 20) { g->vs = v100; g->fs = f100; }
        if (v >= 31 && compute) g->cs = c310;
    } else {
        if (v >= 33) { g->vs = v330; g->fs = f330; }
        else if (v >= 21) { g->vs = v120; g->fs = f120; }
        if (v >= 43 && compute) g->cs = c430;
    }
}

static fn_createshader createshader;
static fn_shadersource shadersource;
static fn_uint compileshader, linkprogram, useprogram, deleteshader, deleteprogram;
static fn_getiv getshaderiv, getprogramiv;
static fn_getlog getshaderlog, getprogramlog;
static fn_createprogram createprogram;
static fn_attach attach;
static fn_bindattrib bindattrib;
static fn_getuniform getuniform;
static fn_uniform2f uniform2f;
static fn_uniform1f uniform1f;
static fn_gen genbuffers, genvertexarrays;
static fn_uint bindvertexarray, enableattrib;
static fn_bindbuffer bindbuffer;
static fn_bindbufferbase bindbufferbase;
static fn_bufferdata bufferdata;
static fn_attribptr attribptr;
static fn_dispatch dispatch;
static fn_barrier membarrier;

static int load_calls(void)
{
    createshader = (fn_createshader)gla_get_proc_address("glCreateShader");
    shadersource = (fn_shadersource)gla_get_proc_address("glShaderSource");
    compileshader = (fn_uint)gla_get_proc_address("glCompileShader");
    deleteshader = (fn_uint)gla_get_proc_address("glDeleteShader");
    getshaderiv = (fn_getiv)gla_get_proc_address("glGetShaderiv");
    getshaderlog = (fn_getlog)gla_get_proc_address("glGetShaderInfoLog");
    createprogram = (fn_createprogram)gla_get_proc_address("glCreateProgram");
    deleteprogram = (fn_uint)gla_get_proc_address("glDeleteProgram");
    attach = (fn_attach)gla_get_proc_address("glAttachShader");
    bindattrib = (fn_bindattrib)gla_get_proc_address("glBindAttribLocation");
    linkprogram = (fn_uint)gla_get_proc_address("glLinkProgram");
    getprogramiv = (fn_getiv)gla_get_proc_address("glGetProgramiv");
    getprogramlog = (fn_getlog)gla_get_proc_address("glGetProgramInfoLog");
    useprogram = (fn_uint)gla_get_proc_address("glUseProgram");
    getuniform = (fn_getuniform)gla_get_proc_address("glGetUniformLocation");
    uniform2f = (fn_uniform2f)gla_get_proc_address("glUniform2f");
    uniform1f = (fn_uniform1f)gla_get_proc_address("glUniform1f");
    genbuffers = (fn_gen)gla_get_proc_address("glGenBuffers");
    genvertexarrays = (fn_gen)gla_get_proc_address("glGenVertexArrays");
    bindvertexarray = (fn_uint)gla_get_proc_address("glBindVertexArray");
    enableattrib = (fn_uint)gla_get_proc_address("glEnableVertexAttribArray");
    bindbuffer = (fn_bindbuffer)gla_get_proc_address("glBindBuffer");
    bindbufferbase = (fn_bindbufferbase)gla_get_proc_address("glBindBufferBase");
    bufferdata = (fn_bufferdata)gla_get_proc_address("glBufferData");
    attribptr = (fn_attribptr)gla_get_proc_address("glVertexAttribPointer");
    dispatch = (fn_dispatch)gla_get_proc_address("glDispatchCompute");
    membarrier = (fn_barrier)gla_get_proc_address("glMemoryBarrier");
    return createshader && shadersource && compileshader && getshaderiv && createprogram && attach && bindattrib
        && linkprogram && getprogramiv && useprogram && getuniform && uniform2f && uniform1f && genbuffers
        && enableattrib && bindbuffer && bufferdata && attribptr;
}

/* The first line of a compile or link log. */
static void say_log(const char *what, const char *log)
{
    char line[160];
    int i;
    for (i = 0; log[i] && log[i] != '\n' && i < (int)sizeof line - 1; i++) line[i] = log[i];
    line[i] = 0;
    say("    %s: %s\n", what, line[0] ? line : "(no log)");
}

/* Compiles a shader from header + body; 0 when it didn't compile. *t gets the time. */
static GLuint compile(GLenum kind, const char *head, const char *body, unsigned long long *t)
{
    const char *src[2];
    char log[512];
    unsigned long long t0;
    GLint ok = 0;
    GLuint sh;

    src[0] = head;
    src[1] = body;
    t0 = ticks();
    sh = createshader(kind);
    shadersource(sh, 2, src, NULL);
    compileshader(sh);
    getshaderiv(sh, Q_COMPILE_STATUS, &ok);
    *t = ticks() - t0;
    if (!ok) {
        log[0] = 0;
        if (getshaderlog) getshaderlog(sh, sizeof log, NULL, log);
        say_log("didn't compile", log);
        if (deleteshader) deleteshader(sh);
        return 0;
    }
    return sh;
}

static GLuint link(GLuint a, GLuint b, unsigned long long *t)
{
    char log[512];
    unsigned long long t0 = ticks();
    GLint ok = 0;
    GLuint prog = createprogram();

    attach(prog, a);
    if (b) attach(prog, b);
    if (b) bindattrib(prog, 0, "pos");
    linkprogram(prog);
    getprogramiv(prog, Q_LINK_STATUS, &ok);
    *t = ticks() - t0;
    if (!ok) {
        log[0] = 0;
        if (getprogramlog) getprogramlog(prog, sizeof log, NULL, log);
        say_log("didn't link", log);
        if (deleteprogram) deleteprogram(prog);
        return 0;
    }
    return prog;
}

static void say_time(const char *what, unsigned long long t)
{
    char buf[32];
    say("    %-28s %s\n", what, ms(0, t, buf));
}

/* One vertex and fragment pair, compiled, linked and drawn once over the buffer. */
static void time_pair(const char *name, const struct glsl *g, const char *vs_body, const char *fs_body, GLuint vbo)
{
    unsigned long long tv, tf, tl, t0, td;
    GLuint vs, fs, prog;
    GLint loc;

    say("  %s (vertex %d lines, fragment %d lines)\n", name, lines_of(g->vs) + lines_of(vs_body),
        lines_of(g->fs) + lines_of(fs_body));
    vs = compile(Q_VERTEX_SHADER, g->vs, vs_body, &tv);
    if (!vs) return;
    say_time("vertex compile", tv);
    fs = compile(Q_FRAGMENT_SHADER, g->fs, fs_body, &tf);
    if (!fs) return;
    say_time("fragment compile", tf);
    prog = link(vs, fs, &tl);
    if (!prog) return;
    say_time("link", tl);
    drain();
    t0 = ticks();
    useprogram(prog);
    if ((loc = getuniform(prog, "iResolution")) >= 0) uniform2f(loc, (GLfloat)S, (GLfloat)S);
    if ((loc = getuniform(prog, "iTime")) >= 0) uniform1f(loc, 1.0f);
    bindbuffer(Q_ARRAY_BUFFER, vbo);
    enableattrib(0);
    attribptr(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    td = ticks() - t0;
    say_time("first draw (16x16) + finish", td);
    if (glGetError() != GL_NO_ERROR) say("    the draw gave a GL error\n");
    useprogram(0);
    if (deleteprogram) deleteprogram(prog);
    if (deleteshader) { deleteshader(vs); deleteshader(fs); }
}

static void time_compute(const struct glsl *g)
{
    unsigned long long tc, tl, t0, td;
    GLuint cs, prog, buf[2];
    GLint loc;

    say("  compute, particles (%d lines)\n", lines_of(g->cs) + lines_of(particles_cs));
    if (!dispatch || !membarrier || !bindbufferbase) {
        say("    GL.module has no glDispatchCompute\n");
        return;
    }
    cs = compile(Q_COMPUTE_SHADER, g->cs, particles_cs, &tc);
    if (!cs) return;
    say_time("compute compile", tc);
    prog = link(cs, 0, &tl);
    if (!prog) return;
    say_time("link", tl);
    genbuffers(2, buf);
    bindbuffer(Q_SHADER_STORAGE_BUFFER, buf[0]);
    bufferdata(Q_SHADER_STORAGE_BUFFER, 64 * 32, NULL, Q_DYNAMIC_COPY);
    bindbufferbase(Q_SHADER_STORAGE_BUFFER, 0, buf[0]);
    bindbuffer(Q_SHADER_STORAGE_BUFFER, buf[1]);
    bufferdata(Q_SHADER_STORAGE_BUFFER, 4, NULL, Q_DYNAMIC_COPY);
    bindbufferbase(Q_SHADER_STORAGE_BUFFER, 1, buf[1]);
    drain();
    t0 = ticks();
    useprogram(prog);
    if ((loc = getuniform(prog, "dt")) >= 0) uniform1f(loc, 0.016f);
    if ((loc = getuniform(prog, "iTime")) >= 0) uniform1f(loc, 1.0f);
    dispatch(1, 1, 1);
    membarrier(Q_SHADER_STORAGE_BARRIER_BIT);
    glFinish();
    td = ticks() - t0;
    say_time("first dispatch (64) + finish", td);
    if (glGetError() != GL_NO_ERROR) say("    the dispatch gave a GL error\n");
    useprogram(0);
    if (deleteprogram) deleteprogram(prog);
    if (deleteshader) deleteshader(cs);
}

static void shader_times(int es, int major, int minor, int compute)
{
    static const GLfloat tri[6] = { -1, -1, 3, -1, -1, 3 };
    struct glsl g;
    GLuint vbo, vao = 0;

    pick_glsl(es, major, minor, compute, &g);
    say(" shader times\n");
    if (!have_timer) { say("  no timer.device\n"); return; }
    if (!g.vs) { say("  none: no GLSL in this context\n"); return; }
    if (!load_calls()) { say("  a shader call is missing from GL.module\n"); return; }
    /* GL core and GLES 3 draw from a vertex array object; ES 2.0 has none. */
    if (!(es && major < 3) && genvertexarrays && bindvertexarray) {
        genvertexarrays(1, &vao);
        bindvertexarray(vao);
    }
    genbuffers(1, &vbo);
    bindbuffer(Q_ARRAY_BUFFER, vbo);
    bufferdata(Q_ARRAY_BUFFER, sizeof tri, tri, Q_STATIC_DRAW);
    glViewport(0, 0, S, S);
    time_pair("tiny pair", &g, tiny_vs, tiny_fs, vbo);
    time_pair("raymarcher", &g, tiny_vs, march_fs, vbo);
    if (g.cs) time_compute(&g);
    else say("  compute: none in this context\n");
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* The context's extensions, sorted: glGetStringi from GL 3.0 and GLES 3.0,
 * else GL_EXTENSIONS split up. *store is freed by the caller. */
static int extensions(int gl3, const char ***list, char **store)
{
    fn_getstringi getstringi = (fn_getstringi)gla_get_proc_address("glGetStringi");
    const char **l = NULL;
    int n = 0, i;

    *store = NULL;
    if (gl3 && getstringi) {
        GLint count = geti(Q_NUM_EXTENSIONS);
        if (count > 0 && (l = (const char **)malloc(sizeof *l * (size_t)count))) {
            for (i = 0; i < count; i++) {
                const char *e = (const char *)getstringi(GL_EXTENSIONS, (GLuint)i);
                if (e) l[n++] = e;
            }
        }
    } else {
        const char *all = (const char *)glGetString(GL_EXTENSIONS);
        char *s, *p;
        int words = 1;
        if (all && (s = (char *)malloc(strlen(all) + 1))) {
            strcpy(s, all);
            for (p = s; *p; p++)
                if (*p == ' ') words++;
            if ((l = (const char **)malloc(sizeof *l * (size_t)words))) {
                for (p = strtok(s, " "); p; p = strtok(NULL, " ")) l[n++] = p;
            }
            *store = s;
        }
    }
    if (n) qsort(l, (size_t)n, sizeof *l, cmp_str);
    *list = l;
    return n;
}

static int has(const char **l, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(l[i], name)) return 1;
    return 0;
}

struct profile {
    const char *key, *name;
    int profile, major, minor;
    int expect_fail;
};

static const struct profile profiles[] = {
    { "es20", "GLES 2.0", GLA_ES2, 2, 0, 0 },
    { "es30", "GLES 3.0", GLA_ES2, 3, 0, 0 },
    { "es31", "GLES 3.1", GLA_ES2, 3, 1, 0 },
    { "es32", "GLES 3.2", GLA_ES2, 3, 2, 1 },
    { "compat", "GL compat (highest)", GLA_COMPAT, 0, 0, 0 },
    { "core33", "GL core 3.3", GLA_CORE, 3, 3, 0 },
    { "core43", "GL core 4.3", GLA_CORE, 4, 3, 0 },
    { "core45", "GL core 4.5", GLA_CORE, 4, 5, 1 },
};
#define NPROFILES (int)(sizeof profiles / sizeof profiles[0])

static void probe(struct gla_display *d, const struct profile *pr, int timing, int noext)
{
    struct gla_config cfg = { 0, 0, 0, 0, 0, 0, 0 };
    struct gla_present hook = { present, NULL };
    struct gla_context *c;
    struct gla_buffer *b;
    fn_getintegeri getintegeri;
    const char *version, *glsl, **ext;
    char *store;
    const char *const *w;
    GLint cnt[3] = { -1, -1, -1 }, sz[3] = { -1, -1, -1 }, nfmt;
    int i, n, es = pr->profile == GLA_ES2, major = 0, minor = 0, compute;

    say("\n== %s: %s\n", gla_display_driver(d), pr->name);
    cfg.profile = pr->profile;
    cfg.major = pr->major;
    cfg.minor = pr->minor;
    cfg.depth = 24;
    cfg.stencil = 8;
    c = gla_context_create(d, &cfg, NULL);
    b = c ? gla_buffer_create(d, &cfg, S, S, &hook) : NULL;
    if (!b || !gla_make_current(c, b)) {
        say(" no context%s\n", pr->expect_fail ? " (expected)" : "");
        if (b) gla_buffer_destroy(b);
        if (c) gla_context_destroy(c);
        return;
    }
    version = (const char *)glGetString(GL_VERSION);
    glsl = (const char *)glGetString(Q_SHADING_LANGUAGE_VERSION);
    if (version) {
        const char *v = version;
        if (!strncmp(v, "OpenGL ES ", 10)) v += 10;
        sscanf(v, "%d.%d", &major, &minor);
    }
    say(" GL_VENDOR                   %s\n", (const char *)glGetString(GL_VENDOR));
    say(" GL_RENDERER                 %s\n", (const char *)glGetString(GL_RENDERER));
    say(" GL_VERSION                  %s\n", version ? version : "(none)");
    say(" GL_SHADING_LANGUAGE_VERSION %s\n", glsl ? glsl : "(none)");
    if (pr->expect_fail) say(" (made, though this one was expected to fail)\n");

    n = extensions(major >= 3, &ext, &store);
    say(" shortlist extensions\n");
    for (w = shortlist; *w; w++) say("  %-40s %s\n", *w, has(ext, n, *w) ? "yes" : "no");

    say(" limits\n");
    getintegeri = (fn_getintegeri)gla_get_proc_address("glGetIntegeri_v");
    if (getintegeri && ((es && major * 10 + minor >= 31) || (!es && major * 10 + minor >= 43)))
        for (i = 0; i < 3; i++) {
            drain();
            getintegeri(Q_MAX_COMPUTE_WORK_GROUP_COUNT, (GLuint)i, &cnt[i]);
            getintegeri(Q_MAX_COMPUTE_WORK_GROUP_SIZE, (GLuint)i, &sz[i]);
            if (glGetError() != GL_NO_ERROR) cnt[i] = sz[i] = -1;
        }
    if (cnt[0] < 0) say("  %-40s n/a\n  %-40s n/a\n", "GL_MAX_COMPUTE_WORK_GROUP_COUNT", "GL_MAX_COMPUTE_WORK_GROUP_SIZE");
    else {
        say("  %-40s %ld %ld %ld\n", "GL_MAX_COMPUTE_WORK_GROUP_COUNT", (long)cnt[0], (long)cnt[1], (long)cnt[2]);
        say("  %-40s %ld %ld %ld\n", "GL_MAX_COMPUTE_WORK_GROUP_SIZE", (long)sz[0], (long)sz[1], (long)sz[2]);
    }
    compute = geti(Q_MAX_COMPUTE_WORK_GROUP_INVOCATIONS) > 0;
    say_limit("GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS", geti(Q_MAX_COMPUTE_WORK_GROUP_INVOCATIONS));
    say_limit("GL_MAX_COMPUTE_SHARED_MEMORY_SIZE", geti(Q_MAX_COMPUTE_SHARED_MEMORY_SIZE));
    say_limit("GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS", geti(Q_MAX_SHADER_STORAGE_BUFFER_BINDINGS));
    say_limit("GL_MAX_SHADER_STORAGE_BLOCK_SIZE", geti(Q_MAX_SHADER_STORAGE_BLOCK_SIZE));
    say_limit("GL_MAX_IMAGE_UNITS", geti(Q_MAX_IMAGE_UNITS));
    say_limit("GL_MAX_TEXTURE_SIZE", geti(GL_MAX_TEXTURE_SIZE));
    say_limit("GL_MAX_3D_TEXTURE_SIZE", geti(Q_MAX_3D_TEXTURE_SIZE));
    say_limit("GL_MAX_ARRAY_TEXTURE_LAYERS", geti(Q_MAX_ARRAY_TEXTURE_LAYERS));
    say_limit("GL_MAX_SAMPLES", geti(Q_MAX_SAMPLES));
    say_limit("GL_MAX_DRAW_BUFFERS", geti(Q_MAX_DRAW_BUFFERS));
    say_limit("GL_MAX_UNIFORM_BLOCK_SIZE", geti(Q_MAX_UNIFORM_BLOCK_SIZE));

    nfmt = geti(Q_NUM_COMPRESSED_TEXTURE_FORMATS);
    say(" compressed texture formats: %ld\n", (long)(nfmt < 0 ? 0 : nfmt));
    if (nfmt > 0) {
        GLint *f = (GLint *)malloc(sizeof *f * (size_t)nfmt);
        if (f) {
            drain();
            glGetIntegerv(Q_COMPRESSED_TEXTURE_FORMATS, f);
            for (i = 0; i < nfmt; i++) say("  0x%04lx %s\n", (unsigned long)f[i], format_name(f[i]));
            free(f);
        }
    }

    say(" extensions: %d\n", n);
    if (!noext)
        for (i = 0; i < n; i++) say("  %s\n", ext[i]);
    free(ext);
    free(store);

    if (timing) shader_times(es, major, minor, compute);

    gla_make_current(NULL, NULL);
    gla_buffer_destroy(b);
    gla_context_destroy(c);
}

static void probe_display(struct gla_display *d, const char *only, int timing, int noext)
{
    int i;
    say("\n==== display %s: GL %d (compat), %d (core), GLES %d\n", gla_display_driver(d),
        gla_display_version(d, GLA_COMPAT), gla_display_version(d, GLA_CORE), gla_display_version(d, GLA_ES2));
    for (i = 0; i < NPROFILES; i++)
        if (!only || !strcasecmp(only, profiles[i].key)) probe(d, &profiles[i], timing, noext);
}

int main(int argc, char **argv)
{
    struct gla_display *d;
    const char *only = NULL, *to = NULL;
    int i, cpu = 1, gpu = 1, timing = 1, noext = 0, opened = 0;

    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "CPU")) gpu = 0;
        else if (!strcasecmp(argv[i], "GPU")) cpu = 0;
        else if (!strcasecmp(argv[i], "NOTIME")) timing = 0;
        else if (!strcasecmp(argv[i], "NOEXT")) noext = 1;
        else if (!strncasecmp(argv[i], "ONLY=", 5)) only = argv[i] + 5;
        else if (!strncasecmp(argv[i], "TO=", 3)) to = argv[i] + 3;
        else if (!strcasecmp(argv[i], "TO") && i + 1 < argc) to = argv[++i];
        else {
            printf("GLInfo [CPU|GPU] [ONLY=es20|es30|es31|es32|compat|core33|core43|core45] [NOTIME] [NOEXT] [TO=file]\n");
            return 5;
        }
    }
    if (to && !(out_file = fopen(to, "w"))) {
        printf("GLInfo: can't write %s\n", to);
        return 20;
    }
    if (!OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&treq, 0)) {
        TimerBase = treq.tr_node.io_Device;
        have_timer = 1;
    }
    say("GLInfo 1.0\n");
    if (have_timer) say("E-clock %lu Hz\n", eclock_hz());

    if (gpu) {
        struct gla_virgl_transport t;
        d = gla_os3_virgl_transport(&t) ? gla_display_create_virgl(&t) : NULL;
        if (d && strcmp(gla_display_driver(d), "virgl")) {
            gla_display_destroy(d);
            d = NULL;
        }
        if (d) {
            probe_display(d, only, timing, noext);
            gla_display_destroy(d);
            opened++;
        } else
            say("\n==== display virgl: none here (opengpu.library carries no OGPU_OP_VIRGL)\n");
    }
    if (cpu) {
        d = gla_display_create();
        if (d) {
            probe_display(d, only, timing, noext);
            gla_display_destroy(d);
            opened++;
        } else
            say("\n==== display softpipe: couldn't open\n");
    }
    say("\nGLInfo: done\n");
    if (have_timer) CloseDevice((struct IORequest *)&treq);
    if (out_file) fclose(out_file);
    return opened ? 0 : 20;
}
