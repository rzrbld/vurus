#include "assets/tga.h"

#include <cstring>

namespace vyruz {

namespace {

inline uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

struct Header {
    int type = 0;
    int width = 0;
    int height = 0;
    int depth = 0;
    int descriptor = 0;
    const uint8_t* pixels = nullptr;
};

bool parseHeader(const std::vector<uint8_t>& data, Header& h) {
    if (data.size() < 18) return false;
    const uint8_t idLen = data[0];
    const uint8_t cmapType = data[1];
    h.type = data[2];
    if (cmapType != 0) return false;          // color-mapped TGAs unsupported
    if (h.type != 2 && h.type != 10) return false;
    h.width = data[12] | (data[13] << 8);
    h.height = data[14] | (data[15] << 8);
    h.depth = data[16];
    h.descriptor = data[17];
    if (h.depth != 16 && h.depth != 24 && h.depth != 32) return false;
    if (h.width <= 0 || h.height <= 0 || h.width > 8192 || h.height > 8192) return false;
    const size_t off = 18 + idLen;
    if (off >= data.size()) return false;
    h.pixels = data.data() + off;
    return true;
}

inline void putPixel(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = a;
}

bool decodePixel(const Header& h, const uint8_t* p, uint8_t* out) {
    switch (h.depth) {
        case 16: {
            const uint16_t v = rd16(p);
            // 15-bit RGB555 (the game's native 16-bit pixel format).
            const uint8_t r = static_cast<uint8_t>(((v >> 10) & 0x1F) * 255 / 31);
            const uint8_t g = static_cast<uint8_t>(((v >> 5) & 0x1F) * 255 / 31);
            const uint8_t b = static_cast<uint8_t>((v & 0x1F) * 255 / 31);
            const uint8_t a = (v & 0x8000) ? 0xFF : 0x00;
            putPixel(out, r, g, b, a);
            return true;
        }
        case 24:
            putPixel(out, p[2], p[1], p[0], 0xFF); // BGR
            return true;
        case 32:
            putPixel(out, p[2], p[1], p[0], p[3]); // BGRA
            return true;
        default:
            return false;
    }
}

} // namespace

bool loadTga(const std::vector<uint8_t>& data, Image& out) {
    Header h;
    if (!parseHeader(data, h)) return false;

    out.width = h.width;
    out.height = h.height;
    const size_t npix = static_cast<size_t>(h.width) * h.height;
    out.rgba.assign(npix * 4, 0);

    const int bpp = h.depth / 8;
    const int originTop = (h.descriptor & 0x20) != 0; // bit 5: top-left origin
    const size_t dataSize = data.size() - (h.pixels - data.data());

    if (h.type == 2) {
        // Uncompressed. Pixels stored bottom-up unless originTop.
        if (dataSize < npix * bpp) return false;
        const uint8_t* src = h.pixels;
        for (int row = 0; row < h.height; ++row) {
            const int dstRow = originTop ? row : (h.height - 1 - row);
            uint8_t* dst = out.rgba.data() + static_cast<size_t>(dstRow) * h.width * 4;
            for (int x = 0; x < h.width; ++x) {
                decodePixel(h, src, dst + static_cast<size_t>(x) * 4);
                src += bpp;
            }
        }
        return true;
    }

    // RLE (type 10).
    const uint8_t* src = h.pixels;
    const uint8_t* end = data.data() + data.size();
    size_t pixelIndex = 0;
    while (pixelIndex < npix) {
        if (src >= end) return false;
        const uint8_t packet = *src++;
        const uint32_t count = (packet & 0x7F) + 1;
        if (packet & 0x80) {
            // Run-length packet: repeat one pixel.
            if (src + bpp > end) return false;
            uint8_t px[4];
            if (!decodePixel(h, src, px)) return false;
            src += bpp;
            for (uint32_t i = 0; i < count && pixelIndex < npix; ++i, ++pixelIndex) {
                const size_t y = pixelIndex / h.width;
                const size_t x = pixelIndex % h.width;
                const size_t dstRow = originTop ? y : (h.height - 1 - y);
                std::memcpy(out.rgba.data() + (dstRow * h.width + x) * 4, px, 4);
            }
        } else {
            // Raw packet: count literal pixels.
            for (uint32_t i = 0; i < count && pixelIndex < npix; ++i, ++pixelIndex) {
                if (src + bpp > end) return false;
                const size_t y = pixelIndex / h.width;
                const size_t x = pixelIndex % h.width;
                const size_t dstRow = originTop ? y : (h.height - 1 - y);
                decodePixel(h, src, out.rgba.data() + (dstRow * h.width + x) * 4);
                src += bpp;
            }
        }
    }
    return true;
}

} // namespace vyruz
