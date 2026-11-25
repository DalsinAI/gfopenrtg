// Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
// SPDX-License-Identifier: MIT
//
// Stream3D's TRIANGLES on the GPU's own pipeline (ogpu3d_gfx.c): the vertex
// shader. Positions come in pixels (28.4 exactly, as the core rounds them),
// depth 0..1 and q, the clip w that makes texture coordinates perspective
// correct (1 without PERSPECTIVE); colours, specular and fog are linear in
// screen space, as the core interpolates them: they go out times q, with q
// itself, and the fragment shader divides (perspective-correct
// interpolation of a * q over that of q is a linear in screen space). Not
// `noperspective`, which llvmpipe interpolates wrongly in triangles it clips.
#version 450
layout(location = 0) in vec4 a_pos;     // x, y (pixels), z (0..1), q
layout(location = 1) in vec4 a_tex;     // u, v (0..1 over the texture), lod, fog w
layout(location = 2) in vec4 a_col;     // R, G, B, A (UNORM8)
layout(location = 3) in vec4 a_spec;    // specular R, G, B; A = vertex fog factor
layout(push_constant) uniform P {
    vec2 scale;                         // 2 / width, 2 / height
    int flags, aref;
    ivec4 cur, envc, fogc;
    vec4 fogp;
} pc;
layout(location = 0) out vec4 v_col;     // times q
layout(location = 1) out vec4 v_spec;    // times q
layout(location = 2) out vec2 v_uv;
layout(location = 3) flat out float v_lod;
layout(location = 4) out vec2 v_fogw;    // fog w times q, q
void main() {
    float q = a_pos.w;
    gl_Position = vec4((a_pos.x * pc.scale.x - 1.0) * q, (a_pos.y * pc.scale.y - 1.0) * q, a_pos.z * q, q);
    v_col = a_col * q;
    v_spec = a_spec * q;
    v_uv = a_tex.xy;
    v_lod = a_tex.z;
    v_fogw = vec2(a_tex.w * q, q);
}
