#include "assets/gaf_font.h"
#include "assets/gaf.h"
#include "assets/lzss.h"
#include <utility>

namespace vyruz {
namespace {
uint32_t u32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
bool streams(const GafChunk& chunk, std::array<std::vector<uint8_t>, 4>& result) {
    if (chunk.payload.size() < 96) return false;
    size_t pos = 96;
    for (size_t i = 0; i < 4; ++i) {
        const auto* p = chunk.payload.data() + i * 24;
        const uint32_t compression = u32(p), count = u32(p + 16);
        if (!count) continue;
        if ((compression != 0 && compression != 0x100) || count > 64 * 1024 * 1024) return false;
        const size_t stored = compression ? u32(p + 12) : count;
        const size_t aligned = (stored + 3) & ~size_t(3);
        if (aligned > chunk.payload.size() - pos) return false;
        if (compression) result[i] = lzssDecompress(chunk.payload.data() + pos, stored, count);
        else result[i].assign(chunk.payload.begin() + pos, chunk.payload.begin() + pos + stored);
        if (result[i].size() != count) return false;
        pos += aligned;
    }
    return pos == chunk.payload.size();
}
}
bool decodeGafFonts(const std::vector<uint8_t>& data, size_t bitmapCount,
                    std::vector<GafFont>& out) {
    GafChunk root;
    if (!parseGaf(data, root)) return false;
    const auto* head = root.findChild("HEAD");
    const auto* body = root.findChild("DATA");
    if (!head || !body) return false;
    const auto* h = head->findChild("FNT_");
    const auto* b = body->findChild("FNT_");
    if (!h && !b) { out.clear(); return true; }
    if (!h || !b || h->payload.size() != 36 || u32(h->payload.data() + 16) != 24) return false;
    const size_t descriptorBytes=u32(h->payload.data()+4);
    if(descriptorBytes!=72 && descriptorBytes!=64)return false;
    const size_t count = u32(h->payload.data());
    if (count > 100000) return false;
    std::array<std::vector<uint8_t>, 4> parts;
    if (!streams(*b, parts) || parts[0].size() != count * descriptorBytes ||
        !parts[1].empty() || !parts[3].empty() || parts[2].size() % 24) return false;
    std::vector<GafFont> decoded;
    size_t index = 0;
    for (size_t f = 0; f < count; ++f) {
        GafFont font;
        for (size_t w = 0; w < descriptorBytes/4; ++w) font.words[w] = u32(parts[0].data() + f * descriptorBytes + w * 4);
        // Observed legacy inventory format shares metrics/glyphs but omits
        // shadow offsets. Its uninitialized styles are ignored by the consumer.
        if(descriptorBytes==64 && font.hasAuthoredStyle())return false;
        const size_t glyphs = font.words[0];
        if (glyphs > 256 || glyphs > parts[2].size() / 24 - index) return false;
        font.records.resize(glyphs);
        for (size_t g = 0; g < glyphs; ++g) {
            const auto* p = parts[2].data() + (index + g) * 24;
            for (size_t l = 0; l < 2; ++l) {
                const uint32_t bitmap = u32(p + l * 12);
                if (bitmap > bitmapCount) return false;
                font.records[g].layers[l] = {bitmap ? bitmap - 1 : GafGlyphLayer::Blank,
                    static_cast<int32_t>(u32(p + l * 12 + 4)),
                    static_cast<int32_t>(u32(p + l * 12 + 8))};
            }
        }
        index += glyphs;
        decoded.push_back(std::move(font));
    }
    if (index != parts[2].size() / 24) return false;
    out = std::move(decoded);
    return true;
}
} // namespace vyruz
