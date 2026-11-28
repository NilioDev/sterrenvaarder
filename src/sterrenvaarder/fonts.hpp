// SPDX-License-Identifier: MPL-2.0
#pragma once
// Interface fonts. The original names its fonts in DATA/FONTS/fonts.ini ([TrueType]
// nickname, font, fixed_height) and takes them from Windows. We do the same: the named
// font from the player's own system when it is installed, otherwise our bundled open
// (SIL OFL) font, otherwise the built-in stroke font.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sterrenvaarder/bytes.hpp"
#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder {

struct FontStyle {
    std::string nickname, font;
    float fixed_height = 0.03f;  // fraction of the screen height
};

// Reads the [TrueType] entries of fonts.ini.
std::vector<FontStyle> parse_fonts_ini(const IniFile& ini);
const FontStyle* find_font_style(const std::vector<FontStyle>& styles, std::string_view nickname);

// Looks for an installed font by family name ("Agency FB") in the usual system and user
// font folders, plus `extra_dirs`. Returns the file path of the regular face.
std::optional<std::string> find_system_font(std::string_view family, const std::vector<std::string>& extra_dirs = {});
// The family name stored in a font file's name table, or empty.
std::string font_family(Bytes ttf);

// A signed-distance-field atlas of the Latin-1 range (32..255), so text stays sharp at
// any size. Glyph boxes are in atlas pixels relative to the pen on the baseline (y down).
struct FontAtlas {
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;  // one channel: 128 = edge, more = inside
    float cap_height = 0;              // in atlas pixels
    float spread = 0;                  // distance in pixels from 0 to 255
    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float advance = 0;
        bool present = false;
    };
    std::array<Glyph, 256> glyphs{};
    std::string family;
};

std::optional<FontAtlas> bake_font_atlas(Bytes ttf, float pixel_height = 48);

}  // namespace sterrenvaarder
