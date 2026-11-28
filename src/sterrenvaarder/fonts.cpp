// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/fonts.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"

namespace sterrenvaarder {
namespace fs = std::filesystem;

std::vector<FontStyle> parse_fonts_ini(const IniFile& ini) {
    std::vector<FontStyle> out;
    for (auto& sec : ini) {
        if (!iequals(sec.name, "TrueType")) continue;
        FontStyle st;
        if (auto* e = sec.find("nickname"); e && !e->values.empty()) st.nickname = e->values[0].as_string();
        if (auto* e = sec.find("font"); e && !e->values.empty()) st.font = e->values[0].as_string();
        if (auto* e = sec.find("fixed_height"); e && !e->values.empty()) st.fixed_height = e->values[0].as_float();
        if (!st.nickname.empty()) out.push_back(std::move(st));
    }
    return out;
}

const FontStyle* find_font_style(const std::vector<FontStyle>& styles, std::string_view nickname) {
    for (auto& s : styles)
        if (iequals(s.nickname, nickname)) return &s;
    return nullptr;
}

namespace {

std::optional<std::vector<std::uint8_t>> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string squash(std::string_view s) {  // lower case, letters and digits only
    std::string r;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::vector<fs::path> font_dirs() {
    std::vector<fs::path> d;
    if (const char* w = std::getenv("WINDIR")) d.emplace_back(fs::path(w) / "Fonts");
    d.emplace_back("C:/Windows/Fonts");
    if (const char* la = std::getenv("LOCALAPPDATA")) d.emplace_back(fs::path(la) / "Microsoft/Windows/Fonts");
    if (const char* h = std::getenv("HOME")) {
        d.emplace_back(fs::path(h) / ".fonts");
        d.emplace_back(fs::path(h) / ".local/share/fonts");
        d.emplace_back(fs::path(h) / "Library/Fonts");
    }
    for (const char* p : {"/usr/share/fonts", "/usr/local/share/fonts", "/Library/Fonts", "/System/Library/Fonts"})
        d.emplace_back(p);
    return d;
}

}  // namespace

std::string font_family(Bytes ttf) {
    stbtt_fontinfo info;
    int off = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (off < 0 || !stbtt_InitFont(&info, ttf.data(), off)) return {};
    // Name ID 1 (family): Windows Unicode English first, then Mac Roman.
    int len = 0;
    if (const char* s = stbtt_GetFontNameString(&info, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
                                                STBTT_MS_LANG_ENGLISH, 1)) {
        std::string r;
        for (int i = 0; i + 1 < len; i += 2) r += s[i] == 0 && static_cast<unsigned char>(s[i + 1]) < 128 ? s[i + 1] : '?';
        return r;
    }
    if (const char* s = stbtt_GetFontNameString(&info, &len, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN, STBTT_MAC_LANG_ENGLISH, 1))
        return std::string(s, static_cast<std::size_t>(len));
    return {};
}

std::optional<std::string> find_system_font(std::string_view family, const std::vector<std::string>& extra_dirs) {
    std::string want = squash(family);
    if (want.empty()) return std::nullopt;
    // Known file names of the original's fonts, then any file whose name starts like the family.
    std::vector<std::string> names;
    if (want == "agencyfb") names = {"agencyr.ttf", "agencyb.ttf"};
    else if (want == "arialunicodems") names = {"arialuni.ttf"};
    std::string stem = want.substr(0, std::min<std::size_t>(want.size(), 4));
    std::vector<fs::path> dirs;
    for (auto& e : extra_dirs) dirs.emplace_back(e);
    for (auto& d : font_dirs()) dirs.push_back(d);
    std::vector<fs::path> candidates;
    for (auto& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string fname = to_lower(it->path().filename().string());
            std::string ext = to_lower(it->path().extension().string());
            if (ext != ".ttf" && ext != ".otf") continue;
            if (std::find(names.begin(), names.end(), fname) != names.end()) return it->path().string();
            if (squash(fname).rfind(stem, 0) == 0) candidates.push_back(it->path());
        }
    }
    // Prefer the regular face: no bold or italic in the name, then the shortest name.
    std::sort(candidates.begin(), candidates.end(), [](const fs::path& a, const fs::path& b) {
        auto styled = [](const fs::path& p) {
            std::string n = to_lower(p.filename().string());
            return n.find("bold") != std::string::npos || n.find("italic") != std::string::npos;
        };
        if (styled(a) != styled(b)) return !styled(a);
        return a.filename().string().size() < b.filename().string().size();
    });
    for (auto& c : candidates)
        if (auto data = read_file(c); data && squash(font_family(Bytes(*data))) == want) return c.string();
    return std::nullopt;
}

std::optional<FontAtlas> bake_font_atlas(Bytes ttf, float pixel_height) {
    stbtt_fontinfo info;
    int off = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    if (off < 0 || !stbtt_InitFont(&info, ttf.data(), off)) return std::nullopt;
    FontAtlas a;
    a.family = font_family(ttf);
    float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
    const int pad = 6;
    a.spread = 2.f * pad;
    const unsigned char onedge = 128;
    const float dist_scale = 128.f / pad;
    int x0, y0, x1, y1;
    if (stbtt_GetCodepointBox(&info, 'H', &x0, &y0, &x1, &y1)) a.cap_height = y1 * scale;
    else a.cap_height = pixel_height * 0.7f;

    struct Bitmap { int cp, w, h, xoff, yoff; unsigned char* data; };
    std::vector<Bitmap> bmps;
    for (int cp = 32; cp < 256; ++cp) {
        if (cp >= 127 && cp < 160) continue;
        if (!stbtt_FindGlyphIndex(&info, cp)) continue;
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&info, cp, &adv, &lsb);
        auto& g = a.glyphs[static_cast<std::size_t>(cp)];
        g.advance = adv * scale;
        g.present = true;
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char* d = stbtt_GetCodepointSDF(&info, scale, cp, pad, onedge, dist_scale, &w, &h, &xo, &yo);
        if (d) bmps.push_back({cp, w, h, xo, yo, d});
    }
    // Shelf packing, tallest first.
    std::sort(bmps.begin(), bmps.end(), [](const Bitmap& l, const Bitmap& r) { return l.h > r.h; });
    a.width = 512;
    int x = 0, y = 0, shelf = 0;
    std::vector<std::pair<int, int>> at(bmps.size());
    for (std::size_t i = 0; i < bmps.size(); ++i) {
        if (x + bmps[i].w + 1 > a.width) { x = 0; y += shelf + 1; shelf = 0; }
        at[i] = {x, y};
        x += bmps[i].w + 1;
        shelf = std::max(shelf, bmps[i].h);
    }
    a.height = 1;
    while (a.height < y + shelf) a.height *= 2;
    a.pixels.assign(static_cast<std::size_t>(a.width) * a.height, 0);
    for (std::size_t i = 0; i < bmps.size(); ++i) {
        auto& b = bmps[i];
        for (int r = 0; r < b.h; ++r)
            std::copy(b.data + r * b.w, b.data + (r + 1) * b.w, a.pixels.begin() + (at[i].second + r) * a.width + at[i].first);
        auto& g = a.glyphs[static_cast<std::size_t>(b.cp)];
        g.u0 = static_cast<float>(at[i].first) / a.width;
        g.v0 = static_cast<float>(at[i].second) / a.height;
        g.u1 = static_cast<float>(at[i].first + b.w) / a.width;
        g.v1 = static_cast<float>(at[i].second + b.h) / a.height;
        g.x0 = static_cast<float>(b.xoff);
        g.y0 = static_cast<float>(b.yoff);
        g.x1 = static_cast<float>(b.xoff + b.w);
        g.y1 = static_cast<float>(b.yoff + b.h);
        stbtt_FreeSDF(b.data, nullptr);
    }
    return a;
}

}  // namespace sterrenvaarder
