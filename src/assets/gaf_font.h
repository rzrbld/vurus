#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vyruz {
struct GafGlyphLayer {
    static constexpr uint32_t Blank = UINT32_MAX;
    uint32_t frameIndex = Blank;
    int32_t offsetX = 0, offsetY = 0;
};
struct GafGlyph { std::array<GafGlyphLayer, 2> layers; };
struct GafFont {
    // Preserves authored metadata, including unused style bytes when flags&2==0.
    std::array<uint32_t, 18> words{};
    std::vector<GafGlyph> records;
    bool proportional() const { return (words[1] & 1) != 0; }
    bool hasAuthoredStyle() const { return (words[1] & 2) != 0; }
    int32_t fixedAdvance() const { return static_cast<int32_t>(words[6]); }
    int64_t lineAdvance() const {
        return static_cast<int32_t>(words[7]) +
               (proportional() ? int64_t(static_cast<int32_t>(words[2])) : 0);
    }
    int64_t advance(uint32_t primaryBitmapWidth) const {
        return proportional() && primaryBitmapWidth != 0
            ? int64_t(primaryBitmapWidth) + static_cast<int32_t>(words[3]) : fixedAdvance();
    }
    const GafGlyph* glyph(uint8_t code) const {
        return code != 0 && size_t(code - 1) < records.size() ? &records[code - 1] : nullptr;
    }
};
// References the bitmap bank of the same GAF, with zero-based frame indices.
// Absent FNT_ yields an empty bank; malformed/unsupported data leaves out unchanged.
bool decodeGafFonts(const std::vector<uint8_t>& data, size_t bitmapCount,
                    std::vector<GafFont>& out);
} // namespace vyruz
