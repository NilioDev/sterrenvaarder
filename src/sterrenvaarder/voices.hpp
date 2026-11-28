// SPDX-License-Identifier: MPL-2.0
#pragma once
// Voices of the player's data (audio/voices_*.ini): each [Voice] is followed by [Sound]
// sections that list its spoken lines (msg). The lines live in audio/<voice nickname>.utf,
// each the UTF leaf named "0x%08X" of fl_create_id(msg): a RIFF WAVE file.
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/utf.hpp"
#include "sterrenvaarder/vfs.hpp"

namespace sterrenvaarder {

struct VoiceLine {
    float attenuation = 0;  // dB
    float duration = 0;     // seconds, when listed
    int priority = 0;
};

struct Voice {
    std::string nickname;
    std::vector<std::string> scripts;                    // animation scripts for speaking (script =)
    std::unordered_map<std::string, VoiceLine> lines;    // lower-case msg
};

// The UTF node name of a spoken line.
std::string voice_node_name(std::string_view msg);

class VoiceDb {
public:
    // Reads one voices ini. [Voice] with extend = adds the lines that follow to that voice.
    void add(const IniFile& ini);
    const Voice* voice(std::string_view nickname) const;
    std::size_t size() const { return voices_.size(); }
    const std::unordered_map<std::string, Voice>& all() const { return voices_; }

    // The WAVE bytes of one line in one voice, from its .utf file (opened once, kept).
    std::optional<std::vector<std::uint8_t>> line_wave(const Vfs& vfs, std::string_view voice, std::string_view msg);

private:
    std::unordered_map<std::string, Voice> voices_;  // lower-case nickname
    std::unordered_map<std::string, std::shared_ptr<UtfFile>> files_;  // by normalised path
};

}  // namespace sterrenvaarder
