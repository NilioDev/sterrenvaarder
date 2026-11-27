#version 450
// 13-tap downsample (as in modern bloom chains). First pass applies a soft threshold.
layout(location = 0) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D t_src;
layout(set = 3, binding = 0) uniform U { vec4 texel_threshold; } u;  // xy texel size, z threshold, w first pass
layout(location = 0) out vec4 o;
vec3 s(vec2 off) { return texture(t_src, v_uv + off * u.texel_threshold.xy).rgb; }
void main() {
    vec3 a = s(vec2(-2, -2)), b = s(vec2(0, -2)), c = s(vec2(2, -2));
    vec3 d = s(vec2(-1, -1)), e = s(vec2(1, -1));
    vec3 f = s(vec2(-2, 0)), g = s(vec2(0, 0)), h = s(vec2(2, 0));
    vec3 i = s(vec2(-1, 1)), j = s(vec2(1, 1));
    vec3 k = s(vec2(-2, 2)), l = s(vec2(0, 2)), m = s(vec2(2, 2));
    vec3 col = (d + e + i + j) * 0.125 + (a + c + k + m) * 0.03125 + (b + f + h + l) * 0.0625 + g * 0.125;
    if (u.texel_threshold.w > 0.5) {
        float br = max(col.r, max(col.g, col.b));
        float t = u.texel_threshold.z;
        float soft = clamp(br - t * 0.5, 0.0, t) ;
        soft = soft * soft / (4.0 * t + 1e-4);
        col *= max(soft, br - t) / max(br, 1e-4);
    }
    o = vec4(col, 1.0);
}
