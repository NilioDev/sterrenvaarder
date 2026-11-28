// SPDX-License-Identifier: MPL-2.0
// Corpus test: parses every file in a user's DATA directory with the sterrenvaarder readers.
//   sterrenvaarder_corpus <path to Freelancer DATA>
// Needs the user's own game install; nothing from it is stored in this repository.
#include <cstdio>
#include <map>
#include <string>

#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/mission.hpp"
#include "game/mission.hpp"
#include "sterrenvaarder/model.hpp"
#include "sterrenvaarder/texture.hpp"
#include "sterrenvaarder/ale.hpp"
#include "sterrenvaarder/thn.hpp"
#include "sterrenvaarder/utf.hpp"
#include "sterrenvaarder/vfs.hpp"

using namespace sterrenvaarder;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: sterrenvaarder_corpus <DATA dir>\n");
        return 2;
    }
    Vfs vfs;
    vfs.mount(argv[1], "base");
    std::map<std::string, int> ok, fail;
    std::size_t meshes = 0, triangles = 0, textures = 0, models = 0, parts = 0;
    int shown = 0;
    auto report = [&](const std::string& kind, const std::string& path, const std::string& msg) {
        ++fail[kind];
        if (shown++ < 25) std::printf("FAIL [%s] %s: %s\n", kind.c_str(), path.c_str(), msg.c_str());
    };
    std::size_t msn_triggers = 0, msn_dangling = 0;
    std::map<std::string, int> msn_unknown;  // conditions the runner doesn't evaluate yet
    for (auto& path : vfs.list()) {
        auto data = vfs.read(path);
        std::string kind;
        try {
            if (path.ends_with(".thn")) {
                kind = "thn";
                ThnScript::parse(data);
            } else if (path.ends_with(".ale")) {
                kind = "ale";
                auto lib = AleLibrary::parse(data);
                for (auto& e : lib.effects())
                    for (auto& r : e.refs)
                        if (r.node_crc != kAleRoot && !lib.node(r.node_crc))
                            throw FormatError("effect " + e.name + " names a missing node");
            } else if (is_bini(data) || path.ends_with(".ini")) {
                kind = is_bini(data) ? "bini" : "text-ini";
                auto ini = parse_ini(data);
                bool story = false;
                for (auto& sec : ini) story = story || iequals(sec.name, "Mission");
                if (story && path.starts_with("missions/")) {
                    // Story script: every trigger an action or watch names must exist.
                    kind = "mission";
                    auto m = parse_mission(ini);
                    msn_triggers += m.triggers.size();
                    for (auto& t : m.triggers) {
                        for (auto& c : t.conditions) {
                            if (!game::MissionRunner::knows_condition(c.name)) ++msn_unknown[c.name];
                            if (iequals(c.name, "WatchTrigger") && !m.trigger(c.arg(0)))
                                throw FormatError("trigger " + t.nickname + " watches a missing trigger");
                        }
                        for (auto& a : t.actions)
                            if ((iequals(a.name, "ActTrig") || iequals(a.name, "DeactTrig")) && !m.trigger(a.arg(0)))
                                ++msn_dangling;
                    }
                }
            } else if (UtfFile::is_utf(data)) {
                kind = "utf";
                auto f = UtfFile::parse(std::move(data));
                MeshLibrary ml;
                ml.add_from(f);
                if (auto lib = f->root().child("VMeshLibrary")) {
                    kind = "utf+vmesh";
                    meshes += lib->children.size();
                }
                MaterialLibrary mats;
                mats.add_from(f);
                if (auto tl = f->root().child("Texture library")) {
                    kind = "utf+textures";
                    for (auto& t : tl->children) {
                        if (t.leaf) continue;
                        if (is_animation_node(t)) {
                            auto a = parse_animation_node(t);
                            if (a.frames.size() != a.frame_count) throw FormatError("bad animation " + t.name);
                            continue;
                        }
                        auto img = decode_texture_node(t);
                        if (img.width == 0 || img.rgba.size() != std::size_t(img.width) * img.height * 4)
                            throw FormatError("bad image " + t.name);
                        ++textures;
                    }
                }
                if (path.ends_with(".cmp") || path.ends_with(".3db")) {
                    kind = "model";
                    Model m = Model::load(f);
                    ++models;
                    for (auto& p : m.parts) {
                        ++parts;
                        for (auto& l : p.lods) {
                            const VMeshData* vm = ml.mesh(l.ref.mesh_id);
                            if (!vm) continue;  // mesh lives in a shared library elsewhere
                            std::size_t cursor = l.ref.start_index;
                            for (std::size_t g = l.ref.start_group; g < std::size_t(l.ref.start_group) + l.ref.group_count; ++g) {
                                const auto& grp = vm->groups.at(g);
                                std::size_t base = std::size_t(l.ref.start_vertex) + grp.start_vertex;
                                for (std::size_t k = 0; k < grp.index_count; ++k) {
                                    std::size_t v = base + vm->indices.at(cursor + k);
                                    if (v >= vm->positions.size() || v > std::size_t(l.ref.start_vertex) + grp.end_vertex)
                                        throw FormatError("index out of range in " + p.name);
                                }
                                cursor += grp.index_count;
                                triangles += grp.index_count / 3;
                            }
                        }
                    }
                }
            } else {
                continue;  // audio, video, scripts: other readers
            }
            ++ok[kind];
        } catch (const std::exception& e) {
            report(kind, path, e.what());
        }
    }
    int total_fail = 0;
    for (auto& [k, n] : ok) std::printf("%-14s ok %6d   failed %d\n", k.c_str(), n, fail[k]);
    for (auto& [k, n] : fail) total_fail += n;
    std::printf("models %zu, parts %zu, meshes %zu, triangles (all LODs) %zu, textures decoded %zu\n", models, parts,
                meshes, triangles, textures);
    std::printf("mission triggers %zu, actions naming a missing trigger %zu\n", msn_triggers, msn_dangling);
    for (auto& [n, c] : msn_unknown) std::printf("  condition not evaluated yet: %s (%d)\n", n.c_str(), c);
    return total_fail ? 1 : 0;
}
