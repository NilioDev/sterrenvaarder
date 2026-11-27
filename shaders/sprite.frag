#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 1) in float v_t;
layout(set = 2, binding = 0) uniform sampler2D t_sprite;
// rgb colour, a alpha; a streak blends from color at its near end to color2 at the far end.
layout(set = 3, binding = 0) uniform U { vec4 color; vec4 color2; } u;
layout(location = 0) out vec4 o;
void main() {
    vec4 t = texture(t_sprite, v_uv);
    vec4 c = mix(u.color, u.color2, v_t);
    o = vec4(c.rgb * t.rgb, t.a * c.a);
}
