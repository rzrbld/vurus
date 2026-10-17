#pragma once
#include <cstdint>
#include <vector>

namespace vyruz {

// Decodes a GOB_ payload: a 96-byte descriptor (4 x 24-byte fields, each
// {u32 compression, u32 compressed_size, u32 uncompressed_size}) followed by
// LZSS-compressed component streams (each aligned to 4 bytes).
// Returns the decompressed components in order (empty components omitted).
bool decodeGobPayload(const std::vector<uint8_t>& payload,
                      std::vector<std::vector<uint8_t>>& comps);

} // namespace vyruz
