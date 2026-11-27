#version 450
// Camera-facing quad for star glows and centres.
layout(set = 1, binding = 0) uniform U {
    mat4 view_proj;
    vec4 center_size;  // xyz camera-relative centre, w half-size
    vec4 right;        // camera right vector
    vec4 up;           // camera up vector
} u;
layout(location = 0) out vec2 v_uv;
void main() {
    const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    vec2 c = corners[gl_VertexIndex];
    v_uv = c * 0.5 + 0.5;
    vec3 p = u.center_size.xyz + (u.right.xyz * c.x + u.up.xyz * c.y) * u.center_size.w;
    gl_Position = u.view_proj * vec4(p, 1.0);
}
