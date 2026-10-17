#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vyruz {

// Vyruz LZSS variant (reimplemented from observed behaviour).
// 4096-byte sliding window, 12-bit offset / 4-bit length match, 1-byte
// literals, 8 control bits per flag byte (MSB-first).
std::vector<uint8_t> lzssDecompress(const uint8_t* src, size_t srcLen, size_t dstSize);

} // namespace vyruz
