#include "assets/lzss.h"

#include <cstring>

namespace vyruz {

std::vector<uint8_t> lzssDecompress(const uint8_t* src, size_t srcLen, size_t dstSize) {
    std::vector<uint8_t> dst;
    dst.reserve(dstSize);

    uint8_t hist[4096];
    std::memset(hist, 0x20, sizeof(hist));
    size_t histPos = 0xfee;
    uint32_t bits = 0;
    size_t ip = 0;

    while (dst.size() < dstSize) {
        bits >>= 1;
        if ((bits & 0x100) == 0) {
            if (ip >= srcLen) break;
            bits = 0xff00u | src[ip++];
        }
        if ((bits & 1) == 0) {
            // Match: copy (len) bytes from history at (offset).
            if (ip + 1 >= srcLen) break;
            const uint8_t b0 = src[ip++];
            const uint8_t b1 = src[ip++];
            uint32_t off = b0 | ((b1 & 0xf0) << 4);
            const uint32_t len = (b1 & 0x0f) + 3;
            for (uint32_t i = 0; i < len && dst.size() < dstSize; ++i) {
                const uint8_t b = hist[off & 0xfff];
                dst.push_back(b);
                hist[histPos & 0xfff] = b;
                histPos = (histPos + 1) & 0xfff;
                ++off;
            }
        } else {
            // Literal byte.
            if (ip >= srcLen) break;
            const uint8_t b = src[ip++];
            dst.push_back(b);
            hist[histPos & 0xfff] = b;
            histPos = (histPos + 1) & 0xfff;
        }
    }
    return dst;
}

} // namespace vyruz
