// SPDX-License-Identifier: MPL-2.0
#include "sterrenvaarder/texture.hpp"

#include <algorithm>
#include <string>

#include "sterrenvaarder/utf.hpp"

namespace sterrenvaarder {

namespace {

void finish_alpha(Image& img) {
    img.has_alpha = false;
    for (std::size_t i = 3; i < img.rgba.size(); i += 4)
        if (img.rgba[i] != 255) { img.has_alpha = true; break; }
}

void put_bgr(std::uint8_t* out, std::uint32_t bpp, Bytes px, std::size_t o, bool alpha_bits) {
    if (bpp == 8) { out[0] = out[1] = out[2] = px[o]; out[3] = 255; return; }
    if (bpp == 16) {
        auto v = read_le<std::uint16_t>(px, o);  // A1R5G5B5
        out[0] = static_cast<std::uint8_t>(((v >> 10) & 31) * 255 / 31);
        out[1] = static_cast<std::uint8_t>(((v >> 5) & 31) * 255 / 31);
        out[2] = static_cast<std::uint8_t>((v & 31) * 255 / 31);
        out[3] = (alpha_bits && !(v & 0x8000)) ? 0 : 255;
        return;
    }
    out[0] = px[o + 2];
    out[1] = px[o + 1];
    out[2] = px[o];
    out[3] = bpp == 32 ? px[o + 3] : 255;
}

}  // namespace

// TGA: types 1 (palette), 2 (truecolor), 3 (grey), and their RLE forms 9/10/11.
Image decode_tga(Bytes d) {
    auto id_len = read_le<std::uint8_t>(d, 0);
    auto cmap_type = read_le<std::uint8_t>(d, 1);
    auto type = read_le<std::uint8_t>(d, 2);
    auto cmap_first = read_le<std::uint16_t>(d, 3);
    auto cmap_len = read_le<std::uint16_t>(d, 5);
    auto cmap_bpp = read_le<std::uint8_t>(d, 7);
    Image img;
    img.width = read_le<std::uint16_t>(d, 12);
    img.height = read_le<std::uint16_t>(d, 14);
    auto bpp = read_le<std::uint8_t>(d, 16);
    auto desc = read_le<std::uint8_t>(d, 17);
    bool alpha_bits = (desc & 0x0F) != 0;
    bool top_down = desc & 0x20;
    std::size_t p = 18 + id_len;

    std::vector<std::uint8_t> palette;  // RGBA
    if (cmap_type == 1) {
        std::size_t esz = (cmap_bpp + 7) / 8;
        palette.resize(std::size_t(cmap_len) * 4);
        for (std::size_t i = 0; i < cmap_len; ++i) put_bgr(&palette[i * 4], cmap_bpp, d, p + i * esz, cmap_bpp == 16 && alpha_bits);
        p += esz * cmap_len;
    }
    const std::uint8_t base = type & 7;
    const bool rle = type & 8;
    if (base < 1 || base > 3) throw FormatError("unsupported TGA type " + std::to_string(type));
    std::size_t pix_size = (bpp + 7) / 8;
    std::size_t count = std::size_t(img.width) * img.height;
    img.rgba.assign(count * 4, 255);

    auto emit = [&](std::size_t i, std::size_t src) {
        std::uint8_t* out = &img.rgba[i * 4];
        if (base == 1) {
            std::uint32_t idx = pix_size == 1 ? d[src] : read_le<std::uint16_t>(d, src);
            idx -= cmap_first;
            if (std::size_t(idx) * 4 + 4 > palette.size()) throw FormatError("TGA palette index out of range");
            std::copy_n(&palette[idx * 4], 4, out);
        } else {
            put_bgr(out, bpp, d, src, alpha_bits);
        }
    };
    if (p + (rle ? 0 : count * pix_size) > d.size()) throw FormatError("TGA truncated");
    if (!rle) {
        for (std::size_t i = 0; i < count; ++i) emit(i, p + i * pix_size);
    } else {
        std::size_t i = 0;
        while (i < count) {
            auto h = read_le<std::uint8_t>(d, p++);
            std::size_t n = (h & 0x7F) + 1;
            if (h & 0x80) {
                if (p + pix_size > d.size()) throw FormatError("TGA RLE truncated");
                for (std::size_t k = 0; k < n && i < count; ++k) emit(i++, p);
                p += pix_size;
            } else {
                if (p + n * pix_size > d.size()) throw FormatError("TGA RLE truncated");
                for (std::size_t k = 0; k < n && i < count; ++k) emit(i++, p + k * pix_size);
                p += n * pix_size;
            }
        }
    }
    if (!top_down) {  // TGA default origin is bottom-left
        std::size_t row = std::size_t(img.width) * 4;
        for (std::uint32_t y = 0; y < img.height / 2; ++y)
            std::swap_ranges(img.rgba.begin() + y * row, img.rgba.begin() + (y + 1) * row,
                             img.rgba.begin() + (img.height - 1 - y) * row);
    }
    finish_alpha(img);
    return img;
}

namespace {

void rgb565(std::uint16_t c, std::uint8_t* o) {
    o[0] = static_cast<std::uint8_t>(((c >> 11) & 31) * 255 / 31);
    o[1] = static_cast<std::uint8_t>(((c >> 5) & 63) * 255 / 63);
    o[2] = static_cast<std::uint8_t>((c & 31) * 255 / 31);
    o[3] = 255;
}

// Decodes one 4x4 DXT colour block into 16 RGBA pixels.
void color_block(Bytes d, std::size_t o, std::uint8_t px[16][4], bool dxt1) {
    auto c0 = read_le<std::uint16_t>(d, o), c1 = read_le<std::uint16_t>(d, o + 2);
    auto bits = read_le<std::uint32_t>(d, o + 4);
    std::uint8_t pal[4][4];
    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    if (!dxt1 || c0 > c1) {
        for (int k = 0; k < 4; ++k) {
            pal[2][k] = static_cast<std::uint8_t>((2 * pal[0][k] + pal[1][k]) / 3);
            pal[3][k] = static_cast<std::uint8_t>((pal[0][k] + 2 * pal[1][k]) / 3);
        }
    } else {
        for (int k = 0; k < 4; ++k) pal[2][k] = static_cast<std::uint8_t>((pal[0][k] + pal[1][k]) / 2);
        pal[3][0] = pal[3][1] = pal[3][2] = 0;
        pal[3][3] = 0;  // 1-bit transparency
    }
    pal[2][3] = 255;
    if (!dxt1 || c0 > c1) pal[3][3] = 255;
    for (int i = 0; i < 16; ++i) std::copy_n(pal[(bits >> (2 * i)) & 3], 4, px[i]);
}

}  // namespace

Image decode_dds(Bytes d) {
    if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) throw FormatError("not a DDS file");
    Image img;
    img.height = read_le<std::uint32_t>(d, 12);
    img.width = read_le<std::uint32_t>(d, 16);
    auto pf_flags = read_le<std::uint32_t>(d, 80);
    auto fourcc = read_le<std::uint32_t>(d, 84);
    std::size_t p = 128;
    std::size_t w = img.width, h = img.height;
    img.rgba.assign(w * h * 4, 255);

