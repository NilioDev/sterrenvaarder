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

bool flag(const IniSection& s, std::string_view key, bool fallback) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? e->values[0].as_bool() : fallback;
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
            } else if (iequals(sec.name, "explosion")) {
                ExplosionArch ex;
                ex.nickname = nick;
                ex.effect = first(sec, "effect");
                for (auto* d : sec.find_all("debris_type"))
                    if (!d->values.empty())
                        ex.debris_types.emplace_back(d->values[0].as_string(), d->values.size() > 1 ? d->values[1].as_float() : 1.f);
                ex.debris_impulse = number(sec, "debris_impulse", 0);
                explosions_[to_lower(nick)] = std::move(ex);
            } else if (iequals(sec.name, "Debris")) {
                DebrisType d;
                d.nickname = nick;
                if (auto* e = sec.find("lifetime"); e && e->values.size() >= 2) {
                    d.lifetime_min = e->values[0].as_float();
                    d.lifetime_max = e->values[1].as_float();
                }
                d.trail = first(sec, "trail");
                d.explosion = first(sec, "explosion");
                debris_[to_lower(nick)] = std::move(d);
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

const std::string& EffectDb::explosion_effect(std::string_view explosion) const {
    static const std::string none;
    const ExplosionArch* ex = this->explosion(explosion);
    return ex ? ex->effect : none;
}

const ExplosionArch* EffectDb::explosion(std::string_view nickname) const {
    auto it = explosions_.find(to_lower(std::string(nickname)));
    return it == explosions_.end() ? nullptr : &it->second;
}

const DebrisType* EffectDb::debris(std::string_view nickname) const {
    auto it = debris_.find(to_lower(std::string(nickname)));
    return it == debris_.end() ? nullptr : &it->second;
}

void EffectDb::add_fuses(const std::vector<std::string>& files) {
    if (!vfs_) return;
    for (auto& f : files) {
        if (!vfs_->exists(f)) continue;
        Fuse* cur = nullptr;
        // Sections belong to the [fuse] above them.
        for (auto& sec : parse_ini(vfs_->read(f))) {
            if (iequals(sec.name, "fuse")) {
                std::string name = first(sec, "name");
                Fuse fu;
                fu.name = name;
                fu.lifetime = number(sec, "lifetime", 1);
                fu.death_fuse = flag(sec, "death_fuse", false);
                cur = &(fuses_[to_lower(name)] = std::move(fu));
            } else if (cur && iequals(sec.name, "start_effect")) {
                Fuse::Start st;
                st.effect = first(sec, "effect");
                for (auto* h : sec.find_all("hardpoint"))
                    if (!h->values.empty()) st.hardpoints.push_back(h->values[0].as_string());
                st.at_t = number(sec, "at_t", 0);
                st.attached = flag(sec, "attached", true);
                if (!st.effect.empty()) cur->starts.push_back(std::move(st));
            }
        }
    }
}

const Fuse* EffectDb::fuse(std::string_view name) const {
    auto it = fuses_.find(to_lower(std::string(name)));
    return it == fuses_.end() ? nullptr : &it->second;
}

}  // namespace sterrenvaarder
