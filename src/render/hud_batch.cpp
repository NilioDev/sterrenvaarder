// SPDX-License-Identifier: MPL-2.0
#include "render/hud_batch.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "sterrenvaarder/fonts.hpp"

namespace sterrenvaarder::render {

std::uint32_t HudColor::packed() const {
    auto b8 = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    return b8(r) | (b8(g) << 8) | (b8(b) << 16) | (b8(a) << 24);
}

void HudBatch::tri(float ax, float ay, float bx, float by, float cx, float cy, std::uint32_t c) {
    verts_.push_back({ax, ay, c, -1, 0});
    verts_.push_back({bx, by, c, -1, 0});
    verts_.push_back({cx, cy, c, -1, 0});
}

void HudBatch::rect(float x, float y, float w, float h, HudColor c) {
    auto p = c.packed();
    tri(x, y, x + w, y, x + w, y + h, p);
    tri(x, y, x + w, y + h, x, y + h, p);
}

void HudBatch::line(float x0, float y0, float x1, float y1, float width, HudColor c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) {
        rect(x0 - width * 0.5f, y0 - width * 0.5f, width, width, c);
        return;
    }
    float h = width * 0.5f;
    float ux = dx / len * h, uy = dy / len * h;  // along, half width (square caps)
    float nx = -uy, ny = ux;                     // normal, half width
    auto p = c.packed();
    float ax = x0 - ux + nx, ay = y0 - uy + ny, bx = x1 + ux + nx, by = y1 + uy + ny;
    float cx = x1 + ux - nx, cy = y1 + uy - ny, ex = x0 - ux - nx, ey = y0 - uy - ny;
    tri(ax, ay, bx, by, cx, cy, p);
    tri(ax, ay, cx, cy, ex, ey, p);
}

void HudBatch::rect_outline(float x, float y, float w, float h, float t, HudColor c) {
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y + t, t, h - 2 * t, c);
    rect(x + w - t, y + t, t, h - 2 * t, c);
}

void HudBatch::arc(float cx, float cy, float r, float a0, float a1, float width, HudColor c, int segments) {
    auto p = c.packed();
    float ri = r - width * 0.5f, ro = r + width * 0.5f;
    int n = std::max(2, static_cast<int>(std::ceil(segments * std::fabs(a1 - a0) / 6.2831853f)));
    for (int i = 0; i < n; ++i) {
        float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n;
        float c0 = std::cos(t0), s0 = std::sin(t0), c1 = std::cos(t1), s1 = std::sin(t1);
        tri(cx + c0 * ri, cy + s0 * ri, cx + c0 * ro, cy + s0 * ro, cx + c1 * ro, cy + s1 * ro, p);
        tri(cx + c0 * ri, cy + s0 * ri, cx + c1 * ro, cy + s1 * ro, cx + c1 * ri, cy + s1 * ri, p);
    }
}

// ---------------------------------------------------------------- stroke font
// Glyphs on a 4 x 6 grid (y down). Each glyph is a list of polylines separated by
// spaces; a polyline is a comma-separated list of "xy" grid points.
namespace {

const char* glyph(char ch) {
    switch (std::toupper(static_cast<unsigned char>(ch))) {
        case '0': return "00,40,46,06,00 06,40";
        case '1': return "10,20,26 06,46";
        case '2': return "00,40,43,03,06,46";
        case '3': return "00,40,46,06 13,43";
        case '4': return "00,03,43 40,46";
        case '5': return "40,00,03,33,44,45,36,06";
        case '6': return "40,00,06,46,43,03";
        case '7': return "00,40,46";
        case '8': return "00,40,46,06,00 03,43";
        case '9': return "43,03,00,40,46,06";
        case 'A': return "06,01,10,30,41,46 03,43";
        case 'B': return "00,30,41,42,33,44,45,36,06,00 03,33";
        case 'C': return "40,00,06,46";
        case 'D': return "00,30,41,45,36,06,00";
        case 'E': return "40,00,06,46 03,33";
        case 'F': return "40,00,06 03,33";
        case 'G': return "40,00,06,46,43,23";
        case 'H': return "00,06 40,46 03,43";
        case 'I': return "10,30 20,26 16,36";
        case 'J': return "40,46,06,04";
        case 'K': return "00,06 40,03,46";
        case 'L': return "00,06,46";
        case 'M': return "06,00,23,40,46";
        case 'N': return "06,00,46,40";
        case 'O': return "00,40,46,06,00";
        case 'P': return "06,00,40,43,03";
        case 'Q': return "00,40,46,06,00 24,46";
        case 'R': return "06,00,40,43,03 23,46";
        case 'S': return "40,00,03,43,46,06";
        case 'T': return "00,40 20,26";
        case 'U': return "00,06,46,40";
        case 'V': return "00,26,40";
        case 'W': return "00,16,23,36,40";
        case 'X': return "00,46 40,06";
        case 'Y': return "00,23,40 23,26";
        case 'Z': return "00,40,06,46";
        case '-': return "13,33";
        case '+': return "03,43 21,25";
        case '.': return "25,26";
        case ',': return "25,17";
        case ':': return "21,22 24,25";
        case '/': return "06,40";
        case '%': return "06,40 00,11 35,46";
        case '(': return "30,11,15,36";
        case ')': return "10,31,35,16";
        case '[': return "30,10,16,36";
        case ']': return "10,30,36,16";
        case '<': return "40,03,46";
        case '>': return "00,43,06";
        case '\'': return "20,22";
        case '?': return "00,40,42,22,24 25,26";
        case '!': return "20,24 25,26";
        case '=': return "02,42 04,44";
        case '_': return "06,46";
        case '$': return "41,30,10,01,02,13,33,44,45,36,16,05 20,26";
        case '#': return "10,16 30,36 02,42 04,44";
        default: return "";
    }
}

constexpr float kAdvance = 5.6f;  // grid units per character

}  // namespace

