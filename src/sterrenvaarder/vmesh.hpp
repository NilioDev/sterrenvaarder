// SPDX-License-Identifier: MPL-2.0
#pragma once
// VMeshData (shared vertex/index buffers) and VMeshRef (a part's slice of them).
#include <cstdint>
#include <vector>

#include "sterrenvaarder/bytes.hpp"
#include "sterrenvaarder/math.hpp"

namespace sterrenvaarder {

struct VMeshGroup {
    std::uint32_t material_crc = 0;  // fl_model_crc(material name)
    std::uint16_t start_vertex = 0;  // indices in this group are relative to this
    std::uint16_t end_vertex = 0;
    std::uint16_t index_count = 0;
};

struct VMeshData {
    std::uint16_t fvf = 0;  // Direct3D flexible vertex format bits
    std::vector<VMeshGroup> groups;
    std::vector<std::uint16_t> indices;  // triangle list
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;           // empty if absent
    std::vector<std::uint32_t> colors;   // D3DCOLOR (ARGB), empty if absent
    std::vector<Vec2> uv0, uv1;          // empty if absent

    static VMeshData parse(Bytes data);
};

struct VMeshRef {
    std::uint32_t mesh_id = 0;  // fl_model_crc(VMeshLibrary entry name)
    std::uint16_t start_vertex = 0, vertex_count = 0;
    std::uint16_t start_index = 0, index_count = 0;
    std::uint16_t start_group = 0, group_count = 0;
    Vec3 bbox_min, bbox_max, center;
    float radius = 0;

    static VMeshRef parse(Bytes data);
};

}  // namespace sterrenvaarder
