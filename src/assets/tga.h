#pragma once
#include <cstdint>
#include <vector>

namespace vyruz {

// Decoded 32-bit RGBA image.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // width*height*4, R,G,B,A
};

// Loads a TGA (type 2 uncompressed or type 10 RLE, 16/24/32-bit).
// 16-bit images are decoded as RGB565 when the TGA descriptor has no alpha
// bit, otherwise ARGB1555 (matching the original engine's 16-bit mode).
bool loadTga(const std::vector<uint8_t>& data, Image& out);

} // namespace vyruz
