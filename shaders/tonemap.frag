#version 450
// Exposure, bloom composite and ACES filmic tonemapping to display-referred sRGB.
layout(location = 0) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D t_hdr;
layout(set = 2, binding = 1) uniform sampler2D t_bloom;
layout(set = 3, binding = 0) uniform U { vec4 params; } u;  // x exposure, y bloom strength
layout(location = 0) out vec4 o;
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main() {
    vec3 hdr = texture(t_hdr, v_uv).rgb + texture(t_bloom, v_uv).rgb * u.params.y;
    vec3 c = aces(hdr * u.params.x);
    c = pow(c, vec3(1.0 / 2.2));
    o = vec4(c, 1.0);
}
