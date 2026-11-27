// SPDX-License-Identifier: MPL-2.0
#pragma once
// Texture decoding to RGBA8. Freelancer stores textures inside UTF "Texture library"
// nodes as either one DDS blob ("MIPS") or one TGA per mip level ("MIP0".."MIPn").
#include <cstdint>
#include <string>
#include <vector>

#include "sterrenvaarder/bytes.hpp"

namespace sterrenvaarder {

struct Image {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;  // top-down rows, 4 bytes per pixel
    bool has_alpha = false;
};

Image decode_tga(Bytes data);
Image decode_dds(Bytes data);  // top mip level only
Image decode_texture(Bytes data);  // by magic: DDS or TGA

struct UtfNode;
// Decodes the highest-resolution image of a Texture library entry node.
Image decode_texture_node(const UtfNode& node);

// Some Texture library entries are not images but flipbook animations over other
// entries ("Texture count", "Frame count", "FPS", "Frame rects").
struct TextureAnimation {
    std::uint32_t texture_count = 0, frame_count = 0;
    float fps = 0;
    struct Rect { std::uint32_t texture; float u0, v0, u1, v1; };
    std::vector<Rect> frames;
};
bool is_animation_node(const UtfNode& node);
TextureAnimation parse_animation_node(const UtfNode& node);

}  // namespace sterrenvaarder
