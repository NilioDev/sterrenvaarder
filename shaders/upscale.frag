#version 450
// Lanczos-2 upscale with ringing clamp, used when rendering below output resolution.
layout(location = 0) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D t_src;
layout(set = 3, binding = 0) uniform U { vec4 src_size; } u;  // xy size, zw 1/size
layout(location = 0) out vec4 o;
float lanczos2(float x) {
    x = abs(x);
    if (x < 1e-5) return 1.0;
    if (x >= 2.0) return 0.0;
    float px = 3.14159265 * x;
    return 2.0 * sin(px) * sin(px * 0.5) / (px * px);
}
void main() {
    vec2 pos = v_uv * u.src_size.xy - 0.5;
    vec2 base = floor(pos);
    vec2 f = pos - base;
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    vec3 mn = vec3(1e9), mx = vec3(-1e9);
    for (int y = -1; y <= 2; ++y) {
        for (int x = -1; x <= 2; ++x) {
            vec2 tc = (base + vec2(x, y) + 0.5) * u.src_size.zw;
            vec3 c = texture(t_src, tc).rgb;
            float w = lanczos2(float(x) - f.x) * lanczos2(float(y) - f.y);
            sum += c * w;
            wsum += w;
            if (x >= 0 && x <= 1 && y >= 0 && y <= 1) { mn = min(mn, c); mx = max(mx, c); }
        }
    }
    o = vec4(clamp(sum / wsum, mn, mx), 1.0);
}
