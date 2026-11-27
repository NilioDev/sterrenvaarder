// SPDX-License-Identifier: MPL-2.0
// sv-dump: print any Freelancer INI (binary or text) as text, or a UTF file as a tree.
//   sv-dump <file>            file on disk
//   sv-dump --data <DATA> <game path>
// A compiled Lua scene script (.thn) prints its globals as Lua text.
// A resource DLL (.dll) prints its strings as "id<TAB>text" and its infocard ids.
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>

#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/lua32.hpp"
#include "sterrenvaarder/resources.hpp"
#include "sterrenvaarder/utf.hpp"
#include "sterrenvaarder/vfs.hpp"

using namespace sterrenvaarder;

static void print_tree(const UtfNode& n, int depth) {
    std::cout << std::string(depth * 2, ' ') << n.name;
    if (n.leaf) std::cout << "  [" << n.data.size() << " B]\n";
    else {
        std::cout << "/\n";
        for (auto& c : n.children) print_tree(c, depth + 1);
    }
}

int main(int argc, char** argv) {
    try {
        std::vector<std::uint8_t> data;
        if (argc == 4 && std::string(argv[1]) == "--data") {
            Vfs vfs;
            vfs.mount(argv[2], "base");
            data = vfs.read(argv[3]);
        } else if (argc == 2) {
            data = read_file(argv[1]);
        } else {
            std::fprintf(stderr, "usage: sv-dump <file> | sv-dump --data <DATA dir> <game path>\n");
            return 2;
        }
        if (data.size() > 2 && data[0] == 'M' && data[1] == 'Z') {
            auto res = PeResources::parse(data);
            std::vector<std::uint32_t> ids;
            for (auto& [id, _] : res.strings) ids.push_back(id);
            std::sort(ids.begin(), ids.end());
            for (auto id : ids) std::cout << id << '\t' << res.strings[id] << '\n';
            std::cout << "# " << res.infocards.size() << " infocards\n";
            return 0;
        }
        if (data.size() > 5 && data[0] == 0x1B && data[1] == 'L' && data[2] == 'u' && data[3] == 'a') {
            for (auto& [name, v] : LuaChunk::parse(data).globals) std::cout << name << " = " << to_lua_text(v) << "\n";
            return 0;
        }
        if (UtfFile::is_utf(data)) print_tree(UtfFile::parse(std::move(data))->root(), 0);
        else std::cout << to_text(parse_ini(data));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
