// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/voices.hpp"

#include <algorithm>
#include <cstdio>

#include "sterrenvaarder/bytes.hpp"
#include "sterrenvaarder/hash.hpp"

namespace sterrenvaarder {
namespace {

std::string str(const IniEntry& e) { return e.at(0) ? e.at(0)->as_string() : std::string(); }

std::string normal_path(std::string p) {
    p = to_lower(p);
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

}  // namespace

std::string voice_node_name(std::string_view msg) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08X", static_cast<unsigned>(fl_create_id(msg)));
    return buf;
}

void VoiceDb::add(const IniFile& ini) {
    Voice* cur = nullptr;
    for (auto& sec : ini) {
        if (iequals(sec.name, "Voice")) {
            cur = nullptr;
            std::string name;
            if (auto* e = sec.find("nickname")) name = str(*e);
            else if (auto* x = sec.find("extend")) name = str(*x);
            if (name.empty()) continue;
            cur = &voices_[to_lower(name)];
            if (cur->nickname.empty()) cur->nickname = name;
            for (auto* s : sec.find_all("script"))
                if (auto p = str(*s); !p.empty()) cur->scripts.push_back(p);
        } else if (iequals(sec.name, "Sound") && cur) {
            auto* m = sec.find("msg");
            if (!m || str(*m).empty()) continue;
            VoiceLine l;
            if (auto* a = sec.find("attenuation"); a && a->at(0)) l.attenuation = a->at(0)->as_float();
            if (auto* d = sec.find("duration"); d && d->at(0)) l.duration = d->at(0)->as_float();
            if (auto* p = sec.find("priority"); p && p->at(0)) l.priority = static_cast<int>(p->at(0)->as_float());
            cur->lines[to_lower(str(*m))] = l;
        }
    }
}

const Voice* VoiceDb::voice(std::string_view nickname) const {
    auto it = voices_.find(to_lower(nickname));
    return it == voices_.end() ? nullptr : &it->second;
}

std::optional<std::vector<std::uint8_t>> VoiceDb::line_wave(const Vfs& vfs, std::string_view voice, std::string_view msg) {
    const Voice* v = this->voice(voice);
    if (!v) return std::nullopt;
    std::string node = voice_node_name(msg);
    std::string path = normal_path("audio/" + v->nickname + ".utf");
    auto it = files_.find(path);
    if (it == files_.end()) {
        std::shared_ptr<UtfFile> f;
        try {
            if (vfs.exists(path)) f = UtfFile::parse(vfs.read(path));
        } catch (const std::exception&) {
        }
        it = files_.emplace(path, std::move(f)).first;
    }
    if (it->second)
        if (const UtfNode* n = it->second->root().child(node); n && n->leaf)
            return std::vector<std::uint8_t>(n->data.begin(), n->data.end());
    return std::nullopt;
}

}  // namespace sterrenvaarder
