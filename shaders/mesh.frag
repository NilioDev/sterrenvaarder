#version 450
// Forward HDR shading for Freelancer materials (DcDt, Ec/Et, Ot, detail maps).
layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_tangent;
layout(location = 4) in vec4 v_color;
layout(location = 5) in vec3 v_local;

layout(set = 2, binding = 0) uniform sampler2D t_diffuse;
layout(set = 2, binding = 1) uniform sampler2D t_normal;
layout(set = 2, binding = 2) uniform sampler2D t_emissive;
layout(set = 2, binding = 3) uniform sampler2D t_detail;

#define MAX_LIGHTS 8
layout(set = 3, binding = 0) uniform FragmentUniforms {
    vec4 diffuse;      // rgb = Dc, a = opacity
    vec4 emissive;     // rgb = Ec, a = 1 if Et present
    vec4 ambient;      // rgb = system ambient, w = 1: clamp light sum to 1 (fixed-function style, rooms)
    vec4 params;       // x = normal strength, y = use vertex color, z = unlit, w = detail tile rate
    vec4 params2;      // x = has detail, y = specular strength, z = exposure scale for unlit, w = sphere uv mode
    vec4 light_pos[MAX_LIGHTS];    // xyz camera-relative, w = range
    vec4 light_color[MAX_LIGHTS];  // rgb, w = 1 if active
    vec4 light_atten[MAX_LIGHTS];  // xyz = constant, linear, quadratic attenuation
} u;

layout(location = 0) out vec4 out_color;

void main() {
    vec2 uv = v_uv;
    vec4 base = texture(t_diffuse, uv) * vec4(u.diffuse.rgb, u.diffuse.a);
    if (u.params.y > 0.5) base *= v_color;
    if (u.params2.x > 0.5) {
        // Planet detail map: tiled grey detail modulating the base colour.
        vec3 d = texture(t_detail, uv * u.params.w).rgb;
        base.rgb *= mix(vec3(1.0), d * 2.0, 0.5);
    }
    if (u.params.z > 0.5) {  // unlit: backgrounds, nebulae, glows
        out_color = vec4(base.rgb * u.params2.z, base.a);
        return;
    }
    vec3 n = normalize(v_normal);
    if (!gl_FrontFacing) n = -n;
    if (u.params.x > 0.0 && dot(v_tangent.xyz, v_tangent.xyz) > 1e-6) {
        vec3 t = normalize(v_tangent.xyz - n * dot(n, v_tangent.xyz));
        vec3 b = cross(n, t) * v_tangent.w;
        vec3 tn = texture(t_normal, uv).xyz * 2.0 - 1.0;
        tn.xy *= u.params.x;
        n = normalize(t * tn.x + b * tn.y + n * tn.z);
    }
    vec3 view = normalize(-v_pos);
    float out_alpha = base.a;
    if (u.params2.w > 0.5) {  // planet atmosphere shell: thicker towards the limb
        float rim = 1.0 - abs(dot(normalize(v_normal), view));
        out_alpha = clamp(u.diffuse.a * (0.15 + 1.6 * pow(rim, 2.5)), 0.0, 1.0);
    }
    // Material emissive colour (Ec) adds to the light falling on the texture, as in the
    // original fixed-function lighting; an emissive texture (Et) glows on its own.
    vec3 light_sum = u.ambient.rgb + (u.emissive.a > 0.5 ? vec3(0.0) : u.emissive.rgb);
    vec3 spec_sum = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (u.light_color[i].w < 0.5) continue;
        vec3 to_light = u.light_pos[i].xyz - v_pos;
        float dist = length(to_light);
        vec3 l = to_light / max(dist, 1e-3);
        float range = u.light_pos[i].w;
        float fade = range > 0.0 ? clamp((range - dist) / (range * 0.1), 0.0, 1.0) : 1.0;
        vec3 a = u.light_atten[i].xyz;
        fade /= max(a.x + a.y * dist + a.z * dist * dist, 1e-3);
        float ndl = max(dot(n, l), 0.0);
        vec3 h = normalize(l + view);
        float spec = pow(max(dot(n, h), 0.0), 32.0) * u.params2.y;
        light_sum += ndl * u.light_color[i].rgb * fade;
        spec_sum += spec * ndl * u.light_color[i].rgb * fade;
    }
    if (u.ambient.w > 0.5) light_sum = min(light_sum, vec3(1.0));
    vec3 lit = base.rgb * light_sum + spec_sum;
    if (u.emissive.a > 0.5) lit += texture(t_emissive, uv).rgb;
    out_color = vec4(lit, out_alpha);
}
