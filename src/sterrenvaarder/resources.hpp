// SPDX-License-Identifier: MPL-2.0
#pragma once
// Text resources from the game's own resource DLLs (Win32 PE string tables and HTML
// resources). The DLLs are only read as data, never loaded or executed.
//
// The game numbers its text globally: id = (dll index << 16) | local id, where index 0 is
// resources.dll and the [Resources] DLLs from Freelancer.ini follow in order.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {

// Raw resources of one PE file: string table entries and HTML (infocard) resources.
struct PeResources {
    std::unordered_map<std::uint32_t, std::string> strings;    // local id -> UTF-8
    std::unordered_map<std::uint32_t, std::string> infocards;  // local id -> UTF-8 (XML-like RDL)
    static PeResources parse(Bytes pe);                        // throws FormatError
};

std::string utf16le_to_utf8(Bytes data);

class GameStrings {
public:
    // Adds a DLL at the given index (0 = resources.dll).
    void add(std::uint32_t index, PeResources res);
    std::optional<std::string> string(std::uint32_t id) const;
    std::optional<std::string> infocard(std::uint32_t id) const;
    // The string, or `fallback` when the id is 0 or unknown.
    std::string name(std::uint32_t id, std::string_view fallback) const;
    std::size_t size() const { return strings_.size(); }

private:
    std::unordered_map<std::uint32_t, std::string> strings_, infocards_;
};

// DLL names from the [Resources] section of Freelancer.ini, in order.
std::vector<std::string> resource_dlls(std::string_view freelancer_ini);

}  // namespace sterrenvaarder
