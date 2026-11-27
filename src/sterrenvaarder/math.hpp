// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <array>

namespace sterrenvaarder {

struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };

// Row-major 3x3 rotation as stored in model joints (row i = rotated basis i).
using Mat3 = std::array<float, 9>;
inline constexpr Mat3 kIdentity3{1, 0, 0, 0, 1, 0, 0, 0, 1};

}  // namespace sterrenvaarder
