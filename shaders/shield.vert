#version 450
// A shield bubble: an ellipsoid around a ship built from the vertex index (a 32 x 16
// latitude/longitude grid, two triangles per cell).
layout(set = 1, binding = 0) uniform U {
    mat4 view_proj;
    vec4 center;  // xyz camera-relative centre
    vec4 ax[3];   // the ellipsoid's axes (orientation times radius)
} u;
layout(location = 0) out vec3 v_local;   // unit sphere position
layout(location = 1) out vec3 v_world;   // camera-relative position
layout(location = 2) out vec3 v_normal;  // world normal
void main() {
    const int LON = 32, LAT = 16;
    int cell = gl_VertexIndex / 6, corner = gl_VertexIndex % 6;
    int i = cell % LON, j = cell / LON;
    const ivec2 offs[6] = ivec2[](ivec2(0, 0), ivec2(1, 0), ivec2(1, 1), ivec2(0, 0), ivec2(1, 1), ivec2(0, 1));
    ivec2 g = ivec2(i, j) + offs[corner];
    float phi = float(g.x) / LON * 6.2831853, th = float(g.y) / LAT * 3.14159265;
    vec3 s = vec3(sin(th) * cos(phi), cos(th), sin(th) * sin(phi));
    v_local = s;
    vec3 p = u.center.xyz + u.ax[0].xyz * s.x + u.ax[1].xyz * s.y + u.ax[2].xyz * s.z;
    v_world = p;
    // Normal of an ellipsoid: the sphere normal through the inverse-transpose of the axes.
    vec3 a0 = u.ax[0].xyz / dot(u.ax[0].xyz, u.ax[0].xyz);
    vec3 a1 = u.ax[1].xyz / dot(u.ax[1].xyz, u.ax[1].xyz);
    vec3 a2 = u.ax[2].xyz / dot(u.ax[2].xyz, u.ax[2].xyz);
    v_normal = normalize(a0 * s.x + a1 * s.y + a2 * s.z);
    gl_Position = u.view_proj * vec4(p, 1.0);
}
