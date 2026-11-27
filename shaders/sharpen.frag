#version 450
// Contrast-adaptive sharpening (applied after upscaling or at native resolution).
layout(location = 0) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D t_src;
layout(set = 3, binding = 0) uniform U { vec4 texel_sharpness; } u;  // xy texel size, z sharpness 0..1
layout(location = 0) out vec4 o;
void main() {
    vec2 t = u.texel_sharpness.xy;
    vec3 b = texture(t_src, v_uv + vec2(0, -t.y)).rgb;
    vec3 d = texture(t_src, v_uv + vec2(-t.x, 0)).rgb;
    vec3 e = texture(t_src, v_uv).rgb;
    vec3 f = texture(t_src, v_uv + vec2(t.x, 0)).rgb;
    vec3 h = texture(t_src, v_uv + vec2(0, t.y)).rgb;
    vec3 mn = min(min(min(b, d), min(f, h)), e);
    vec3 mx = max(max(max(b, d), max(f, h)), e);
    vec3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 1e-4), 0.0, 1.0));
    float peak = -1.0 / mix(8.0, 5.0, clamp(u.texel_sharpness.z, 0.0, 1.0));
    vec3 w = amp * peak;
    vec3 c = (b * w + d * w + f * w + h * w + e) / (1.0 + 4.0 * w);
    o = vec4(clamp(c, 0.0, 1.0), 1.0);
}
