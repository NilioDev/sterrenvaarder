// SPDX-License-Identifier: MPL-2.0
#pragma once
// Minimal PNG writer (stored deflate blocks) so tools have no external dependencies.
#include <cstdint>
#include <vector>

#include "sterrenvaarder/texture.hpp"

namespace sterrenvaarder {
std::vector<std::uint8_t> encode_png(const Image& img);
}
