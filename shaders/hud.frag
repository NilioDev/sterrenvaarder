#version 450
// Solid HUD shapes, and text from the font's signed-distance atlas (u >= 0).
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D t_font;
layout(location = 0) out vec4 o;
void main() {
    if (v_uv.x < 0.0) { o = v_color; return; }
    float d = texture(t_font, v_uv).r;
    float w = max(fwidth(d), 1e-4) * 0.7;
    o = vec4(v_color.rgb, v_color.a * smoothstep(0.5 - w, 0.5 + w, d));
}
