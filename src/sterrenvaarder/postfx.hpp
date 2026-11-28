// SPDX-License-Identifier: MPL-2.0
#pragma once
// Optional post-processing ("ReShade-like" looks), set in sterrenvaarder/postfx.ini.
// Off by default: the original look is what the game shows unless the player turns it on.
#include <string>
#include <string_view>
#include <vector>

namespace sterrenvaarder {

struct PostFx {
    bool enabled = false;
    float bloom = 0;        // extra glow on top of the engine's own (0..1)
    float sharpen = 0;      // extra sharpening (0..1)
    float saturation = 1;   // 1 = unchanged, 0 = grey
    float vibrance = 0;     // boosts dull colours more than vivid ones (-1..1)
    float contrast = 1;     // 1 = unchanged
    float brightness = 0;   // added after tonemapping (-0.5..0.5)
    float temperature = 0;  // < 0 cooler (bluer), > 0 warmer (-1..1)
    float vignette = 0;     // darker corners (0..1)
    float grain = 0;        // film grain (0..1)
    float chromatic = 0;    // colour fringes toward the edges (0..1)

    // The built-in looks: original, vivid, cinematic, crisp. Returns false for an unknown name.
    static bool preset(std::string_view name, PostFx& out);
    static std::vector<std::string> preset_names();

    // Reads [PostFx] (enabled, preset, then any key above as an override) and any player
    // [Preset] sections (nickname plus keys). Returns warnings for unknown keys or presets.
    std::vector<std::string> apply_ini(std::string_view text);
};

}  // namespace sterrenvaarder
