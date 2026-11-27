// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/effects.hpp"

#include "sterrenvaarder/hash.hpp"
#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder {

namespace {

std::string first(const IniSection& s, std::string_view key) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_string() : std::string();
}

float number(const IniSection& s, std::string_view key, float fallback) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_float() : fallback;
}

Color3 color255(const IniSection& s, std::string_view key, Color3 fallback) {
    auto e = s.find(key);
    if (!e || e->values.size() < 3) return fallback;
    return {e->values[0].as_float() / 255.f, e->values[1].as_float() / 255.f, e->values[2].as_float() / 255.f};
}

}  // namespace

EffectDb::EffectDb(const Vfs& vfs, const std::vector<std::string>& files) : vfs_(&vfs) {
    for (auto& f : files) {
        if (!vfs.exists(f)) continue;
        for (auto& sec : parse_ini(vfs.read(f))) {
            std::string nick = first(sec, "nickname");
            if (nick.empty()) continue;
            if (iequals(sec.name, "VisEffect")) {
                VisEffect v;
                v.nickname = nick;
                v.alchemy = first(sec, "alchemy");
                if (auto e = sec.find("effect_crc"); e && !e->values.empty())
                    v.effect_crc = static_cast<std::uint32_t>(e->values[0].as_int());
                for (auto* t : sec.find_all("textures"))
                    if (!t->values.empty()) v.textures.push_back(t->values[0].as_string());
                vis_[to_lower(nick)] = std::move(v);
            } else if (iequals(sec.name, "Effect")) {
                std::string ve = first(sec, "vis_effect");
                if (!ve.empty()) effect_to_vis_[to_lower(nick)] = to_lower(ve);
                std::string vb = first(sec, "vis_beam");
                if (!vb.empty()) effect_to_beam_[to_lower(nick)] = to_lower(vb);
            } else if (iequals(sec.name, "BeamSpear") || iequals(sec.name, "BeamBolt")) {
                BeamSpear b;
                b.nickname = nick;
                b.tip_length = number(sec, "tip_length", b.tip_length);
                b.tail_length = number(sec, "tail_length", b.tail_length);
                b.head_width = number(sec, "head_width", b.head_width);
                b.core_width = number(sec, "core_width", b.core_width);
                b.tip_color = color255(sec, "tip_color", b.tip_color);
                b.core_color = color255(sec, "core_color", b.core_color);
                b.outer_color = color255(sec, "outter_color", b.outer_color);  // spelled so in the data
                b.tail_color = color255(sec, "tail_color", b.tail_color);
                b.head_brightness = number(sec, "head_brightness", b.head_brightness);
                b.trail_brightness = number(sec, "trail_brightness", b.trail_brightness);
                b.head_texture = first(sec, "head_texture");
                b.trail_texture = first(sec, "trail_texture");
                b.flash_size = number(sec, "flash_size", 0);
                beams_[to_lower(nick)] = std::move(b);
            }
        }
    }
}

const VisEffect* EffectDb::vis(std::string_view name) const {
    std::string key = to_lower(name);
    if (auto it = effect_to_vis_.find(key); it != effect_to_vis_.end()) key = it->second;
    auto it = vis_.find(key);
    return it == vis_.end() ? nullptr : &it->second;
}

const BeamSpear* EffectDb::beam(std::string_view name) const {
    std::string key = to_lower(name);
    if (auto it = effect_to_beam_.find(key); it != effect_to_beam_.end()) key = it->second;
    auto it = beams_.find(key);
    return it == beams_.end() ? nullptr : &it->second;
}

ResolvedEffect EffectDb::resolve(std::string_view name) const {
    ResolvedEffect r;
    r.vis = vis(name);
    if (!r.vis || r.vis->alchemy.empty() || !vfs_) return r;
    std::string path = to_lower(r.vis->alchemy);
    auto& lib = libs_[path];
    if (!lib) {
        if (!vfs_->exists(path)) return r;
        try {
            lib = std::make_shared<const AleLibrary>(AleLibrary::parse(vfs_->read(path)));
        } catch (const FormatError&) {
            lib = std::make_shared<const AleLibrary>();
        }
    }
    r.lib = lib;
    // The effect whose name hashes to effect_crc, else one named like the VisEffect, else the first.
    for (auto& e : lib->effects())
        if (r.vis->effect_crc && fl_model_crc_exact(e.name) == r.vis->effect_crc) r.effect = &e;
    if (!r.effect) r.effect = lib->effect(r.vis->nickname);
    if (!r.effect && !lib->effects().empty()) r.effect = &lib->effects().front();
    return r;
}

}  // namespace sterrenvaarder
