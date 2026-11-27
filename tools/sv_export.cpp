// SPDX-License-Identifier: MPL-2.0
// sv-export: export a model from the user's game data to glTF binary (.glb).
//   sv-export --data <DATA> [--mod <dir>]... --ship <nickname> <out.glb> [--lod N]
//   sv-export --data <DATA> [--mod <dir>]... --model <game path> [--mat <game path>]... <out.glb>
// Output is for personal inspection/modding; it contains the game's own art.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "sterrenvaarder/hash.hpp"
#include "sterrenvaarder/ini.hpp"
#include "sterrenvaarder/model.hpp"
#include "sterrenvaarder/png.hpp"
#include "sterrenvaarder/vfs.hpp"

using namespace sterrenvaarder;

namespace {

struct Glb {
    std::vector<std::uint8_t> bin;
    std::ostringstream accessors, views, meshes, nodes, materials, images, textures;
    int n_acc = 0, n_view = 0, n_mesh = 0, n_node = 0, n_mat = 0, n_img = 0;

    static void sep(std::ostringstream& s) { if (s.tellp() > 0) s << ","; }

    int view(const void* p, std::size_t n, int target) {
        while (bin.size() % 4) bin.push_back(0);
        std::size_t off = bin.size();
        bin.insert(bin.end(), static_cast<const std::uint8_t*>(p), static_cast<const std::uint8_t*>(p) + n);
        sep(views);
        views << "{\"buffer\":0,\"byteOffset\":" << off << ",\"byteLength\":" << n;
        if (target) views << ",\"target\":" << target;
        views << "}";
        return n_view++;
    }
    int accessor(int v, int comp, std::size_t count, const char* type, const std::string& extra = {}) {
        sep(accessors);
        accessors << "{\"bufferView\":" << v << ",\"componentType\":" << comp << ",\"count\":" << count
                  << ",\"type\":\"" << type << "\"" << extra << "}";
        return n_acc++;
    }
};

std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\') r.push_back('\\');
        if (static_cast<unsigned char>(c) >= 0x20) r.push_back(c);
    }
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Vfs vfs;
        std::string ship, model_path, out;
        std::vector<std::string> mats;
        int lod = 0;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
                return argv[++i];
            };
            if (a == "--data") vfs.mount(next(), "base");
            else if (a == "--mod") { auto d = next(); vfs.mount(d, d); }
            else if (a == "--ship") ship = next();
            else if (a == "--model") model_path = next();
            else if (a == "--mat") mats.push_back(next());
            else if (a == "--lod") lod = std::stoi(next());
            else out = a;
        }
        if (out.empty() || (ship.empty() && model_path.empty())) {
            std::fprintf(stderr, "usage: sv-export --data <DATA> (--ship <nickname> | --model <path>) <out.glb>\n");
            return 2;
        }
        if (!ship.empty()) {  // look the ship up in shiparch.ini, like the game does
            auto ini = parse_ini(vfs.read("ships/shiparch.ini"));
            for (auto& sec : ini) {
                auto nick = sec.find("nickname");
                if (!iequals(sec.name, "Ship") || !nick || !iequals(nick->values.at(0).as_string(), ship)) continue;
                model_path = sec.find("DA_archetype")->values.at(0).as_string();
                for (auto* m : sec.find_all("material_library")) mats.push_back(m->values.at(0).as_string());
            }
            if (model_path.empty()) throw std::runtime_error("ship not found in shiparch.ini: " + ship);
        }

        auto file = UtfFile::parse(vfs.read(model_path));
        Model model = Model::load(file);
        MeshLibrary meshes;
        MaterialLibrary materials;
        meshes.add_from(file);
        for (auto& m : mats) materials.add_from(UtfFile::parse(vfs.read(m)));
        materials.add_from(file);  // a model's own library wins

        Glb g;
        std::map<std::uint32_t, int> mat_index;
        std::map<std::string, int> tex_index;
        auto texture_for = [&](const std::string& name) -> int {
            if (name.empty()) return -1;
            auto key = to_lower(name);
            if (auto it = tex_index.find(key); it != tex_index.end()) return it->second;
            int idx = -1;
            if (auto node = materials.texture(name)) {
                auto png = encode_png(decode_texture_node(*node));
                int v = g.view(png.data(), png.size(), 0);
                Glb::sep(g.images);
                g.images << "{\"bufferView\":" << v << ",\"mimeType\":\"image/png\",\"name\":\"" << esc(name) << "\"}";
                Glb::sep(g.textures);
                g.textures << "{\"source\":" << g.n_img << "}";
                idx = g.n_img++;
            }
            tex_index[key] = idx;
            return idx;
        };
        auto material_for = [&](std::uint32_t crc) -> int {
            if (auto it = mat_index.find(crc); it != mat_index.end()) return it->second;
            const Material* m = materials.material(crc);
            Glb::sep(g.materials);
            if (!m) {
                g.materials << "{\"name\":\"missing_" << std::hex << crc << std::dec << "\"}";
            } else {
                int t = texture_for(m->diffuse_texture);
                int et = texture_for(m->emissive_texture);
                g.materials << "{\"name\":\"" << esc(m->name) << "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":["
                            << m->diffuse.x << "," << m->diffuse.y << "," << m->diffuse.z << "," << m->opacity
                            << "],\"metallicFactor\":0.2,\"roughnessFactor\":0.6";
                if (t >= 0) g.materials << ",\"baseColorTexture\":{\"index\":" << t << "}";
                g.materials << "}";
                if (et >= 0) g.materials << ",\"emissiveTexture\":{\"index\":" << et << "},\"emissiveFactor\":[1,1,1]";
                else if (m->emissive.x + m->emissive.y + m->emissive.z > 0)
                    g.materials << ",\"emissiveFactor\":[" << m->emissive.x << "," << m->emissive.y << "," << m->emissive.z << "]";
                if (m->alpha_blend) g.materials << ",\"alphaMode\":\"BLEND\"";
                if (m->two_sided) g.materials << ",\"doubleSided\":true";
                g.materials << "}";
            }
            mat_index[crc] = g.n_mat;
            return g.n_mat++;
        };

        std::map<std::string, int> node_of;
        std::vector<std::vector<int>> children(model.parts.size());
        std::size_t tris = 0;
        for (std::size_t pi = 0; pi < model.parts.size(); ++pi) {
            const ModelPart& part = model.parts[pi];
            int mesh_idx = -1;
            if (!part.lods.empty()) {
                const Lod& l = part.lods[std::min<std::size_t>(lod, part.lods.size() - 1)];
                const VMeshData* vm = meshes.mesh(l.ref.mesh_id);
                if (!vm) throw std::runtime_error("mesh not found for part " + part.name);
                std::ostringstream prims;
                std::size_t idx_cursor = l.ref.start_index;
                for (std::size_t gi = l.ref.start_group; gi < std::size_t(l.ref.start_group) + l.ref.group_count; ++gi) {
                    const VMeshGroup& grp = vm->groups.at(gi);
                    std::size_t base = std::size_t(l.ref.start_vertex) + grp.start_vertex;
                    std::size_t nverts = std::size_t(grp.end_vertex) - grp.start_vertex + 1;
                    if (base + nverts > vm->positions.size()) throw std::runtime_error("vertex range out of bounds");
                    std::vector<std::uint16_t> idx(vm->indices.begin() + idx_cursor, vm->indices.begin() + idx_cursor + grp.index_count);
                    idx_cursor += grp.index_count;
                    tris += idx.size() / 3;
                    Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
                    for (std::size_t v = base; v < base + nverts; ++v) {
                        auto& p = vm->positions[v];
                        mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
                        mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
                    }
                    std::ostringstream ext;
                    ext << ",\"min\":[" << mn.x << "," << mn.y << "," << mn.z << "],\"max\":[" << mx.x << "," << mx.y << "," << mx.z << "]";
                    int pos = g.accessor(g.view(&vm->positions[base], nverts * 12, 34962), 5126, nverts, "VEC3", ext.str());
                    std::ostringstream attrs;
                    attrs << "\"POSITION\":" << pos;
                    if (!vm->normals.empty())
                        attrs << ",\"NORMAL\":" << g.accessor(g.view(&vm->normals[base], nverts * 12, 34962), 5126, nverts, "VEC3");
                    if (!vm->uv0.empty())
                        attrs << ",\"TEXCOORD_0\":" << g.accessor(g.view(&vm->uv0[base], nverts * 8, 34962), 5126, nverts, "VEC2");
                    int ia = g.accessor(g.view(idx.data(), idx.size() * 2, 34963), 5123, idx.size(), "SCALAR");
                    if (prims.tellp() > 0) prims << ",";
                    prims << "{\"attributes\":{" << attrs.str() << "},\"indices\":" << ia << ",\"material\":" << material_for(grp.material_crc) << "}";
                }
                Glb::sep(g.meshes);
                g.meshes << "{\"name\":\"" << esc(part.name) << "\",\"primitives\":[" << prims.str() << "]}";
                mesh_idx = g.n_mesh++;
            }
            node_of[to_lower(part.name)] = static_cast<int>(pi);
            (void)mesh_idx;
            children[pi].clear();
        }
        // Nodes: one per part, transformed by the joint that attaches it to its parent.
        std::vector<const Joint*> joint_of(model.parts.size(), nullptr);
        for (auto& j : model.joints) {
            auto c = node_of.find(to_lower(j.child)), p = node_of.find(to_lower(j.parent));
            if (c == node_of.end() || p == node_of.end()) continue;
            joint_of[c->second] = &j;
            children[p->second].push_back(c->second);
        }
        int mesh_counter = 0;
        for (std::size_t pi = 0; pi < model.parts.size(); ++pi) {
            Glb::sep(g.nodes);
            g.nodes << "{\"name\":\"" << esc(model.parts[pi].name) << "\"";
            if (!model.parts[pi].lods.empty()) g.nodes << ",\"mesh\":" << mesh_counter++;
            if (auto* j = joint_of[pi]) {
                const auto& r = j->rotation;  // row-major; glTF matrices are column-major
                g.nodes << ",\"matrix\":[" << r[0] << "," << r[1] << "," << r[2] << ",0," << r[3] << "," << r[4] << "," << r[5]
                        << ",0," << r[6] << "," << r[7] << "," << r[8] << ",0," << j->origin.x << "," << j->origin.y << ","
                        << j->origin.z << ",1]";
            }
            if (!children[pi].empty()) {
                g.nodes << ",\"children\":[";
                for (std::size_t k = 0; k < children[pi].size(); ++k) g.nodes << (k ? "," : "") << children[pi][k];
                g.nodes << "]";
            }
            g.nodes << "}";
        }
        while (g.bin.size() % 4) g.bin.push_back(0);
        std::ostringstream js;
        js << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"sterrenvaarder sv-export\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
           << "\"nodes\":[" << g.nodes.str() << "],\"meshes\":[" << g.meshes.str() << "],\"materials\":[" << g.materials.str()
           << "],\"accessors\":[" << g.accessors.str() << "],\"bufferViews\":[" << g.views.str() << "],\"buffers\":[{\"byteLength\":"
           << g.bin.size() << "}]";
        if (g.n_img) js << ",\"images\":[" << g.images.str() << "],\"textures\":[" << g.textures.str() << "]";
        js << "}";
        std::string json = js.str();
        while (json.size() % 4) json.push_back(' ');

        std::ofstream f(out, std::ios::binary);
        auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        u32(0x46546C67);
        u32(2);
        u32(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + g.bin.size()));
        u32(static_cast<std::uint32_t>(json.size()));
        u32(0x4E4F534A);
        f.write(json.data(), static_cast<std::streamsize>(json.size()));
        u32(static_cast<std::uint32_t>(g.bin.size()));
        u32(0x004E4942);
        f.write(reinterpret_cast<const char*>(g.bin.data()), static_cast<std::streamsize>(g.bin.size()));
        std::printf("%s: %zu parts, %zu triangles, %d materials, %d textures -> %s\n", model_path.c_str(),
                    model.parts.size(), tris, g.n_mat, g.n_img, out.c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
