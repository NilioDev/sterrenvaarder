// SPDX-License-Identifier: MPL-2.0
#pragma once
// 2D overlay geometry in output pixels (origin top-left). The HUD is drawn after
// upscaling and sharpening, so it stays crisp at every render scale and resolution.
// Text uses the interface font when one is set (set_font), else a built-in stroke font.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sterrenvaarder {
struct FontAtlas;
}

namespace sterrenvaarder::render {

struct HudVertex {
    float x, y;
    std::uint32_t rgba;  // little-endian RGBA8 (r in the low byte)
    float u = -1, v = 0;  // font atlas coordinates; u < 0 is a solid shape
};
static_assert(sizeof(HudVertex) == 20);

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
    // Breaks text into lines no wider than max_width, at spaces and at '\n'.
    static std::vector<std::string> wrap_text(float size, std::string_view s, float max_width);
    // The interface font for all HUD text (nullptr = stroke font). Text is Latin-1 or UTF-8.
    static void set_font(const FontAtlas* font);
    static const FontAtlas* font();

private:
    float font_text(float x, float y, float size, std::string_view s, HudColor c, TextAlign align);
    void tri(float ax, float ay, float bx, float by, float cx, float cy, std::uint32_t c);
    std::vector<HudVertex> verts_;
};

}  // namespace sterrenvaarder::render
