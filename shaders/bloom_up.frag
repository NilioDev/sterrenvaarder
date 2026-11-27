#version 450
// 3x3 tent upsample, blended additively onto the next larger level.
layout(location = 0) in vec2 v_uv;
layout(set = 2, binding = 0) uniform sampler2D t_src;
layout(set = 3, binding = 0) uniform U { vec4 texel_radius; } u;
layout(location = 0) out vec4 o;
void main() {
    vec2 r = u.texel_radius.xy * u.texel_radius.z;
    vec3 c = texture(t_src, v_uv).rgb * 4.0;
    c += (texture(t_src, v_uv + vec2(-r.x, 0)).rgb + texture(t_src, v_uv + vec2(r.x, 0)).rgb +
          texture(t_src, v_uv + vec2(0, -r.y)).rgb + texture(t_src, v_uv + vec2(0, r.y)).rgb) * 2.0;
    c += texture(t_src, v_uv + vec2(-r.x, -r.y)).rgb + texture(t_src, v_uv + vec2(r.x, -r.y)).rgb +
         texture(t_src, v_uv + vec2(-r.x, r.y)).rgb + texture(t_src, v_uv + vec2(r.x, r.y)).rgb;
    o = vec4(c / 16.0, 1.0);
}
