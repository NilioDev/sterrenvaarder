// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/vfs.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {

std::string Vfs::normalize(std::string_view p) {
    std::string r;
    r.reserve(p.size());
    for (char c : p) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if (c == '/' && (r.empty() || r.back() == '/')) continue;  // drop leading / doubled
        r.push_back(c);
    }
    // Resolve "." and ".." segments.
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= r.size()) {
        std::size_t end = r.find('/', start);
        if (end == std::string::npos) end = r.size();
        std::string seg = r.substr(start, end - start);
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!seg.empty() && seg != ".") {
            parts.push_back(std::move(seg));
        }
        start = end + 1;
    }
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out.push_back('/');
        out += parts[i];
    }
    return out;
}

void Vfs::mount(const std::filesystem::path& dir, std::string name) {
    namespace fs = std::filesystem;
    if (!fs::is_directory(dir)) throw std::runtime_error("not a directory: " + dir.string());
    std::size_t layer = layers_.size();
    layers_.push_back(name.empty() ? dir.string() : std::move(name));
    for (auto& e : fs::recursive_directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        auto rel = fs::relative(e.path(), dir).generic_string();
        auto key = normalize(rel);
        auto it = index_.find(key);
        if (it == index_.end()) {
            index_[key] = Entry{e.path(), layer, {}};
        } else {
            it->second.shadowed.insert(it->second.shadowed.begin(), it->second.real);
            it->second.real = e.path();
            it->second.layer = layer;
        }
    }
}

std::optional<std::filesystem::path> Vfs::resolve(std::string_view game_path) const {
    auto it = index_.find(normalize(game_path));
    if (it == index_.end()) return std::nullopt;
    return it->second.real;
}

std::vector<std::filesystem::path> Vfs::resolve_all(std::string_view game_path) const {
    auto it = index_.find(normalize(game_path));
    if (it == index_.end()) return {};
    std::vector<std::filesystem::path> r{it->second.real};
    r.insert(r.end(), it->second.shadowed.begin(), it->second.shadowed.end());
    return r;
}

std::optional<std::string> Vfs::layer_of(std::string_view game_path) const {
    auto it = index_.find(normalize(game_path));
    if (it == index_.end()) return std::nullopt;
    return layers_[it->second.layer];
}

std::vector<std::uint8_t> Vfs::read(std::string_view game_path) const {
    auto p = resolve(game_path);
    if (!p) throw std::runtime_error("file not found in data: " + std::string(game_path));
    return read_file(*p);
}

std::vector<std::string> Vfs::list(std::string_view suffix) const {
    std::string suf = to_lower(suffix);
    std::vector<std::string> r;
    for (auto& [k, v] : index_)
        if (suf.empty() || (k.size() >= suf.size() && k.compare(k.size() - suf.size(), suf.size(), suf) == 0))
            r.push_back(k);
    std::sort(r.begin(), r.end());
    return r;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    f.seekg(0, std::ios::end);
    auto n = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::uint8_t> buf(n);
    if (n) f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n));
    return buf;
}

}  // namespace sterrenvaarder
