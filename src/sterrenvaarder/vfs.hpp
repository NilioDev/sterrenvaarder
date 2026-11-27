// SPDX-License-Identifier: MPL-2.0
#pragma once
// Layered, case-insensitive virtual file system.
//
// The original game data is mounted first; mods are mounted on top. A file in a later
// layer replaces the same path in an earlier one, so mods never modify the user's
// install. Paths use Freelancer's convention: relative to DATA, '\' or '/' separators,
// any case ("SHIPS\\Liberty\\li_elite\\li_elite.cmp").
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sterrenvaarder {

class Vfs {
public:
    // Mounts a directory as a new top layer. `name` is shown in diagnostics.
    void mount(const std::filesystem::path& dir, std::string name = {});

    std::optional<std::filesystem::path> resolve(std::string_view game_path) const;
    bool exists(std::string_view game_path) const { return resolve(game_path).has_value(); }
    std::vector<std::uint8_t> read(std::string_view game_path) const;  // throws if missing

    // Which layer a file comes from (for mod conflict reports).
    std::optional<std::string> layer_of(std::string_view game_path) const;

    // All files, normalized ("ships/liberty/li_elite/li_elite.cmp"), optionally by suffix.
    std::vector<std::string> list(std::string_view suffix = {}) const;

    static std::string normalize(std::string_view game_path);

private:
    struct Entry {
        std::filesystem::path real;
        std::size_t layer;
    };
    std::vector<std::string> layers_;
    std::unordered_map<std::string, Entry> index_;
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& p);

}  // namespace sterrenvaarder
