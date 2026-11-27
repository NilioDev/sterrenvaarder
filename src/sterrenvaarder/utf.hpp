// SPDX-License-Identifier: MPL-2.0
#pragma once
// UTF: Freelancer's tree container used by models (.cmp .3db), materials (.mat),
// textures (.txm), effects (.ale), deformables (.dfm), spheres (.sph) and more.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {

struct UtfNode {
    std::string name;
    bool leaf = false;
    Bytes data;                     // leaf payload (view into the owning UtfFile)
    std::vector<UtfNode> children;  // directory entries

    // Case-insensitive lookup of a direct child, or a '/'-separated path.
    const UtfNode* child(std::string_view name) const;
    const UtfNode* find(std::string_view path) const;

    template <typename T>
    T as() const { return read_le<T>(data, 0); }
    std::string as_string() const;  // NUL-terminated text leaf
};

class UtfFile {
public:
    static std::shared_ptr<UtfFile> parse(std::vector<std::uint8_t> data);
    static bool is_utf(Bytes data);
    const UtfNode& root() const { return root_; }

private:
    std::vector<std::uint8_t> buf_;
    UtfNode root_;
};

}  // namespace sterrenvaarder
