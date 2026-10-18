#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "assets/gaf.h"
#include "assets/gaf_bitmap.h"

namespace vyruz {

// One decoded sprite frame plus its hot-spot (origin) inside the image.
struct GafFrame {
    GafBitmap image;
    int originX = 0;
    int originY = 0;
    uint32_t bitmapFlags = 0;
    uint16_t colorKey = 0x7c1f;
    int flippedOriginX = 0;
    int flippedOriginY = 0;
};

// DATA/ANM_ field2 entries (28 bytes each). The frame number on disk is 1-based.
struct GafSubframe {
    static constexpr uint32_t Blank = UINT32_MAX;
    uint32_t frameIndex = 0; // zero-based index, or Blank for an authored gap
    int offsetX = 0;
    int offsetY = 0;
    uint32_t duration = 0;
    uint32_t flags = 0;
    uint32_t command = 0; // bit 0 triggers the one-based sample index
    uint32_t sample = 0;
};

struct GafSequence {
    std::vector<GafSubframe> subframes;
    uint32_t flags = 0;
    uint32_t loopStart = 1; // one-based authored frame
};

// GAF bitmap bank and its ANM_ sequence table; no assumed frame/angle ordering.
struct GafAnimation {
    std::vector<GafFrame> frames;
    std::vector<GafSequence> sequences;
};

// Selective modes preserve frame indices, origins and all sequence metadata.
// Unselected images have no pixel storage. Metadata skips the bitmap stream.
enum class GafDecodeMode { All, Metadata, FirstBitmap, FirstSequencePose, SequenceFirstPoses, SelectedSequences };

// SelectedSequences takes a sequence mask after ANM parsing. All metadata and
// bitmap headers remain validated; the shared pixel stream is inflated once.
// Decodes every sprite frame by default; narrower modes avoid unused pixels.
bool decodeGafAnimation(const std::vector<uint8_t>& data, GafAnimation& out,
                        GafDecodeMode mode = GafDecodeMode::All,
                        const std::function<std::vector<bool>(const GafAnimation&)>& selectSequences = {});

// Decodes the first sprite frame of a GAF animation into a 32-bit RGBA image.
bool decodeGafSprite(const std::vector<uint8_t>& data, Image& out);

} // namespace vyruz
