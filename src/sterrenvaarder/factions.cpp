// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/factions.hpp"

#include <cstdlib>

#include "sterrenvaarder/ini.hpp"

namespace sterrenvaarder {

namespace {

std::uint32_t id_of(const IniSection& s, std::string_view key) {
    auto e = s.find(key);
    return (e && !e->values.empty()) ? static_cast<std::uint32_t>(e->values[0].as_int()) : 0;
}

}  // namespace

Factions::Factions(const Vfs& vfs, std::string_view initial_world) {
    if (!vfs.exists(initial_world)) return;
    for (auto& sec : parse_ini(vfs.read(initial_world))) {
        if (!iequals(sec.name, "Group")) continue;
        Faction f;
        auto n = sec.find("nickname");
        if (!n || n->values.empty()) continue;
        f.nickname = n->values[0].as_string();
        f.ids_name = id_of(sec, "ids_name");
        f.ids_short_name = id_of(sec, "ids_short_name");
        for (auto* r : sec.find_all("rep"))
            if (r->values.size() >= 2) f.rep[to_lower(r->values[1].as_string())] = r->values[0].as_float();
        factions_[to_lower(f.nickname)] = std::move(f);
    }
}

const Faction* Factions::faction(std::string_view n) const {
    auto it = factions_.find(to_lower(n));
    return it == factions_.end() ? nullptr : &it->second;
}

float Factions::rep(std::string_view from, std::string_view to) const {
    auto f = faction(from);
    if (!f) return 0;
    auto it = f->rep.find(to_lower(to));
    return it == f->rep.end() ? 0 : it->second;
}

std::map<std::string, float> read_house_lines(std::string_view text) {
    std::map<std::string, float> out;
    // Character files have placeholders such as %%NAME%% that are not valid INI values;
    // only the house lines matter here, so read them line by line.
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        auto eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        std::string_view key = line.substr(0, eq);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.remove_suffix(1);
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.remove_prefix(1);
        if (!iequals(key, "house")) continue;
        std::string rest(line.substr(eq + 1));
        auto comma = rest.find(',');
        if (comma == std::string::npos) continue;
        float v = std::strtof(rest.c_str(), nullptr);
        std::string group = rest.substr(comma + 1);
        auto b = group.find_first_not_of(" \t"), e = group.find_last_not_of(" \t\r;");
        if (b == std::string::npos) continue;
        group = group.substr(b, e - b + 1);
        if (auto sc = group.find(';'); sc != std::string::npos) group = group.substr(0, group.find_last_not_of(" \t", sc - 1) + 1);
        out[to_lower(group)] = v;
    }
    return out;
}

}  // namespace sterrenvaarder
