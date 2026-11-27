#version 450
layout(location = 0) in vec3 v_local;
layout(location = 1) in vec3 v_world;
layout(location = 2) in vec3 v_normal;
layout(set = 3, binding = 0) uniform U {
    vec4 color;  // rgb, a = strength (fades out after the hit)
    vec4 hit;    // xyz hit direction on the unit sphere, w = spot sharpness
} u;
layout(location = 0) out vec4 o;
void main() {
    vec3 n = normalize(v_normal);
    vec3 view = normalize(-v_world);
    float rim = 1.0 - abs(dot(n, view));
    float spot = exp(-(1.0 - dot(normalize(v_local), u.hit.xyz)) * u.hit.w);
    float a = u.color.a * (0.25 * rim * rim * rim + 0.8 * spot);
    o = vec4(u.color.rgb, a);
}
