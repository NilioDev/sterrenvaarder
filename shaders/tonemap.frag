#version 450
// Exposure, bloom composite and ACES filmic tonemapping to display-referred sRGB, then the
// optional post-processing look (postfx.ini). With the look off (params.w = 0) the output is
// exactly the plain tonemap.
layout(location = 0) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D t_hdr;
layout(set = 2, binding = 1) uniform sampler2D t_bloom;
layout(set = 3, binding = 0) uniform U {
    vec4 params;  // x exposure, y bloom strength, z time (s), w look on
    vec4 grade;   // x saturation, y vibrance, z contrast, w brightness
    vec4 extra;   // x temperature, y vignette, z grain, w chromatic aberration
    vec4 aspect;  // x width / height
} u;
layout(location = 0) out vec4 o;
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 scene(vec2 uv) { return texture(t_hdr, uv).rgb + texture(t_bloom, uv).rgb * u.params.y; }
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    vec3 hdr;
    bool look = u.params.w > 0.5;
    if (look && u.extra.w > 0.0) {
        // Colour fringes grow toward the edges, like a simple lens.
        vec2 d = (v_uv - 0.5) * u.extra.w * 0.012;
        hdr = vec3(scene(v_uv - d).r, scene(v_uv).g, scene(v_uv + d).b);
    } else {
        hdr = scene(v_uv);
    }
    vec3 c = aces(hdr * u.params.x);
    if (look) {
        // Temperature: warm shifts red up and blue down, cool the reverse.
        float t = u.extra.x;
        c *= vec3(1.0 + 0.1 * t, 1.0, 1.0 - 0.1 * t);
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        // Vibrance lifts saturation more where colours are still dull.
        float sat = max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
        float s = u.grade.x * (1.0 + u.grade.y * (1.0 - sat));
        c = mix(vec3(l), c, s);
        c = (c - 0.5) * u.grade.z + 0.5 + u.grade.w;
        // Vignette: darker corners, round on any screen shape.
        vec2 v = (v_uv - 0.5) * vec2(u.aspect.x, 1.0) / max(u.aspect.x, 1.0);
        c *= 1.0 - u.extra.y * smoothstep(0.2, 0.75, length(v) * 1.2);
        c = clamp(c, 0.0, 1.0);
    }
    c = pow(c, vec3(1.0 / 2.2));
    if (look && u.extra.z > 0.0)
        c += (hash(v_uv * 1000.0 + fract(u.params.z) * 61.0) - 0.5) * 0.08 * u.extra.z;
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
