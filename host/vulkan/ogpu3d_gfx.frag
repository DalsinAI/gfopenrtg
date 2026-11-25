// Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
// SPDX-License-Identifier: MIT
//
// Stream3D's TRIANGLES on the GPU's own pipeline (ogpu3d_gfx.c): the
// fragment shader. ogpu_3d.c's pixel in integers, from the colour to the
// alpha test (texture environment, specular, fog), so only interpolation
// and the texture unit's filtering differ from the core; the depth test,
// blend and colour mask are the pipeline's own.
#version 450
layout(push_constant) uniform P {
    vec2 scale;
    int flags, aref;
    ivec4 cur, envc, fogc;
    vec4 fogp;                          // fog start, end, density
} pc;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec4 v_col;      // times q (ogpu3d_gfx.vert)
layout(location = 1) in vec4 v_spec;     // times q
layout(location = 2) in vec2 v_uv;
layout(location = 3) flat in float v_lod;
layout(location = 4) in vec2 v_fogw;     // fog w times q, q
layout(location = 0) out vec4 o_col;

// flags (ogpu3d_gfx.c's GF_)
const int F_TEX = 1, F_GOURAUD = 2, F_SPEC = 4, F_Q565 = 8;
// bits 4-6 fog mode (stream3d.h's OGPU_FOG_), 8-11 env, 12-15 base, 16-19 alpha function

int m255(int x, int y) { int t = x * y + 128; return (t + (t >> 8)) >> 8; }
ivec4 unorm8(vec4 c) { return clamp(ivec4(floor(c * 255.0 + 0.004)), 0, 255); }
bool cmp(int func, int a, int b) {
    if (func == 1) return false;
    if (func == 2) return a < b;
    if (func == 3) return a >= b;
    if (func == 4) return a <= b;
    if (func == 5) return a > b;
    if (func == 6) return a != b;
    if (func == 7) return a == b;
    return true;
}

void main() {
    int fl = pc.flags;
    float rq = 1.0 / v_fogw.y;                  // back to linear in screen space
    vec4 col = v_col * rq, spec = v_spec * rq;
    ivec4 f = (fl & F_GOURAUD) != 0 ? unorm8(col) : pc.cur;
    if ((fl & F_TEX) != 0) {
        ivec4 T = ivec4(textureLod(tex, v_uv, v_lod) * 255.0 + 0.5);
        int env = (fl >> 8) & 15, base = (fl >> 12) & 15;
        bool has_c = true, has_a = true;
        if (base == 1) has_a = false;                                   // RGB
        else if (base == 2) has_c = false;                              // ALPHA
        else if (base == 3) { T.gb = T.rr; has_a = false; }             // LUMINANCE
        else if (base == 4) T.gb = T.rr;                                // LUM_ALPHA
        else if (base == 5) T = T.rrrr;                                 // INTENSITY
        ivec4 c = pc.envc;
        if (env == 1) {                                                 // REPLACE
            if (has_c) f.rgb = T.rgb;
            if (has_a) f.a = T.a;
        } else if (env == 2) {                                          // DECAL
            if (base == 1) f.rgb = T.rgb;
            else if (base == 0)
                f.rgb = ivec3(m255(f.r, 255 - T.a) + m255(T.r, T.a), m255(f.g, 255 - T.a) + m255(T.g, T.a),
                              m255(f.b, 255 - T.a) + m255(T.b, T.a));
        } else if (env == 3) {                                          // MODULATE
            if (has_c) f.rgb = ivec3(m255(f.r, T.r), m255(f.g, T.g), m255(f.b, T.b));
            if (has_a) f.a = m255(f.a, T.a);
        } else if (env == 4) {                                          // BLEND
            if (has_c)
                f.rgb = ivec3(m255(f.r, 255 - T.r) + m255(c.r, T.r), m255(f.g, 255 - T.g) + m255(c.g, T.g),
                              m255(f.b, 255 - T.b) + m255(c.b, T.b));
            if (base == 5) f.a = m255(f.a, 255 - T.a) + m255(c.a, T.a);
            else if (has_a) f.a = m255(f.a, T.a);
        } else if (env == 5) {                                          // ADD
            if (has_c) f.rgb = clamp(f.rgb + T.rgb, 0, 255);
            if (has_a) f.a = m255(f.a, T.a);
        } else if (env == 6) {                                          // SUB
            if (has_c) f.rgb = clamp(f.rgb - T.rgb, 0, 255);
            if (has_a) f.a = m255(f.a, T.a);
        }
    }
    if ((fl & F_SPEC) != 0) f.rgb = clamp(f.rgb + unorm8(spec).rgb, 0, 255);
    int fog = (fl >> 4) & 7;
    if (fog != 0) {
        int ff;
        if (fog == 4) ff = unorm8(spec).a;                            // VERTEX
        else {
            // the core's table: w at index floor(w * 1024), 1024 entries over 0..1
            float w = clamp(floor(v_fogw.x * rq * 1024.0), 0.0, 1023.0) / 1023.0, d = 1.0 - w;
            if (fog == 1) {                                             // LINEAR
                if (pc.fogp.x == pc.fogp.y) ff = w >= pc.fogp.x ? 255 : 0;
                else ff = int((w - pc.fogp.y) / (pc.fogp.x - pc.fogp.y) * 255.0);
            } else if (fog == 2) ff = int(exp(-pc.fogp.z * d) * 255.0 + 0.5);   // EXP
            else {                                                       // EXP2
                float x = pc.fogp.z * d;
                ff = x > 64.0 ? 0 : int(exp(-x * x) * 255.0 + 0.5);
            }
            ff = clamp(ff, 0, 255);
        }
        if (ff < 255)
            f.rgb = ivec3(m255(f.r, ff) + m255(pc.fogc.r, 255 - ff), m255(f.g, ff) + m255(pc.fogc.g, 255 - ff),
                          m255(f.b, ff) + m255(pc.fogc.b, 255 - ff));
    }
    int af = (fl >> 16) & 15;
    if (af != 0 && !cmp(af, f.a, pc.aref)) discard;
    if ((fl & F_Q565) != 0)                     // the core keeps a 565 target's top bits: so does this
        o_col = vec4(float(f.r >> 3) / 31.0, float(f.g >> 2) / 63.0, float(f.b >> 3) / 31.0, 1.0);
    else
        o_col = vec4(f) / 255.0;
}
