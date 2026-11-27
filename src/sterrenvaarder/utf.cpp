// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/utf.hpp"

#include <unordered_set>

namespace sterrenvaarder {

const UtfNode* UtfNode::child(std::string_view n) const {
    for (auto& c : children)
        if (iequals(c.name, n)) return &c;
    return nullptr;
}

const UtfNode* UtfNode::find(std::string_view path) const {
    const UtfNode* cur = this;
    while (cur && !path.empty()) {
        auto slash = path.find('/');
        cur = cur->child(path.substr(0, slash));
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1);
    }
    return cur;
}

std::string UtfNode::as_string() const {
    std::size_t n = 0;
    while (n < data.size() && data[n]) ++n;
    return std::string(reinterpret_cast<const char*>(data.data()), n);
}

bool UtfFile::is_utf(Bytes d) { return d.size() >= 56 && std::memcmp(d.data(), "UTF ", 4) == 0; }

// Header (after "UTF "): u32 version 0x101, u32 tree offset, u32 tree size, u32 reserved,
// u32 entry size (44), u32 names offset, u32 names allocated, u32 names used,
// u32 data offset, ...
// Node (44 bytes): u32 next sibling, u32 name offset, u32 flags (0x10 dir, 0x80 leaf),
// u32 sharing, u32 child offset (dir) or data offset (leaf), u32 allocated size,
// u32 size, u32 uncompressed size, 3 x u32 timestamps.
std::shared_ptr<UtfFile> UtfFile::parse(std::vector<std::uint8_t> data) {
    auto f = std::shared_ptr<UtfFile>(new UtfFile());
    f->buf_ = std::move(data);
    Bytes b(f->buf_);
    if (!is_utf(b)) throw FormatError("not a UTF file");
    auto tree_off = read_le<std::uint32_t>(b, 8);
    auto tree_size = read_le<std::uint32_t>(b, 12);
    auto names_off = read_le<std::uint32_t>(b, 24);
    auto data_off = read_le<std::uint32_t>(b, 36);
    if (std::size_t(tree_off) + tree_size > b.size()) throw FormatError("UTF tree out of range");

    std::unordered_set<std::uint32_t> seen;
    // Iterative-over-siblings, recursive-over-depth walk with cycle and depth guards.
    auto walk = [&](auto&& self, std::uint32_t off, int depth, std::vector<UtfNode>& out) -> void {
        if (depth > 64) throw FormatError("UTF tree too deep");
        while (true) {
            if (off + 44u > tree_size) throw FormatError("UTF node out of range");
            if (!seen.insert(off).second) throw FormatError("UTF node cycle");
            std::size_t p = std::size_t(tree_off) + off;
            auto next = read_le<std::uint32_t>(b, p);
            auto name_off = read_le<std::uint32_t>(b, p + 4);
            auto flags = read_le<std::uint32_t>(b, p + 8);
            auto child = read_le<std::uint32_t>(b, p + 16);
            auto size = read_le<std::uint32_t>(b, p + 24);
            UtfNode n;
            n.name = std::string(read_cstr(b, std::size_t(names_off) + name_off));
            if (flags & 0x10) {
                // An empty directory has its child offset set to 0 (which is the root).
                if (child != 0) self(self, child, depth + 1, n.children);
            } else {
                n.leaf = true;
                std::size_t start = std::size_t(data_off) + child;
                if (start + size > b.size()) throw FormatError("UTF leaf data out of range: " + n.name);
                n.data = b.subspan(start, size);
            }
            out.push_back(std::move(n));
            if (next == 0) break;
            off = next;
        }
    };
    std::vector<UtfNode> top;
    walk(walk, 0, 0, top);
    if (top.size() != 1) throw FormatError("UTF: expected one root node");
    f->root_ = std::move(top.front());
    return f;
}

}  // namespace sterrenvaarder
