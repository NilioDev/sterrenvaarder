// SPDX-License-Identifier: MPL-2.0
#pragma once
// Freelancer INI files, both binary (BINI) and text. Order and duplicates are kept,
// because the game relies on both (e.g. many [Object] sections, repeated "hp_type" keys).
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {

struct IniValue {
    enum class Type { Int, Float, String } type = Type::String;
    std::int32_t i = 0;
    float f = 0;
    std::string s;

    float as_float() const;
    std::int32_t as_int() const;
    std::string as_string() const;
    bool as_bool() const;
};

struct IniEntry {
    std::string key;
    std::vector<IniValue> values;
    const IniValue* at(std::size_t idx) const { return idx < values.size() ? &values[idx] : nullptr; }
};

struct IniSection {
    std::string name;
    std::vector<IniEntry> entries;
    const IniEntry* find(std::string_view key) const;  // first match, case-insensitive
    std::vector<const IniEntry*> find_all(std::string_view key) const;
};

using IniFile = std::vector<IniSection>;

IniFile parse_ini(Bytes data);  // detects BINI vs text
IniFile parse_bini(Bytes data);
IniFile parse_text_ini(std::string_view text);
bool is_bini(Bytes data);

// Writes any IniFile as readable text (used to decompile BINI for modders).
std::string to_text(const IniFile& ini);

}  // namespace sterrenvaarder
