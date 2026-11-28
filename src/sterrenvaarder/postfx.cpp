// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/postfx.hpp"

#include <algorithm>
#include <map>

#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {
namespace {

float* field(PostFx& p, std::string_view key) {
    static const std::pair<const char*, float PostFx::*> keys[] = {
        {"bloom", &PostFx::bloom},         {"sharpen", &PostFx::sharpen},     {"saturation", &PostFx::saturation},
        {"vibrance", &PostFx::vibrance},   {"contrast", &PostFx::contrast},   {"brightness", &PostFx::brightness},
        {"temperature", &PostFx::temperature}, {"vignette", &PostFx::vignette}, {"grain", &PostFx::grain},
        {"chromatic", &PostFx::chromatic},
    };
    for (auto& [name, member] : keys)
        if (iequals(key, name)) return &(p.*member);
    return nullptr;
}

// Sets the look's values from a section; "enabled" and "preset" are handled by the caller.
void read_values(const IniSection& sec, PostFx& p, std::vector<std::string>& warnings) {
    for (auto& e : sec.entries) {
        if (iequals(e.key, "enabled") || iequals(e.key, "preset") || iequals(e.key, "nickname")) continue;
        if (float* f = field(p, e.key)) {
            if (!e.values.empty()) *f = e.values[0].as_float();
        } else {
            warnings.push_back("unknown key " + e.key);
        }
    }
}

}  // namespace

bool PostFx::preset(std::string_view name, PostFx& out) {
    PostFx p;
    p.enabled = true;
    if (iequals(name, "original")) {
        p.enabled = false;
    } else if (iequals(name, "vivid")) {
        p.saturation = 1.15f;
        p.vibrance = 0.25f;
        p.contrast = 1.08f;
        p.bloom = 0.08f;
        p.sharpen = 0.2f;
    } else if (iequals(name, "cinematic")) {
        p.contrast = 1.12f;
        p.saturation = 0.9f;
        p.temperature = 0.12f;
        p.bloom = 0.15f;
        p.vignette = 0.35f;
        p.grain = 0.15f;
        p.chromatic = 0.2f;
    } else if (iequals(name, "crisp")) {
        p.sharpen = 0.5f;
        p.contrast = 1.05f;
        p.vibrance = 0.1f;
    } else {
        return false;
    }
    out = p;
    return true;
}

std::vector<std::string> PostFx::preset_names() { return {"original", "vivid", "cinematic", "crisp"}; }

std::vector<std::string> PostFx::apply_ini(std::string_view text) {
    std::vector<std::string> warnings;
    IniFile ini = parse_text_ini(text);
    std::map<std::string, const IniSection*> own;  // player presets by lower-case nickname
    for (auto& sec : ini)
        if (iequals(sec.name, "Preset"))
            if (auto* n = sec.find("nickname"); n && !n->values.empty()) own[to_lower(n->values[0].as_string())] = &sec;
    for (auto& sec : ini) {
        if (iequals(sec.name, "Preset")) continue;
        if (!iequals(sec.name, "PostFx")) {
            warnings.push_back("unknown section [" + sec.name + "]");
            continue;
        }
        if (auto* pr = sec.find("preset"); pr && !pr->values.empty()) {
            std::string name = pr->values[0].as_string();
            if (auto it = own.find(to_lower(name)); it != own.end()) {
                *this = PostFx{};
                enabled = true;
                read_values(*it->second, *this, warnings);
            } else if (!preset(name, *this)) {
                warnings.push_back("unknown preset " + name);
            }
        }
        read_values(sec, *this, warnings);
        if (auto* en = sec.find("enabled"); en && !en->values.empty()) enabled = en->values[0].as_bool();
    }
    return warnings;
}

}  // namespace sterrenvaarder
