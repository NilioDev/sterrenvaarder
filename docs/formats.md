# Data formats (as implemented)

Notes on the original game's data formats, written while building the readers in
`src/sterrenvaarder`. All checks were run against a retail v1.0 install (Feb 2003).

| Format | Files | Reader | Corpus result |
| --- | --- | --- | --- |
| BINI (binary INI) | 1,251 | `ini.cpp` | all parse |
| UTF tree container | 3,072 | `utf.cpp` | all parse |
| VMeshData / VMeshRef | 2,178 meshes | `vmesh.cpp` | all index ranges valid |
| Compound models (.cmp/.3db) | 1,852 | `model.cpp` | all load, 7,167 parts |
| Textures (TGA 8/16/24/32-bit, DDS DXT1/3/5 + uncompressed) | 6,849 | `texture.cpp` | all decode |
| Texture animations ("Frame rects") | | `texture.cpp` | all parse |

## BINI
`"BINI"`, u32 version (1), u32 string table offset. Sections until the string table:
u16 name offset, u16 entry count. Entry: u16 name offset, u8 value count. Value: u8 type
(1 = int32, 2 = float, 3 = string offset) + 4 data bytes.

## UTF
Header after `"UTF "`: version 0x101, tree offset, tree size, reserved, entry size (44),
names offset, names allocated, names used, data offset. 44-byte nodes: next sibling,
name offset, flags (0x10 directory, 0x80 leaf), sharing, child/data offset, allocated
size, size, uncompressed size, three timestamps. An empty directory has child offset 0.

## Model hash
Mesh and material references use a CRC-32 variant over the lowercased name: the usual
reflected 0xEDB88320 update, but each table entry's top byte is XORed with the
carry-less product `i * 0x7E` (see `hash.cpp`). Verified against VMeshRef ids and mesh
group material ids in the retail data.

## VMeshData
u32 type (1), u32 surface (4), u16 group count, u16 index count, u16 FVF, u16 vertex count.
Groups (12 bytes): u32 material hash, u16 start vertex, u16 end vertex, u16 index count,
u16 padding. Then u16 indices, then vertices laid out by the Direct3D FVF
(seen: 0x002, 0x012, 0x102, 0x112, 0x142, 0x152, 0x212, 0x252).

## VMeshRef (60 bytes)
u32 size (60), u32 mesh hash, u16 start vertex, vertex count, start index, index count,
start group, group count, bbox as max x, min x, max y, min y, max z, min z, center xyz,
radius. A group's indices are relative to `ref.start_vertex + group.start_vertex`.

## Compound joints (`Cmpnd/Cons`)
Records: parent name[64], child name[64], then
- Fix / Loose (176 B): origin, 3x3 rotation
- Rev / Pris (208 B): origin, offset, rotation, axis, min, max
- Sphere (212 B): origin, offset, rotation, min xyz, max xyz

## Level of detail
`MultiLevel/Switch2` holds N+1 floats: switch distances for N levels. Ships also carry
`LODranges` in shiparch.ini. Sterrenvaarder keeps these as the default visibility rules.
