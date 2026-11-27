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
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sterrenvaarder {

std::vector<std::uint8_t> read_file(const std::filesystem::path& p);

class Vfs {
public:
    // Mounts a directory as a new top layer. `name` is shown in diagnostics.
    void mount(const std::filesystem::path& dir, std::string name = {});

    std::optional<std::filesystem::path> resolve(std::string_view game_path) const;
    bool exists(std::string_view game_path) const { return resolve(game_path).has_value(); }
    std::vector<std::uint8_t> read(std::string_view game_path) const;  // throws if missing

    // Every layer's copy of a file, top layer first. Lets callers fall back to a lower
    // layer (ultimately the original data) when a mod's version fails to load.
    std::vector<std::filesystem::path> resolve_all(std::string_view game_path) const;

    // Calls `load(bytes)` on the top layer's copy, then lower layers until one succeeds.
    template <typename F>
    auto load_with_fallback(std::string_view game_path, F&& load) const -> decltype(load(std::vector<std::uint8_t>{})) {
        auto layers = resolve_all(game_path);
        if (layers.empty()) throw std::runtime_error("file not found in data: " + std::string(game_path));
        for (std::size_t i = 0; i < layers.size(); ++i) {
            try {
                return load(read_file(layers[i]));
            } catch (const std::exception&) {
                if (i + 1 == layers.size()) throw;
            }
        }
        throw std::runtime_error("unreachable");
    }

    // Which layer a file comes from (for mod conflict reports).
    std::optional<std::string> layer_of(std::string_view game_path) const;

    // All files, normalized ("ships/liberty/li_elite/li_elite.cmp"), optionally by suffix.
    std::vector<std::string> list(std::string_view suffix = {}) const;

    static std::string normalize(std::string_view game_path);

private:
    struct Entry {
        std::filesystem::path real;
        std::size_t layer;
        std::vector<std::filesystem::path> shadowed;  // lower layers' copies, top first
    };
    std::vector<std::string> layers_;
    std::unordered_map<std::string, Entry> index_;
};

}  // namespace sterrenvaarder
