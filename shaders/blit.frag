#version 450
layout(location = 0) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D t_src;
layout(location = 0) out vec4 o;
void main() { o = vec4(texture(t_src, v_uv).rgb, 1.0); }
