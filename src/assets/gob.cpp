#include "assets/gob.h"

#include "assets/lzss.h"

namespace vyruz {

namespace {
inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
} // namespace

bool decodeGobPayload(const std::vector<uint8_t>& payload,
                      std::vector<std::vector<uint8_t>>& comps) {
    if (payload.size() < 96) return false;
    struct Field { uint32_t comp, csize, usize; };
    Field fields[4];
    for (int i = 0; i < 4; ++i) {
        const uint8_t* f = payload.data() + i * 24;
        fields[i].comp = rd32(f);
        fields[i].csize = rd32(f + 12);
        fields[i].usize = rd32(f + 16);
    }
    comps.clear();
    size_t pos = 96;
    for (int i = 0; i < 4; ++i) {
        if (fields[i].usize == 0) continue;
        const size_t aligned = (fields[i].csize + 3) & ~size_t(3);
        if (pos + aligned > payload.size()) return false;
        comps.push_back(lzssDecompress(payload.data() + pos, aligned, fields[i].usize));
        pos += aligned;
    }
    return true;
}

} // namespace vyruz
