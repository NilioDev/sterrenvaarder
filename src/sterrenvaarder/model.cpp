// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/model.hpp"

#include "sterrenvaarder/hash.hpp"

namespace sterrenvaarder {

namespace {

Vec3 read_vec3(Bytes d, std::size_t o) { return {read_le<float>(d, o), read_le<float>(d, o + 4), read_le<float>(d, o + 8)}; }

}  // namespace

void MaterialLibrary::add_from(std::shared_ptr<UtfFile> file) {
    const UtfNode& root = file->root();
    if (auto ml = root.child("Material library")) {
        for (auto& m : ml->children) {
            if (m.leaf) continue;  // "Material count"
            Material mat;
            mat.name = m.name;
            if (auto n = m.child("Type")) mat.type = n->as_string();
            if (auto n = m.child("Dc"); n && n->data.size() >= 12) mat.diffuse = read_vec3(n->data, 0);
            if (auto n = m.child("Ec"); n && n->data.size() >= 12) mat.emissive = read_vec3(n->data, 0);
            if (auto n = m.child("Oc"); n && n->data.size() >= 4) mat.opacity = n->as<float>();
            if (auto n = m.child("Dt_name")) mat.diffuse_texture = n->as_string();
            if (auto n = m.child("Et_name")) mat.emissive_texture = n->as_string();
            if (auto n = m.child("Dm_name")) mat.detail_texture = n->as_string();
            if (auto n = m.child("Dm0_name")) mat.detail_texture = n->as_string();
            if (auto n = m.child("TileRate"); n && n->data.size() >= 4) mat.detail_tile = n->as<float>();
            if (auto n = m.child("TileRate0"); n && n->data.size() >= 4) mat.detail_tile = n->as<float>();
            if (auto n = m.child("Ac"); n && n->data.size() >= 12) mat.ambient = read_vec3(n->data, 0);
            if (auto n = m.child("Alpha"); n && n->data.size() >= 4) mat.alpha = n->as<float>();
            if (auto n = m.child("Scale"); n && n->data.size() >= 4) mat.scale = n->as<float>();
            mat.two_sided = mat.type.find("Two") != std::string::npos;
            mat.alpha_blend = mat.type.find("Ot") != std::string::npos || mat.opacity < 1.0f;
            materials_[fl_model_crc(mat.name)] = std::move(mat);
        }
    }
    if (auto tl = root.child("Texture library"))
        for (auto& t : tl->children)
            if (!t.leaf) textures_[to_lower(t.name)] = &t;
    files_.push_back(std::move(file));
}

const Material* MaterialLibrary::material_by_name(std::string_view name) const { return material(fl_model_crc(name)); }

const Material* MaterialLibrary::material(std::uint32_t crc) const {
    auto it = materials_.find(crc);
    return it == materials_.end() ? nullptr : &it->second;
}

const UtfNode* MaterialLibrary::texture(std::string_view name) const {
    auto it = textures_.find(to_lower(name));
    return it == textures_.end() ? nullptr : it->second;
}

void MeshLibrary::add_from(std::shared_ptr<UtfFile> file) {
    if (auto lib = file->root().child("VMeshLibrary"))
        for (auto& e : lib->children)
            if (auto d = e.child("VMeshData")) meshes_[fl_model_crc(e.name)] = VMeshData::parse(d->data);
    files_.push_back(std::move(file));
}

const VMeshData* MeshLibrary::mesh(std::uint32_t id) const {
    auto it = meshes_.find(id);
    return it == meshes_.end() ? nullptr : &it->second;
}

const ModelPart* Model::part(std::string_view name) const {
    for (auto& p : parts)
        if (iequals(p.name, name)) return &p;
    return nullptr;
}

namespace {

// A part node holds either MultiLevel/Level<N>/VMeshPart/VMeshRef (+ Switch2 ranges)
// or VMeshPart/VMeshRef directly.
ModelPart load_part(const UtfNode& node, std::string name) {
    ModelPart part;
    part.name = std::move(name);
    if (auto ml = node.child("MultiLevel")) {
        std::vector<float> ranges;
        if (auto sw = ml->child("Switch2"))
            for (std::size_t o = 0; o + 4 <= sw->data.size(); o += 4) ranges.push_back(read_le<float>(sw->data, o));
        for (int lvl = 0;; ++lvl) {
            auto l = ml->child("Level" + std::to_string(lvl));
            if (!l) break;
            auto r = l->find("VMeshPart/VMeshRef");
            if (!r) continue;
            Lod lod;
            lod.ref = VMeshRef::parse(r->data);
            if (std::size_t(lvl + 1) < ranges.size()) lod.range_end = ranges[lvl + 1];
            part.lods.push_back(lod);
        }
    } else if (auto r = node.find("VMeshPart/VMeshRef")) {
        part.lods.push_back(Lod{VMeshRef::parse(r->data), 0});
    }
    if (auto hp = node.child("Hardpoints"))
        for (auto& group : hp->children)
            for (auto& h : group.children) part.hardpoints.push_back(h.name);
    return part;
}

void load_joints(const UtfNode& cons, std::vector<Joint>& out) {
    struct Kind { const char* name; JointType type; std::size_t size; };
    const Kind kinds[] = {{"Fix", JointType::Fixed, 176}, {"Rev", JointType::Revolute, 208},
                          {"Pris", JointType::Prismatic, 208}, {"Sphere", JointType::Sphere, 212},
                          {"Loose", JointType::Loose, 176}};
    for (auto& k : kinds) {
        auto n = cons.child(k.name);
        if (!n) continue;
        Bytes d = n->data;
        if (d.size() % k.size) throw FormatError(std::string("bad joint record size in ") + k.name);
        for (std::size_t o = 0; o < d.size(); o += k.size) {
            Joint j;
            j.type = k.type;
            j.parent = std::string(read_cstr(d.subspan(o, 64), 0));
            j.child = std::string(read_cstr(d.subspan(o + 64, 64), 0));
            j.origin = read_vec3(d, o + 128);
            std::size_t rot = o + 140;
            if (k.type == JointType::Revolute || k.type == JointType::Prismatic || k.type == JointType::Sphere)
                rot += 12;  // offset vector precedes the rotation
            for (int i = 0; i < 9; ++i) j.rotation[i] = read_le<float>(d, rot + 4 * i);
            if (k.type == JointType::Revolute || k.type == JointType::Prismatic) {
                j.axis = read_vec3(d, o + 188);
                j.min = read_le<float>(d, o + 200);
                j.max = read_le<float>(d, o + 204);
            }
            out.push_back(std::move(j));
        }
    }
}

}  // namespace

Model Model::load(std::shared_ptr<UtfFile> file) {
    Model m;
    m.file = file;
    const UtfNode& root = file->root();
    if (auto cmpnd = root.child("Cmpnd")) {
        const UtfNode* root_entry = nullptr;
        std::vector<const UtfNode*> entries;
        for (auto& c : cmpnd->children) {
            if (c.leaf || iequals(c.name, "Cons")) continue;
            (iequals(c.name, "Root") ? root_entry : entries.emplace_back()) = &c;
        }
        if (root_entry) entries.insert(entries.begin(), root_entry);
        for (auto* e : entries) {
            auto obj = e->child("Object name");
            auto file_name = e->child("File name");
            if (!obj || !file_name) continue;
            auto part_node = root.child(file_name->as_string());
            if (!part_node) throw FormatError("compound part missing: " + file_name->as_string());
            m.parts.push_back(load_part(*part_node, obj->as_string()));
        }
        if (auto cons = cmpnd->child("Cons")) load_joints(*cons, m.joints);
    } else {
        m.parts.push_back(load_part(root, "Root"));
    }
    return m;
}

}  // namespace sterrenvaarder
