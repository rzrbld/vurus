#pragma once
#include <cstdint>
#include <vector>

namespace vyruz {

// File extents for PCM interleaved with other media (for example AVI).
struct PcmChunk { uint64_t offset=0,bytes=0; };

struct Wave {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    std::vector<uint8_t> pcm; // raw sample data
};

// Loads a PCM WAV (RIFF) into memory. Returns false on unsupported formats.
bool loadWav(const std::vector<uint8_t>& data, Wave& out);

} // namespace vyruz
