// SPDX-License-Identifier: MPL-2.0
// Corpus test: parses every file in a user's DATA directory with the sterrenvaarder readers.
//   sterrenvaarder_corpus <path to Freelancer DATA>
// Needs the user's own game install; nothing from it is stored in this repository.
#include <cstdio>
#include <map>
#include <string>

#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/mission.hpp"
#include "sterrenvaarder/population.hpp"
#include "sterrenvaarder/dfm.hpp"
#include "sterrenvaarder/mbases.hpp"
#include "sterrenvaarder/audio.hpp"
#include "sterrenvaarder/sounds.hpp"
#include "sterrenvaarder/universe.hpp"
#include "game/skin.hpp"
#include "game/thn_player.hpp"
#include "sterrenvaarder/anm.hpp"
#include "sterrenvaarder/characters.hpp"
#include <algorithm>
#include <cmath>
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
    std::size_t dfm_tris = 0, dfm_bones = 0, dfm_bad_bones = 0, anm_scripts = 0;
    std::vector<std::string> scene_costumes, scene_anims;  // what scene characters wear and play
    std::vector<std::string> scene_sounds;                 // SOUND entity sound names
    std::size_t thn_sound_events = 0, thn_bad_sound_events = 0;
    double wav_seconds = 0;
    std::size_t thn_paths = 0, thn_short_paths = 0, thn_rides = 0, thn_bad_rides = 0;
    std::size_t thn_views = 0, thn_moves = 0, thn_cuts = 0, thn_bad_cuts = 0, thn_bad_moves = 0;
    std::size_t dfm_posed = 0, dfm_pose_off = 0;  // bones the joints reach, and those that land elsewhere than stored
    float dfm_pose_worst = 0;
    double anm_seconds = 0;
    std::map<std::string, int> msn_unknown;  // conditions the runner doesn't evaluate yet
    for (auto& path : vfs.list()) {
        auto data = vfs.read(path);
        std::string kind;
        try {
            if (path.ends_with(".anm")) {
                kind = "anm";
                auto file = UtfFile::parse(std::move(data));
                AnmLibrary lib;
                lib.add(file->root());
                anm_scripts += lib.scripts().size();
                for (auto& sc : lib.scripts()) anm_seconds += sc.length();
            } else if (path.ends_with(".wav")) {
                // Sounds: every file should decode to samples (numbers only).
                kind = "wav";
                Sound snd = decode_wav(data);
                if (snd.samples.empty()) throw FormatError("no samples");
                wav_seconds += snd.seconds();
            } else if (path.ends_with(".dfm")) {
                // Character meshes: every level and its strips must hold together.
                kind = "dfm";
                auto file = UtfFile::parse(std::move(data));
                auto d = DfmModel::load(file->root());
                for (auto& lvl : d.levels) {
                    for (auto& g : lvl.groups) dfm_tris += lvl.triangles(g).size() / 3;
                }
                dfm_bones += d.bones.size();
                for (auto& b : d.bones) dfm_bad_bones += b.name.empty();
                // The joints' bind pose must put each bone where its "Bone to root" says.
                auto pose = game::pose_bones(d);
                for (std::size_t i = 0; i < d.bones.size(); ++i) {
                    auto ref = game::Xform::from_bone(d.bones[i].to_root);
                    float err = 0;
                    for (int k = 0; k < 9; ++k) err = std::max(err, std::abs(pose[i].r[k] - ref.r[k]));
                    err = std::max({err, std::abs(pose[i].t.x - ref.t.x), std::abs(pose[i].t.y - ref.t.y),
                                    std::abs(pose[i].t.z - ref.t.z)});
                    ++dfm_posed;
                    if (err > 1e-3f) ++dfm_pose_off;
                    dfm_pose_worst = std::max(dfm_pose_worst, err);
                }
            } else if (path.ends_with(".thn")) {
                kind = "thn";
                // Scenes: play each one through; cameras it cuts to should be its own.
                auto scene = ThnScript::parse(data);
                game::ThnPlayer player(scene);
                for (float f : {0.f, 0.5f, 1.f}) player.seek(player.duration() * f);
                thn_moves += scene.moves.size();
                thn_cuts += scene.cuts.size();
                for (auto& c : scene.cuts) {
                    thn_bad_cuts += !scene.entity(c.camera);
                    player.seek(c.time + 1e-3f);
                    game::ThnPlayer::View v;
                    thn_views += player.view(v) && std::isfinite(v.fov_y) && v.fov_y > 0;
                }
                for (auto& m : scene.moves) thn_bad_moves += !scene.entity(m.target);
                for (auto& e : scene.entities)
                    if (e.type == "DEFORMABLE") scene_costumes.push_back(e.template_name);
                    else if (e.type == "SOUND") scene_sounds.push_back(e.template_name);
                for (auto& so : scene.sounds) {
                    ++thn_sound_events;
                    auto* e = scene.entity(so.target);
                    thn_bad_sound_events += !e || e->type != "SOUND";
                }
                for (auto& m : scene.motions) scene_anims.push_back(m.animation);
                for (auto& [n, pth] : scene.paths) {
                    ++thn_paths;
                    thn_short_paths += pth.points.size() < 2;
                }
                for (auto& a : scene.path_anims) {
                    ++thn_rides;
                    thn_bad_rides += !scene.paths.count(to_lower(a.path));
                }
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
    // Costumes: every part they name should exist, with its mesh in the data.
    if (vfs.exists("characters/bodyparts.ini") && vfs.exists("characters/costumes.ini")) {
        CharacterParts cp;
        cp.add_body_parts(parse_ini(vfs.read("characters/bodyparts.ini")));
        IniFile costumes = parse_ini(vfs.read("characters/costumes.ini"));
        cp.add_costumes(costumes);
        std::size_t missing_part = 0, missing_mesh = 0, refs = 0;
        for (auto& sec : costumes) {
            for (auto& e : sec.entries) {
                if (iequals(e.key, "nickname") || e.values.empty()) continue;
                ++refs;
                auto* p = cp.part(e.values[0].as_string());
                if (!p) { ++missing_part; continue; }
                std::string mesh = to_lower(p->mesh);
                std::replace(mesh.begin(), mesh.end(), '\\', '/');
                if (!vfs.exists(mesh)) ++missing_mesh;
            }
        }
        std::printf("costumes: %zu, parts %zu, %zu part references, %zu name a missing part, %zu a missing mesh\n",
                    cp.costume_count(), cp.part_count(), refs, missing_part, missing_mesh);
        // Base characters: the parts each one wears should exist too.
        if (vfs.exists("missions/mbases.ini")) {
            auto bases = parse_mbases(parse_ini(vfs.read("missions/mbases.ini")));
            std::size_t npcs = 0, rooms = 0, fixtures = 0, npc_refs = 0, npc_missing = 0, fixture_unknown = 0;
            for (auto& b : bases) {
                rooms += b.rooms.size();
                for (auto& r : b.rooms)
                    for (auto& f : r.fixtures) {
                        ++fixtures;
                        if (!b.npc(f.npc)) ++fixture_unknown;
                    }
                for (auto& n : b.npcs) {
                    ++npcs;
                    std::vector<const std::string*> wear{&n.body, &n.head, &n.left_hand, &n.right_hand};
                    for (auto& a : n.accessories) wear.push_back(&a);
                    for (auto* w : wear) {
                        if (w->empty()) continue;
                        ++npc_refs;
                        if (!cp.part(*w)) ++npc_missing;
                    }
                }
            }
            std::printf("base characters: %zu bases, %zu rooms, %zu fixed (%zu unknown), %zu characters wearing %zu parts, %zu missing\n",
                        bases.size(), rooms, fixtures, fixture_unknown, npcs, npc_refs, npc_missing);
            // Fixed characters standing in their idle animation: how far the feet sit below
            // the root, against the animation's root height (numbers only).
            AnmLibrary anims;
            for (auto& f : cp.animation_files()) {
                std::string path = to_lower(f);
                std::replace(path.begin(), path.end(), '\\', '/');
                if (vfs.exists(path)) anims.add(UtfFile::parse(vfs.read(path))->root());
            }
            std::size_t posed = 0, no_idle = 0;
            double feet_sum = 0, root_sum = 0, gap_worst = 0;
            for (auto& b : bases)
                for (auto& r : b.rooms)
                    for (auto& fx : r.fixtures) {
                        const BaseNpc* n = b.npc(fx.npc);
                        const BodyPart* body = n ? cp.part(n->body) : nullptr;
                        if (!body || fx.fidget.empty() || !vfs.exists(fx.fidget)) continue;
                        std::string mesh = to_lower(body->mesh);
                        std::replace(mesh.begin(), mesh.end(), '\\', '/');
                        if (!vfs.exists(mesh)) continue;
                        auto model = DfmModel::load(UtfFile::parse(vfs.read(mesh))->root());
                        auto script = ThnScript::parse(vfs.read(fx.fidget));
                        const AnmScript* a = nullptr;
                        for (auto& m : script.motions)
                            if ((a = anims.script(m.animation))) break;
                        if (!a) { ++no_idle; continue; }
                        auto pts = game::skin_points(model, model.levels.front(), game::pose_bones(model, a, 0));
                        float lo = 1e9f;
                        for (auto& q : pts) lo = std::min(lo, q.y);
                        ++posed;
                        feet_sum += lo;
                        root_sum += a->root_height;
                        gap_worst = std::max(gap_worst, std::abs(double(lo) + a->root_height));
                    }
            // Scene characters: their costume should give a body, their motions an animation.
            std::size_t dressed = 0, played = 0;
            for (auto& c : scene_costumes)
                if (auto* cos = cp.costume(c)) dressed += cp.part(cos->body) != nullptr;
            for (auto& a : scene_anims) played += anims.script(a) != nullptr;
            std::printf("scene characters: %zu of %zu dressed, %zu of %zu motions find their animation\n", dressed,
                        scene_costumes.size(), played, scene_anims.size());
            if (posed)
                std::printf("fixed characters idle: %zu posed, %zu without idle; feet %.2f below root on average, root height %.2f, worst gap %.2f\n",
                            posed, no_idle, -feet_sum / posed, root_sum / posed, gap_worst);
        }
    }
    // Music: each system's tracks should name a sound whose file is in the data (counts only).
    if (vfs.exists("universe/universe.ini") && vfs.exists("audio/music.ini")) {
        SoundDb sounds;
        sounds.add(parse_ini(vfs.read("audio/music.ini")));
        Universe uni(vfs);
        std::size_t systems = 0, with_music = 0, refs = 0, unknown = 0, missing_file = 0;
        for (auto& name : uni.system_names()) {
            std::string file = uni.system_file(name);
            if (file.empty() || !vfs.exists(file)) continue;
            ++systems;
            SystemMusic m = parse_system_music(parse_ini(vfs.read(file)));
            if (m.space.empty() && m.danger.empty() && m.battle.empty()) continue;
            ++with_music;
            for (auto* nick : {&m.space, &m.danger, &m.battle}) {
                if (nick->empty()) continue;
                ++refs;
                const SoundDef* d = sounds.find(*nick);
                if (!d) { ++unknown; continue; }
                std::string path = to_lower(d->file);
                std::replace(path.begin(), path.end(), '\\', '/');
                if (!vfs.exists(path)) ++missing_file;
            }
        }
        std::printf("music: %zu sounds, %zu of %zu systems have music, %zu tracks named (%zu unknown, %zu without file)\n",
                    sounds.size(), with_music, systems, refs, unknown, missing_file);
    }
    // Base rooms: [Room_Sound] music and ambience should name sounds with a file (counts only).
    if (vfs.exists("universe/universe.ini") && vfs.exists("audio/music.ini")) {
        SoundDb sounds;
        for (const char* f : {"audio/music.ini", "audio/ambience_sounds.ini", "audio/gf_sounds.ini", "audio/sounds.ini",
                              "audio/story_sounds.ini", "audio/engine_sounds.ini", "audio/interface_sounds.ini"})
            if (vfs.exists(f)) sounds.add(parse_ini(vfs.read(f)));
        std::size_t rooms = 0, with_music = 0, with_ambient = 0, unresolved = 0;
        for (auto& sec : parse_ini(vfs.read("universe/universe.ini"))) {
            if (!iequals(sec.name, "Base")) continue;
            auto* nick = sec.find("nickname");
            if (!nick || !nick->at(0)) continue;
            std::string file = base_file_of(vfs, nick->at(0)->as_string());
            if (file.empty()) continue;
            std::optional<BaseRooms> b;
            try { b = BaseRooms::load(vfs, file); } catch (const std::exception&) { continue; }
            if (!b) continue;
            for (auto& r : b->rooms) {
                ++rooms;
                for (auto* n : {&r.music, &r.ambient}) {
                    if (n->empty()) continue;
                    ++(n == &r.music ? with_music : with_ambient);
                    const SoundDef* d = sounds.find(*n);
                    std::string path = d ? to_lower(d->file) : std::string();
                    std::replace(path.begin(), path.end(), '\\', '/');
                    if (!d || !vfs.exists(path)) {
                        if (unresolved++ < 3) std::printf("  room sound %s: %s\n", n->c_str(), d ? "no file" : "unknown");
                    }
                }
            }
        }
        std::size_t scene_found = 0;
        for (auto& n : scene_sounds) scene_found += sounds.find(n) != nullptr;
        std::printf("scene sound: %zu of %zu sound entities in the sound lists, %zu sound events (%zu on no sound entity)\n",
                    scene_found, scene_sounds.size(), thn_sound_events, thn_bad_sound_events);
        // Equipment sounds: gun shots, engine hum and cruise, shield collapse and rebuild.
        std::size_t equip_refs = 0, equip_bad = 0;
        for (const char* f : {"equipment/weapon_equip.ini", "equipment/engine_equip.ini", "equipment/st_equip.ini",
                              "equipment/misc_equip.ini", "equipment/select_equip.ini"}) {
            if (!vfs.exists(f)) continue;
            for (auto& sec : parse_ini(vfs.read(f)))
                for (auto& e : sec.entries) {
                    if (!(iequals(e.key, "one_shot_sound") || iequals(e.key, "rumble_sound") || iequals(e.key, "cruise_start_sound") ||
                          iequals(e.key, "cruise_loop_sound") || iequals(e.key, "cruise_stop_sound") ||
                          iequals(e.key, "shield_collapse_sound") || iequals(e.key, "shield_rebuilt_sound")) || !e.at(0))
                        continue;
                    ++equip_refs;
                    const SoundDef* d = sounds.find(e.at(0)->as_string());
                    std::string path = d ? to_lower(d->file) : std::string();
                    std::replace(path.begin(), path.end(), '\\', '/');
                    equip_bad += !d || !vfs.exists(path);
                }
        }
        std::printf("equipment sound: %zu sounds named (%zu unresolved)\n", equip_refs, equip_bad);
        std::printf("room sound: %zu rooms, %zu with music, %zu with ambience (%zu unresolved)\n", rooms, with_music, with_ambient,
                    unresolved);
    }
    // Random traffic: every populated zone's encounters should find ships for each slot.
    {
        auto read = [&](const char* p) { return vfs.exists(p) ? parse_ini(vfs.read(p)) : IniFile{}; };
        auto classes = parse_ship_classes(read("missions/shipclasses.ini"));
        auto factions = parse_faction_ships(read("missions/faction_prop.ini"));
        auto archs = parse_npc_ship_archs(read("missions/npcships.ini"));
        std::map<std::string, EncounterDef> encs;
        std::size_t zones = 0, populated = 0, slots = 0, slots_found = 0, missing_enc = 0;
        std::map<std::string, int> misses;
        for (auto& path : vfs.list()) {
            if (!path.starts_with("universe/systems/") || !path.ends_with(".ini") ||
                std::count(path.begin(), path.end(), '/') != 3)
                continue;
            for (auto& z : parse_pop_zones(parse_ini(vfs.read(path)))) {
                ++zones;
                if (!z.populated()) continue;
                ++populated;
                for (auto& e : z.encounters) {
                    std::string file = "missions/encounters/" + to_lower(e.name) + ".ini";
                    if (!encs.count(file)) {
                        if (!vfs.exists(file)) {
                            ++missing_enc;
                            continue;
                        }
                        encs[file] = parse_encounter(parse_ini(vfs.read(file)));
                    }
                    for (auto& f : encs[file].formations)
                        for (auto& slot : f.slots)
                            for (auto& [fac, w] : e.factions) {
                                ++slots;
                                int lvl = std::max(1, e.difficulty + slot.level_offset);
                                if (!ships_for(archs, factions, classes, fac, slot.ship_class, lvl).empty()) ++slots_found;
                                else ++misses[fac + " " + slot.ship_class + " d" + std::to_string(lvl)];
                            }
                }
            }
        }
        std::printf("population: %zu zones, %zu populated, %zu encounter files (%zu missing), slots with ships %zu of %zu\n",
                    zones, populated, encs.size(), missing_enc, slots_found, slots);
        int shown_miss = 0;
        for (auto& [k, n] : misses)
            if (shown_miss++ < 12) std::printf("  no ship for %s (%d)\n", k.c_str(), n);
    }
    int total_fail = 0;
    for (auto& [k, n] : ok) std::printf("%-14s ok %6d   failed %d\n", k.c_str(), n, fail[k]);
    for (auto& [k, n] : fail) total_fail += n;
    std::printf("models %zu, parts %zu, meshes %zu, triangles (all LODs) %zu, textures decoded %zu\n", models, parts,
                meshes, triangles, textures);
    std::printf("character meshes: %zu triangles, %zu bones, %zu bones without a part\n", dfm_tris, dfm_bones, dfm_bad_bones);
    std::printf("scenes: %zu moves (%zu name no entity), %zu camera cuts (%zu to no camera, %zu give a view)\n", thn_moves,
                thn_bad_moves, thn_cuts, thn_bad_cuts, thn_views);
    std::printf("sounds: %.0f minutes in all\n", wav_seconds / 60);
    std::printf("scene paths: %zu (%zu with fewer than 2 points), %zu path animations (%zu on no path)\n", thn_paths,
                thn_short_paths, thn_rides, thn_bad_rides);
    std::printf("character bind pose: %zu of %zu bones off (worst %.4f)\n", dfm_pose_off, dfm_posed, dfm_pose_worst);
    std::printf("animations: %zu scripts, %.0f seconds\n", anm_scripts, anm_seconds);
    std::printf("mission triggers %zu, actions naming a missing trigger %zu\n", msn_triggers, msn_dangling);
    for (auto& [n, c] : msn_unknown) std::printf("  condition not evaluated yet: %s (%d)\n", n.c_str(), c);
    return total_fail ? 1 : 0;
}
