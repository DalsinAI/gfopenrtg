#version 450
layout(location=0) in vec2 pos;
layout(location=1) in vec4 col;
layout(location=0) out vec4 c;
layout(push_constant) uniform P { vec2 inv; } p;
void main() { c = col; gl_Position = vec4(pos * p.inv - 1.0, 0.0, 1.0); }
