#include "assets/wav.h"

#include <cstring>
#include <utility>

namespace vyruz {

namespace {

inline uint32_t rd32le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint16_t rd16le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

} // namespace

bool loadWav(const std::vector<uint8_t>& data, Wave& out) {
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 ||
        std::memcmp(data.data() + 8, "WAVE", 4) != 0) return false;
    const uint32_t riffSize = rd32le(data.data() + 4);
    if (riffSize < 4 || uint64_t(riffSize) + 8 > data.size()) return false;
    const size_t end = size_t(riffSize) + 8;
    size_t off = 12, pcmOffset = 0, pcmSize = 0;
    Wave next;
    bool haveFmt = false, haveData = false;
    while (off < end) {
        if (end - off < 8) return false;
        const uint32_t id = rd32le(data.data() + off);
        const uint32_t size = rd32le(data.data() + off + 4);
        off += 8;
        if (size > end - off) return false;
        const uint8_t* body = data.data() + off;
        if (id == 0x20746d66) {
            if (haveFmt || size < 16 || rd16le(body) != 1) return false;
            next.channels = rd16le(body + 2);
            next.sampleRate = rd32le(body + 4);
            next.bitsPerSample = rd16le(body + 14);
            if ((next.channels != 1 && next.channels != 2) ||
                (next.bitsPerSample != 8 && next.bitsPerSample != 16) ||
                next.sampleRate < 1000 || next.sampleRate > 384000) return false;
            const unsigned frameSize = next.channels * (next.bitsPerSample / 8);
            if (rd16le(body + 12) != frameSize || rd32le(body + 8) != next.sampleRate * frameSize) return false;
            haveFmt = true;
        } else if (id == 0x61746164) {
            if (haveData) return false;
            pcmOffset = off;
            pcmSize = size;
            haveData = true;
        }
        off += size;
        if (size & 1) { if (off == end) return false; ++off; }
    }
    if (!haveFmt || !haveData || pcmSize == 0 ||
        pcmSize % (next.channels * (next.bitsPerSample / 8)) != 0) return false;
    next.pcm.assign(data.begin() + pcmOffset, data.begin() + pcmOffset + pcmSize);
    out = std::move(next);
    return true;
}

} // namespace vyruz
