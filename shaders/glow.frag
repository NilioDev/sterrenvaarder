#version 450
layout(location = 0) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D t_glow;
layout(set = 3, binding = 0) uniform U { vec4 inner; vec4 outer; } u;  // rgb colour, inner.w intensity
layout(location = 0) out vec4 o;
void main() {
    vec4 t = texture(t_glow, v_uv);
    float r = length(v_uv * 2.0 - 1.0);
    vec3 col = mix(u.inner.rgb, u.outer.rgb, clamp(r, 0.0, 1.0));
    // Soft radial falloff on top of the original glow texture hides its low resolution.
    float falloff = pow(clamp(1.0 - r, 0.0, 1.0), 2.0);
    o = vec4(col * t.rgb * t.a * falloff * u.inner.w, 1.0);
}