namespace {
const FontAtlas* g_font = nullptr;

// Next character as a Latin-1 code (UTF-8 sequences decoded; anything above 255 becomes '?').
unsigned next_char(std::string_view s, std::size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    if ((c & 0xe0) == 0xc0 && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) {
        unsigned cp = ((c & 0x1fu) << 6) | (static_cast<unsigned char>(s[i++]) & 0x3fu);
        return cp < 256 ? cp : '?';
    }
    if ((c & 0xf0) == 0xe0 || (c & 0xf8) == 0xf0) {
        int n = (c & 0xf0) == 0xe0 ? 2 : 3;
        for (int k = 0; k < n && i < s.size() && (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80; ++k) ++i;
        return '?';
    }
    return c;  // already Latin-1
}
}  // namespace

void HudBatch::set_font(const FontAtlas* font) { g_font = font; }
const FontAtlas* HudBatch::font() { return g_font; }

float HudBatch::font_text(float x, float y, float size, std::string_view s, HudColor c, TextAlign align) {
    const FontAtlas& f = *g_font;
    float k = size / f.cap_height;
    float w = text_width(size, s);
    if (align == TextAlign::Center) x -= w * 0.5f;
    else if (align == TextAlign::Right) x -= w;
    float base = y + size;  // y is the top of the capitals
    float pen = x;
    auto p = c.packed();
    for (std::size_t i = 0; i < s.size();) {
        unsigned ch = next_char(s, i);
        const auto* g = &f.glyphs[ch];
        if (!g->present) g = &f.glyphs['?'];
        if (g->u1 > g->u0) {
            float x0 = pen + g->x0 * k, x1 = pen + g->x1 * k, y0 = base + g->y0 * k, y1 = base + g->y1 * k;
            verts_.push_back({x0, y0, p, g->u0, g->v0});
            verts_.push_back({x1, y0, p, g->u1, g->v0});
            verts_.push_back({x1, y1, p, g->u1, g->v1});
            verts_.push_back({x0, y0, p, g->u0, g->v0});
            verts_.push_back({x1, y1, p, g->u1, g->v1});
            verts_.push_back({x0, y1, p, g->u0, g->v1});
        }
        pen += g->advance * k;
    }
    return w;
}

float HudBatch::text_width(float size, std::string_view s) {
    if (s.empty()) return 0;
    if (g_font) {
        float k = size / g_font->cap_height, w = 0;
        for (std::size_t i = 0; i < s.size();) {
            unsigned ch = next_char(s, i);
            const auto* g = &g_font->glyphs[ch];
            if (!g->present) g = &g_font->glyphs['?'];
            w += g->advance * k;
        }
        return w;
    }
    float u = size / 6.f;
    return (static_cast<float>(s.size()) * kAdvance - (kAdvance - 4.f)) * u;
}

std::vector<std::string> HudBatch::wrap_text(float size, std::string_view s, float max_width) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= s.size()) {
        auto nl = s.find('\n', start);
        std::string_view para = s.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        std::string line;
        std::size_t i = 0;
        while (i < para.size()) {
            auto sp = para.find(' ', i);
            std::string_view word = para.substr(i, sp == std::string_view::npos ? std::string_view::npos : sp - i);
            i = sp == std::string_view::npos ? para.size() : sp + 1;
            if (word.empty()) continue;
            std::string next = line.empty() ? std::string(word) : line + " " + std::string(word);
            if (!line.empty() && text_width(size, next) > max_width) {
                lines.push_back(line);
                line = std::string(word);
            } else {
                line = std::move(next);
            }
        }
        lines.push_back(line);
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return lines;
}

float HudBatch::text(float x, float y, float size, std::string_view s, HudColor c, TextAlign align) {
    if (g_font) return font_text(x, y, size, s, c, align);
    float u = size / 6.f;
    float w = text_width(size, s);
    if (align == TextAlign::Center) x -= w * 0.5f;
    else if (align == TextAlign::Right) x -= w;
    float stroke = std::max(1.0f, size * 0.12f);
    float pen = x;
    for (char ch : s) {
        const char* g = glyph(ch);
        float px = 0, py = 0;
        bool have = false;
        for (const char* p = g; *p;) {
            if (*p == ' ') { have = false; ++p; continue; }
            if (*p == ',') { ++p; continue; }
            float gx = static_cast<float>(p[0] - '0'), gy = static_cast<float>(p[1] - '0');
            p += 2;
            float sx = pen + gx * u, sy = y + gy * u;
            if (have) line(px, py, sx, sy, stroke, c);
            px = sx;
            py = sy;
            have = true;
        }
        pen += kAdvance * u;
    }
    return w;
}

}  // namespace sterrenvaarder::render
