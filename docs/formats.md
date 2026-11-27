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

## Alchemy effects (`.ale`)
A UTF file with two leaves, `ALEffectLib/ALEffectLib` and `AlchemyNodeLibrary/AlchemyNodeLibrary`.
Both start with f32 version and u32 count. Strings are u16 length (with the NUL), the
bytes, then padding to an even length. Worked out by observing all 596 files of a retail
install; every file reads to its last byte with the rules below.

Node library, per node: kind string (`FxSphereEmitter`, `FxBasicAppearance`, ...), then
properties until a u16 0. A property is u16 type, u32 name hash, payload:

| type | payload |
|------|---------|
| 0x0001 / 0x8001 | bool false / true (no payload) |
| 0x0002 | 32-bit integer |
| 0x0003 | f32 |
| 0x0103 | string |
| 0x0104 | blend: u32 source, u32 destination (Direct3D blend factors) |
| 0x0105 | transform: u16, u16 flags; with flags & 0x8000, nine curves (translate, rotate, scale) |
| 0x0200 | animated float: u8, u8 n, n x (f32 param, u8, u8 m, m x (f32 t, f32 v)) |
| 0x0201 | animated colour: as 0x0200 with keys (f32 t, f32 r, g, b) |
| 0x0202 | curve: u8, u8 n, n x (f32 param, f32 value, u16 flags, u16 m, m x (f32 time, value, tangent in, out)) |

Every node has its name as a string property (hash 0xEFA8CE01). The "param" of animated
values is the effect's spark parameter (an engine's throttle, for instance); the `t` of
animated floats and colours is a particle's normalised age.

Effect library, per effect: name; with version 1.1 four f32; u32 n and n references
(u32 flags, u32 node CRC, u32 parent id, u32 id); u32 m and m pairs (emitter id,
appearance id). The root reference has CRC 0xEE223B51 and parent 0x8000. Node CRCs are
the model CRC of the node name *without* folding case.
