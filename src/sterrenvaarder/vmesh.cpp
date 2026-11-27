// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/vmesh.hpp"

#include <string>

namespace sterrenvaarder {

namespace {
constexpr std::uint16_t kXYZ = 0x002, kNormal = 0x010, kDiffuse = 0x040, kSpecular = 0x080;
}

// Header: u32 mesh type (1), u32 surface type (4), u16 group count, u16 index count,
// u16 FVF, u16 vertex count. Then groups (12 bytes each), u16 indices, vertices.
VMeshData VMeshData::parse(Bytes d) {
    VMeshData m;
    auto group_count = read_le<std::uint16_t>(d, 8);
    auto index_count = read_le<std::uint16_t>(d, 10);
    m.fvf = read_le<std::uint16_t>(d, 12);
    auto vertex_count = read_le<std::uint16_t>(d, 14);

    std::size_t p = 16;
    m.groups.resize(group_count);
    for (auto& g : m.groups) {
        g.material_crc = read_le<std::uint32_t>(d, p);
        g.start_vertex = read_le<std::uint16_t>(d, p + 4);
        g.end_vertex = read_le<std::uint16_t>(d, p + 6);
        g.index_count = read_le<std::uint16_t>(d, p + 8);
        p += 12;  // + u16 padding
    }
    m.indices.resize(index_count);
    for (auto& i : m.indices) {
        i = read_le<std::uint16_t>(d, p);
        p += 2;
    }

    const unsigned tex_sets = (m.fvf >> 8) & 0xF;
    if (!(m.fvf & kXYZ)) throw FormatError("VMeshData without positions (fvf " + std::to_string(m.fvf) + ")");
    std::size_t stride = 12 + ((m.fvf & kNormal) ? 12 : 0) + ((m.fvf & kDiffuse) ? 4 : 0) +
                         ((m.fvf & kSpecular) ? 4 : 0) + tex_sets * 8;
    // Some mod-made meshes carry a few trailing bytes; the original game ignores them.
    if (p + stride * vertex_count > d.size())
        throw FormatError("VMeshData too small: fvf " + std::to_string(m.fvf));

    m.positions.resize(vertex_count);
    if (m.fvf & kNormal) m.normals.resize(vertex_count);
    if (m.fvf & kDiffuse) m.colors.resize(vertex_count);
    if (tex_sets >= 1) m.uv0.resize(vertex_count);
    if (tex_sets >= 2) m.uv1.resize(vertex_count);
    auto vec3 = [&](std::size_t o) { return Vec3{read_le<float>(d, o), read_le<float>(d, o + 4), read_le<float>(d, o + 8)}; };
    for (std::size_t v = 0; v < vertex_count; ++v) {
        std::size_t q = p + v * stride;
        m.positions[v] = vec3(q);
        q += 12;
        if (m.fvf & kNormal) { m.normals[v] = vec3(q); q += 12; }
        if (m.fvf & kDiffuse) { m.colors[v] = read_le<std::uint32_t>(d, q); q += 4; }
        if (m.fvf & kSpecular) q += 4;
        if (tex_sets >= 1) m.uv0[v] = {read_le<float>(d, q), read_le<float>(d, q + 4)};
        if (tex_sets >= 2) m.uv1[v] = {read_le<float>(d, q + 8), read_le<float>(d, q + 12)};
    }
    return m;
}

// 60 bytes: u32 header size (60), u32 mesh id, 6 x u16 ranges, bbox (max x, min x,
// max y, min y, max z, min z), center xyz, radius.
VMeshRef VMeshRef::parse(Bytes d) {
    if (read_le<std::uint32_t>(d, 0) != 60) throw FormatError("unexpected VMeshRef header size");
    VMeshRef r;
    r.mesh_id = read_le<std::uint32_t>(d, 4);
    r.start_vertex = read_le<std::uint16_t>(d, 8);
    r.vertex_count = read_le<std::uint16_t>(d, 10);
    r.start_index = read_le<std::uint16_t>(d, 12);
    r.index_count = read_le<std::uint16_t>(d, 14);
    r.start_group = read_le<std::uint16_t>(d, 16);
    r.group_count = read_le<std::uint16_t>(d, 18);
    auto f = [&](int i) { return read_le<float>(d, 20 + 4 * i); };
    r.bbox_max = {f(0), f(2), f(4)};
    r.bbox_min = {f(1), f(3), f(5)};
    r.center = {f(6), f(7), f(8)};
    r.radius = f(9);
    return r;
}

}  // namespace sterrenvaarder
