#version 450
layout(location = 0) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D t_sprite;
layout(set = 3, binding = 0) uniform U { vec4 color; } u;  // rgb colour, a alpha
layout(location = 0) out vec4 o;
void main() {
    vec4 t = texture(t_sprite, v_uv);
    o = vec4(u.color.rgb * t.rgb, t.a * u.color.a);
}
