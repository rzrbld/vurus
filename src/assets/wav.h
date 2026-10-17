#pragma once
#include <cstdint>
#include <vector>

namespace vyruz {

struct Wave {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    std::vector<uint8_t> pcm; // raw sample data
};

// Loads a PCM WAV (RIFF) into memory. Returns false on unsupported formats.
bool loadWav(const std::vector<uint8_t>& data, Wave& out);

} // namespace vyruz
