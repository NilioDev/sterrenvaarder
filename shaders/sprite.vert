#version 450
// Camera-facing, rotated quad for particle effects.
layout(set = 1, binding = 0) uniform U {
    mat4 view_proj;
    vec4 center_size;  // xyz camera-relative centre, w half-size
    vec4 right;        // camera right vector
    vec4 up;           // camera up vector
    vec4 rot;          // cos, sin of the rotation around the view axis
    vec4 uv;           // texture rect: u0, v0 (top left), u1, v1 (bottom right)
} u;
layout(location = 0) out vec2 v_uv;
void main() {
    const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    vec2 c = corners[gl_VertexIndex];
    v_uv = mix(u.uv.xy, u.uv.zw, vec2(c.x * 0.5 + 0.5, 0.5 - c.y * 0.5));
    vec2 r = vec2(c.x * u.rot.x - c.y * u.rot.y, c.x * u.rot.y + c.y * u.rot.x);
    vec3 p = u.center_size.xyz + (u.right.xyz * r.x + u.up.xyz * r.y) * u.center_size.w;
    gl_Position = u.view_proj * vec4(p, 1.0);
}
