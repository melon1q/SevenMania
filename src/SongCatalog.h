#pragma once

#include "BmsParser.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bms {

// Versioned, lightweight data contract consumed by the C# song-selection UI.
std::string serializeSongCatalog(const std::vector<Chart>& charts,
    const std::filesystem::path& libraryRoot, uint64_t parseSeed);

} // namespace bms