    if (pf_flags & 0x4) {  // FOURCC: DXT1/3/5
        std::string cc(reinterpret_cast<const char*>(d.data() + 84), 4);
        int kind = cc == "DXT1" ? 1 : cc == "DXT3" ? 3 : cc == "DXT5" ? 5 : 0;
        if (!kind) throw FormatError("unsupported DDS fourcc " + cc);
        std::size_t bsize = kind == 1 ? 8 : 16;
        std::size_t bw = (w + 3) / 4, bh = (h + 3) / 4;
        if (p + bw * bh * bsize > d.size()) throw FormatError("DDS truncated");
        for (std::size_t by = 0; by < bh; ++by)
            for (std::size_t bx = 0; bx < bw; ++bx) {
                std::size_t o = p + (by * bw + bx) * bsize;
                std::uint8_t px[16][4];
                color_block(d, kind == 1 ? o : o + 8, px, kind == 1);
                if (kind == 3) {
                    for (int i = 0; i < 16; ++i) {
                        auto nib = (d[o + i / 2] >> ((i & 1) * 4)) & 0xF;
                        px[i][3] = static_cast<std::uint8_t>(nib * 17);
                    }
                } else if (kind == 5) {
                    std::uint8_t a0 = d[o], a1 = d[o + 1], a[8];
                    a[0] = a0;
                    a[1] = a1;
                    if (a0 > a1) for (int k = 1; k < 7; ++k) a[k + 1] = static_cast<std::uint8_t>(((7 - k) * a0 + k * a1) / 7);
                    else {
                        for (int k = 1; k < 5; ++k) a[k + 1] = static_cast<std::uint8_t>(((5 - k) * a0 + k * a1) / 5);
                        a[6] = 0;
                        a[7] = 255;
                    }
                    std::uint64_t bits = 0;
                    for (int k = 0; k < 6; ++k) bits |= std::uint64_t(d[o + 2 + k]) << (8 * k);
                    for (int i = 0; i < 16; ++i) px[i][3] = a[(bits >> (3 * i)) & 7];
                }
                for (int i = 0; i < 16; ++i) {
                    std::size_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
                    if (x < w && y < h) std::copy_n(px[i], 4, &img.rgba[(y * w + x) * 4]);
                }
            }
    } else {  // uncompressed RGB(A) with bit masks
        auto bits = read_le<std::uint32_t>(d, 88);
        std::uint32_t mask[4] = {read_le<std::uint32_t>(d, 92), read_le<std::uint32_t>(d, 96),
                                 read_le<std::uint32_t>(d, 100), read_le<std::uint32_t>(d, 104)};
        if (!(pf_flags & 0x1)) mask[3] = 0;  // no alpha channel
        std::size_t bpp = bits / 8;
        if (bpp < 1 || bpp > 4) throw FormatError("unsupported DDS bit depth");
        if (p + w * h * bpp > d.size()) throw FormatError("DDS truncated");
        for (std::size_t i = 0; i < w * h; ++i) {
            std::uint32_t v = 0;
            for (std::size_t k = 0; k < bpp; ++k) v |= std::uint32_t(d[p + i * bpp + k]) << (8 * k);
            for (int c = 0; c < 4; ++c) {
                if (!mask[c]) continue;
                std::uint32_t m = mask[c];
                int shift = 0;
                while (!(m & 1)) { m >>= 1; ++shift; }
                img.rgba[i * 4 + c] = static_cast<std::uint8_t>(((v >> shift) & m) * 255 / m);
            }
        }
    }
    (void)fourcc;
    finish_alpha(img);
    return img;
}

