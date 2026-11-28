// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/dfm.hpp"

#include <cstdio>

namespace sterrenvaarder {
namespace {

template <class T>
std::vector<T> read_all(const UtfNode* n) {
    std::vector<T> out;
    if (!n) return out;
    out.reserve(n->data.size() / sizeof(T));
    for (std::size_t off = 0; off + sizeof(T) <= n->data.size(); off += sizeof(T)) out.push_back(read_le<T>(n->data, off));
    return out;
}

std::vector<Vec3> read_vec3s(const UtfNode* n) {
    auto f = read_all<float>(n);
    std::vector<Vec3> out;
    for (std::size_t i = 0; i + 2 < f.size(); i += 3) out.push_back({f[i], f[i + 1], f[i + 2]});
    return out;
}

DfmMesh load_mesh(const UtfNode& m) {
    DfmMesh mesh;
    const UtfNode* geo = m.child("Geometry");
    if (!geo) throw FormatError("DFM mesh " + m.name + " has no Geometry");
    mesh.points = read_vec3s(geo->child("Points"));
    mesh.normals = read_vec3s(geo->child("Vertex_normals"));
    auto uv = read_all<float>(geo->child("UV0"));
    for (std::size_t i = 0; i + 1 < uv.size(); i += 2) mesh.uvs.push_back({uv[i], uv[i + 1]});
    mesh.point_index = read_all<std::uint32_t>(geo->child("Point_indices"));
    mesh.uv_index = read_all<std::uint32_t>(geo->child("UV0_indices"));
    mesh.bone_first = read_all<std::uint32_t>(geo->child("Point_bone_first"));
    mesh.bone_count = read_all<std::uint32_t>(geo->child("Point_bone_count"));
    mesh.bone_ids = read_all<std::uint32_t>(geo->child("Bone_id_chain"));
    mesh.bone_weights = read_all<float>(geo->child("Bone_weight_chain"));
    for (std::uint32_t p : mesh.point_index)
        if (p >= mesh.points.size()) throw FormatError("DFM corner names a missing point");
    if (const UtfNode* groups = m.child("Face_groups"))
        for (auto& g : groups->children) {
            if (g.leaf) continue;
            DfmMesh::Group grp;
            if (auto* mat = g.child("Material_name")) grp.material = mat->as_string();
            grp.strip = read_all<std::uint16_t>(g.child("Tristrip_indices"));
            for (auto c : grp.strip)
                if (c >= mesh.point_index.size()) throw FormatError("DFM strip names a missing corner");
            mesh.groups.push_back(std::move(grp));
        }
    return mesh;
}

}  // namespace

std::vector<std::uint32_t> DfmMesh::triangles(const Group& g) const {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 2; i < g.strip.size(); ++i) {
        std::uint32_t a = g.strip[i - 2], b = g.strip[i - 1], c = g.strip[i];
        if (a == b || b == c || a == c) continue;  // the joins between strips
        if (i % 2 == 0) out.insert(out.end(), {a, b, c});
        else out.insert(out.end(), {b, a, c});  // every other triangle of a strip turns the other way
    }
    return out;
}

bool DfmModel::is_dfm(const UtfNode& root) { return root.child("MultiLevel") != nullptr; }

DfmModel DfmModel::load(const UtfNode& root) {
    DfmModel d;
    const UtfNode* ml = root.child("MultiLevel");
    if (!ml) throw FormatError("not a deformable model (no MultiLevel)");
    d.fractions = read_all<float>(ml->child("Fractions"));
    for (int i = 0;; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "Mesh%d", i);
        const UtfNode* m = ml->child(name);
        if (!m) break;
        d.levels.push_back(load_mesh(*m));
    }
    if (d.levels.empty()) throw FormatError("deformable model without meshes");
    if (auto* sk = root.find("Skeleton/Name")) d.skeleton = sk->as_string();
    // Bones are the .3db nodes in file order: the skin chains name them by that position.
    for (auto& n : root.children) {
        if (n.leaf || n.name.size() < 4 || n.name.compare(n.name.size() - 4, 4, ".3db") != 0) continue;
        DfmBone b;
        b.node = n.name;
        b.index = static_cast<int>(d.bones.size());
        if (auto* tr = n.child("Bone to root"); tr && tr->data.size() >= 48)
            for (int k = 0; k < 12; ++k) b.to_root[k] = read_le<float>(tr->data, k * 4);
        d.bones.push_back(std::move(b));
    }
    // Parts give the bones their names ("Part_L Palm" -> "L Palm").
    if (const UtfNode* cmp = root.child("Cmpnd")) {
        if (auto* s = cmp->child("Scale"); s && s->data.size() >= 4) d.scale = s->as<float>();
        if (auto* cons = cmp->child("Cons")) load_joints(*cons, d.joints);
        for (auto& p : cmp->children) {
            const UtfNode* file = p.leaf ? nullptr : p.child("File name");
            if (!file) continue;
            std::string node = file->as_string();
            for (auto& b : d.bones)
                if (iequals(b.node, node)) b.name = p.name.rfind("Part_", 0) == 0 ? p.name.substr(5) : p.name;
        }
    }
    for (auto& lvl : d.levels)
        for (auto id : lvl.bone_ids)
            if (id >= d.bones.size()) throw FormatError("DFM skin names a missing bone");
    return d;
}

}  // namespace sterrenvaarder
