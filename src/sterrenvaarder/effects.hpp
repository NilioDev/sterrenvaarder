// SPDX-License-Identifier: MPL-2.0
#pragma once
// Effect names from the game data: [Effect] (effects.ini: a named effect and its visual
// effect) and [VisEffect] (fx/*_ale.ini: the .ale file, which effect in it, and the texture
// libraries it draws with). Engines, munitions and explosions name either kind.
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/ale.hpp"
#include "sterrenvaarder/vfs.hpp"

namespace sterrenvaarder {

struct VisEffect {
    std::string nickname;
    std::string alchemy;  // .ale path
    std::uint32_t effect_crc = 0;
    std::vector<std::string> textures;  // .txm libraries
};

// [BeamSpear] / [BeamBolt] from beam_effects.ini: how a gun's bolt is drawn.
struct BeamSpear {
    std::string nickname;
    float tip_length = 5, tail_length = 60;
    float head_width = 2, core_width = 2;
    Color3 tip_color{0, 0, 0}, core_color{1, 1, 1}, outer_color{1, 1, 1}, tail_color{1, 1, 1};  // 0..1
    float head_brightness = 1, trail_brightness = 1;
    std::string head_texture, trail_texture;
    float flash_size = 0;
};

// A loaded visual effect: the library it lives in and the effect inside it.
struct ResolvedEffect {
    const VisEffect* vis = nullptr;
    std::shared_ptr<const AleLibrary> lib;
    const AleEffect* effect = nullptr;
    explicit operator bool() const { return effect != nullptr; }
};

class EffectDb {
public:
    EffectDb() = default;
    EffectDb(const Vfs& vfs, const std::vector<std::string>& files);

    // Looks up an [Effect] or [VisEffect] nickname and loads its .ale file (cached).
    ResolvedEffect resolve(std::string_view name) const;
    const VisEffect* vis(std::string_view name) const;
    // The bolt look named by an [Effect]'s vis_beam (or a beam nickname itself).
    const BeamSpear* beam(std::string_view name) const;
    std::size_t size() const { return vis_.size(); }

private:
    const Vfs* vfs_ = nullptr;
    std::unordered_map<std::string, VisEffect> vis_;
    std::unordered_map<std::string, std::string> effect_to_vis_;  // [Effect] -> vis_effect
    std::unordered_map<std::string, std::string> effect_to_beam_;  // [Effect] -> vis_beam
    std::unordered_map<std::string, BeamSpear> beams_;
    mutable std::unordered_map<std::string, std::shared_ptr<const AleLibrary>> libs_;
};

}  // namespace sterrenvaarder
