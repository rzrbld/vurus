#include "assets/gaf.h"

#include <cstring>

namespace vyruz {

namespace {

inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool parseChunk(const uint8_t* data, size_t size, size_t& off, GafChunk& chunk) {
    if (off + 16 > size) return false;
    chunk.tag.assign(reinterpret_cast<const char*>(data + off), 4);
    const uint32_t payloadSize = rd32(data + off + 4);
    chunk.version = rd32(data + off + 8);
    const uint32_t count = rd32(data + off + 12);
    off += 16;
    if (payloadSize > size - off) return false;

    if (count == 0) {
        chunk.payload.assign(data + off, data + off + payloadSize);
    } else {
        chunk.children.resize(count);
        size_t childOff = off;
        for (uint32_t i = 0; i < count; ++i) {
            if (!parseChunk(data, size, childOff, chunk.children[i])) return false;
        }
        if (childOff != off + payloadSize) return false;
    }
    off += payloadSize;
    return true;
}

} // namespace

const GafChunk* GafChunk::findChild(const char* t) const {
    for (const auto& c : children) {
        if (c.tag == t) return &c;
    }
    return nullptr;
}

bool parseGaf(const std::vector<uint8_t>& data, GafChunk& root) {
    size_t off = 0;
    if (!parseChunk(data.data(), data.size(), off, root)) return false;
    return off == data.size();
}

} // namespace vyruz
