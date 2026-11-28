#version 450
// HUD overlay: positions in output pixels, origin top-left.
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
layout(set = 1, binding = 0) uniform U { vec4 screen; };  // xy = 2 / size
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
void main() {
    v_color = a_color;
    v_uv = a_uv;
    gl_Position = vec4(a_pos.x * screen.x - 1.0, 1.0 - a_pos.y * screen.y, 0.0, 1.0);
}
