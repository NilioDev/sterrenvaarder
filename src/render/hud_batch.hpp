// SPDX-License-Identifier: MPL-2.0
#pragma once
// 2D overlay geometry in output pixels (origin top-left). The HUD is drawn after
// upscaling and sharpening, so it stays crisp at every render scale and resolution.
// Text uses a small built-in stroke font, so no font files are needed.
#include <cstdint>
#include <string_view>
#include <vector>

namespace sterrenvaarder::render {

struct HudVertex {
    float x, y;
    std::uint32_t rgba;  // little-endian RGBA8 (r in the low byte)
};
static_assert(sizeof(HudVertex) == 12);

struct HudColor {
    float r = 1, g = 1, b = 1, a = 1;
    HudColor with_alpha(float na) const { return {r, g, b, na}; }
    std::uint32_t packed() const;
};

enum class TextAlign { Left, Center, Right };

class HudBatch {
public:
    void clear() { verts_.clear(); }
    const std::vector<HudVertex>& vertices() const { return verts_; }

    void rect(float x, float y, float w, float h, HudColor c);
    void line(float x0, float y0, float x1, float y1, float width, HudColor c);
    void rect_outline(float x, float y, float w, float h, float width, HudColor c);
    // Arc from angle a0 to a1 (radians, 0 = right, clockwise on screen because y points down).
    void arc(float cx, float cy, float radius, float a0, float a1, float width, HudColor c, int segments = 32);
    void circle(float cx, float cy, float radius, float width, HudColor c, int segments = 48) {
        arc(cx, cy, radius, 0, 6.2831853f, width, c, segments);
    }
    // `size` is the cap height in pixels. Returns the text width.
    float text(float x, float y, float size, std::string_view s, HudColor c, TextAlign align = TextAlign::Left);
    static float text_width(float size, std::string_view s);

private:
    void tri(float ax, float ay, float bx, float by, float cx, float cy, std::uint32_t c);
    std::vector<HudVertex> verts_;
};

}  // namespace sterrenvaarder::render
