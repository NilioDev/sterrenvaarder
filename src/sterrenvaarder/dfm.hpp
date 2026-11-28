// SPDX-License-Identifier: MPL-2.0
#pragma once
// Deformable models (.dfm): the skinned meshes of character bodies, heads and hands.
// A file holds several detail levels of one mesh (MultiLevel/MeshN), its bones (one
// "<name>.3db" node each, with the bone's bind transform) and the parts that name them.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "sterrenvaarder/math.hpp"
#include "sterrenvaarder/model.hpp"
#include "sterrenvaarder/utf.hpp"

namespace sterrenvaarder {

struct DfmMesh {
    std::vector<Vec3> points, normals;               // per point
    std::vector<std::array<float, 2>> uvs;           // UV0 table
    std::vector<std::uint32_t> point_index, uv_index;  // per corner: which point, which uv
    // Skinning: for each point, `bone_count` entries from `bone_first` in the chains.
    std::vector<std::uint32_t> bone_first, bone_count, bone_ids;
    std::vector<float> bone_weights;
    struct Group {
        std::string material;
        std::vector<std::uint16_t> strip;  // triangle strip over corners, joined by repeats
    };
    std::vector<Group> groups;
    // The strip of one group as a triangle list of corners, without degenerate triangles.
    std::vector<std::uint32_t> triangles(const Group& g) const;
};

struct DfmBone {
    std::string name;        // part name without "Part_" (e.g. "L Palm"); empty for bones without a part
    std::string node;        // its .3db node
    int index = 0;           // its position among the .3db nodes: the id the skin chains use
    std::array<float, 12> to_root{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};  // "Bone to root" as stored (it maps root space into the bone)
};

struct DfmModel {
    std::vector<float> fractions;  // detail of each level, 1 = full
    std::vector<DfmMesh> levels;   // level 0 has the most detail
    std::vector<DfmBone> bones;
    std::vector<Joint> joints;  // Cmpnd/Cons: how the bones hang together (by part name)
    std::string skeleton;
    float scale = 1;
    static bool is_dfm(const UtfNode& root);
    static DfmModel load(const UtfNode& root);  // throws FormatError
};

}  // namespace sterrenvaarder
