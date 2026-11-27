// SPDX-License-Identifier: MPL-2.0
// sv-dump: print any Freelancer INI (binary or text) as text, or a UTF file as a tree.
//   sv-dump <file>            file on disk
//   sv-dump --data <DATA> <game path>
// A compiled Lua scene script (.thn) prints its globals as Lua text.
// An effect file (.ale) prints its effects and nodes.
// A resource DLL (.dll) prints its strings as "id<TAB>text" and its infocard ids.
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>

#include "sterrenvaarder/ale.hpp"
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
        if (UtfFile::is_utf(data) && UtfFile::parse(data)->root().find("ALEffectLib/ALEffectLib")) {
            auto lib = AleLibrary::parse(data);
            for (auto& e : lib.effects()) {
                std::cout << "effect " << e.name << "\n";
                for (auto& r : e.refs) {
                    const AleNode* n = lib.node(r.node_crc);
                    std::cout << "  #" << r.id << " parent " << (r.parent == 0x8000 ? std::string("-") : std::to_string(r.parent))
                              << "  " << (r.node_crc == kAleRoot ? std::string("(root)") : n ? n->kind + " " + n->name : "?") << "\n";
                }
                for (auto& [a, b] : e.pairs) std::cout << "  emitter #" << a << " -> appearance #" << b << "\n";
            }
            for (auto& n : lib.nodes()) {
                std::cout << "node " << n.kind << " " << n.name << ": " << n.props.size() << " properties";
                if (auto* t = n.get<std::string>(ale::kTexture)) std::cout << ", texture " << *t;
                if (n.is_emitter())
                    std::cout << ", " << n.number(ale::kFrequency, 0) << "/s, life " << n.number(ale::kParticleLife, 0)
                              << " s, speed " << n.number(ale::kPressure, 0) << " m/s";
                std::cout << "\n";
            }
            return 0;
        }
        if (UtfFile::is_utf(data)) print_tree(UtfFile::parse(std::move(data))->root(), 0);
        else std::cout << to_text(parse_ini(data));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