Image decode_texture(Bytes d) {
    if (d.size() >= 4 && std::memcmp(d.data(), "DDS ", 4) == 0) return decode_dds(d);
    return decode_tga(d);
}

bool is_animation_node(const UtfNode& node) { return node.child("Frame count") != nullptr; }

TextureAnimation parse_animation_node(const UtfNode& node) {
    TextureAnimation a;
    if (auto n = node.child("Texture count")) a.texture_count = n->as<std::uint32_t>();
    if (auto n = node.child("Frame count")) a.frame_count = n->as<std::uint32_t>();
    if (auto n = node.child("FPS")) a.fps = n->as<float>();
    if (auto n = node.child("Frame rects"))
        for (std::size_t o = 0; o + 20 <= n->data.size(); o += 20)
            a.frames.push_back({read_le<std::uint32_t>(n->data, o), read_le<float>(n->data, o + 4), read_le<float>(n->data, o + 8),
                                read_le<float>(n->data, o + 12), read_le<float>(n->data, o + 16)});
    return a;
}

Image decode_texture_node(const UtfNode& node) {
    if (is_animation_node(node)) throw FormatError("texture node is an animation, not an image: " + node.name);
    if (auto mips = node.child("MIPS")) return decode_dds(mips->data);
    if (auto mip0 = node.child("MIP0")) return decode_tga(mip0->data);
    for (auto& c : node.children)  // some textures only have a single unnamed level
        if (c.leaf) return decode_texture(c.data);
    throw FormatError("texture node has no image: " + node.name);
}

}  // namespace sterrenvaarder
