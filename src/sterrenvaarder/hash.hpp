// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <string_view>

namespace sterrenvaarder {

// Hash used inside model files: VMeshRef -> VMeshLibrary entry, mesh -> material.
// A CRC-32 variant: same register/update as the reflected 0xEDB88320 CRC, but the
// lookup table's top byte differs by a linear term (see hash.cpp). Input is lowercased.
std::uint32_t fl_model_crc(std::string_view name);
// The same CRC without folding case (Alchemy effects refer to their nodes this way).
std::uint32_t fl_model_crc_exact(std::string_view name);

// Hash used for game nicknames (ships, equipment, systems): Freelancer's "CreateID".
std::uint32_t fl_create_id(std::string_view nickname);

}  // namespace sterrenvaarder
