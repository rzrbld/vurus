#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vyruz {

// Reverse-engineered Vyruz GAF chunk tree (clean-room documented format).
//
// Chunk header (16 bytes, little-endian):
//   char tag[4]; u32 size; u32 version; u32 count
//   count == 0  -> the following `size` bytes are the raw payload
//   count  > 0  -> the following `size` bytes contain `count` nested chunks
struct GafChunk {
    std::string tag;
    uint32_t version = 0;
    std::vector<uint8_t> payload;     // leaf chunks only
    std::vector<GafChunk> children;   // container chunks only

    const GafChunk* findChild(const char* t) const;
};

bool parseGaf(const std::vector<uint8_t>& data, GafChunk& root);

} // namespace vyruz
