#version 450
// Rigid mesh vertex shader. Positions are camera-relative to keep precision at system scale.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;
layout(location = 4) in vec4 in_color;

layout(set = 1, binding = 0) uniform VertexUniforms {
    mat4 view_proj;
    mat4 model;   // object -> camera-relative world
    mat4 normal;  // inverse-transpose of model (upper 3x3 used)
} u;

layout(location = 0) out vec3 v_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_tangent;
layout(location = 4) out vec4 v_color;
layout(location = 5) out vec3 v_local;

void main() {
    vec4 world = u.model * vec4(in_pos, 1.0);
    v_pos = world.xyz;
    v_normal = mat3(u.normal) * in_normal;
    v_tangent = vec4(mat3(u.model) * in_tangent.xyz, in_tangent.w);
    v_uv = in_uv;
    v_color = in_color;
    v_local = in_pos;
    gl_Position = u.view_proj * world;
}
