// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/sounds.hpp"

#include <cmath>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {
namespace {

std::string str(const IniEntry& e) { return e.at(0) ? e.at(0)->as_string() : std::string(); }

}  // namespace

float SoundDef::gain() const { return std::pow(10.f, attenuation / 20.f); }

void SoundDb::add(const IniFile& ini) {
    for (auto& sec : ini) {
        if (!iequals(sec.name, "Sound")) continue;
        SoundDef d;
        for (auto& e : sec.entries) {
            if (iequals(e.key, "nickname")) d.nickname = str(e);
            else if (iequals(e.key, "file")) d.file = str(e);
            else if (iequals(e.key, "type")) d.type = str(e);
            else if (iequals(e.key, "attenuation")) d.attenuation = e.at(0) ? e.at(0)->as_float() : 0;
            else if (iequals(e.key, "streamer")) d.streamer = !e.at(0) || iequals(str(e), "true") || e.at(0)->as_float() != 0;
        }
        if (!d.nickname.empty()) sounds_[to_lower(d.nickname)] = std::move(d);
    }
}

const SoundDef* SoundDb::find(std::string_view nickname) const {
    auto it = sounds_.find(to_lower(nickname));
    return it == sounds_.end() ? nullptr : &it->second;
}

SystemMusic parse_system_music(const IniFile& ini) {
    SystemMusic m;
    for (auto& sec : ini) {
        if (!iequals(sec.name, "Music")) continue;
        for (auto& e : sec.entries) {
            if (iequals(e.key, "space")) m.space = str(e);
            else if (iequals(e.key, "danger")) m.danger = str(e);
            else if (iequals(e.key, "battle")) m.battle = str(e);
        }
    }
    return m;
}

}  // namespace sterrenvaarder
