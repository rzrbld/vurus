#include "assets/gaf_sprite.h"

#include <cstring>
#include <utility>

#include "assets/lzss.h"

namespace vyruz {

namespace {

inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

// Packed RGB555 rows; the stored runtime pitch is not the disk row length.
bool decodeBmpBlock(const uint8_t* p, size_t len, GafFrame& frame, size_t& blockLen) {
    if (len < 68 || std::memcmp(p, "BMP_", 4) != 0) return false;
    const uint32_t bitmapFlags = rd32(p + 8);
    // EXPLO.GAF also stores 0x311; terrain groundlight.gaf stores0x111.
    // Native 47ba50 passes depth@28 to
    // 44ee70, which maps 16-bit disk samples to format 15 (RGB555),
    // independently of these resource/runtime flags.
    if ((bitmapFlags != 0x40000122 && bitmapFlags != 0x40000322 && bitmapFlags != 0x311 && bitmapFlags != 0x111) ||
        rd32(p + 12) != 56 || rd32(p + 16) != bitmapFlags ||
        rd32(p + 24) != 16 || rd32(p + 28) != 16) return false;
    const int width = static_cast<int>(rd32(p + 32));
    const int height = static_cast<int>(rd32(p + 36));
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    const size_t npix = static_cast<size_t>(width) * height;
    const size_t pixelBytes = rd32(p + 4);
    if (pixelBytes != ((npix * 2 + 3) & ~size_t(3)) || pixelBytes > len - 68) return false;
    blockLen = 68 + pixelBytes;
    frame.bitmapFlags = bitmapFlags;
    frame.colorKey = rd16(p + 60);
    Image& out = frame.image;
    out.width = width;
    out.height = height;
    out.rgba.resize(npix * 4);
    const uint8_t* px = p + 68;
    for (size_t i = 0; i < npix; ++i) {
        const uint16_t v = rd16(px + i * 2);
        out.rgba[i * 4 + 0] = static_cast<uint8_t>(((v >> 10) & 0x1F) * 255 / 31);
        out.rgba[i * 4 + 1] = static_cast<uint8_t>(((v >> 5) & 0x1F) * 255 / 31);
        out.rgba[i * 4 + 2] = static_cast<uint8_t>((v & 0x1F) * 255 / 31);
        out.rgba[i * 4 + 3] = (v == frame.colorKey) ? 0 : 0xFF;
    }
    return true;
}

// Decompresses the four component streams of a GAF DATA leaf (GOB_ or ANM_).
bool gobStreams(const GafChunk& gob, std::vector<std::vector<uint8_t>>& comps) {
    if (gob.payload.size() < 96) return false;
    struct Field { uint32_t compression, csize, usize; };
    Field f[4];
    for (int i = 0; i < 4; ++i) {
        const uint8_t* p = gob.payload.data() + i * 24;
        f[i].compression = rd32(p);
        f[i].csize = rd32(p + 12);
        f[i].usize = rd32(p + 16);
    }
    size_t pos = 96;
    for (int i = 0; i < 4; ++i) {
        if (f[i].usize == 0) {
            comps.emplace_back();
            continue;
        }
        if (f[i].compression != 0 && f[i].compression != 0x100) return false;
        const size_t stored = f[i].compression ? f[i].csize : f[i].usize;
        const size_t aligned = (stored + 3) & ~size_t(3);
        if (aligned > gob.payload.size() - pos) return false;
        if (f[i].compression) {
            comps.push_back(lzssDecompress(gob.payload.data() + pos, stored, f[i].usize));
        } else {
            comps.emplace_back(gob.payload.begin() + pos, gob.payload.begin() + pos + stored);
        }
        if (comps.back().size() != f[i].usize) return false;
        pos += aligned;
    }
    return true;
}

} // namespace

bool decodeGafAnimation(const std::vector<uint8_t>& data, GafAnimation& out) {
    GafChunk root;
    if (!parseGaf(data, root)) return false;
    const GafChunk* head = root.findChild("HEAD");
    const GafChunk* dataChunk = root.findChild("DATA");
    if (!head || !dataChunk) return false;
    const GafChunk* gob = dataChunk->findChild("GOB_");
    const GafChunk* anm = dataChunk->findChild("ANM_");
    const GafChunk* headGob = head->findChild("GOB_");
    const GafChunk* headAnm = head->findChild("ANM_");
    if (!gob || !anm || !headGob || !headAnm ||
        headGob->payload.size() < 4 || headAnm->payload.size() < 4) return false;

    std::vector<std::vector<uint8_t>> comps;
    if (!gobStreams(*gob, comps) || comps.size() < 2) return false;
    const std::vector<uint8_t>& desc = comps[0]; // 44-byte frame descriptors
    const std::vector<uint8_t>& pix = comps[1];  // concatenated BMP_ blocks

    const size_t frameCount = rd32(headGob->payload.data());
    if (frameCount == 0 || frameCount > 100000 || desc.size() != frameCount * 44) return false;

    GafAnimation decoded;
    decoded.frames.reserve(frameCount);
    size_t pos = 0;
    for (size_t i = 0; i < frameCount; ++i) {
        if (pos + 4 > pix.size() || std::memcmp(pix.data() + pos, "BMP_", 4) != 0) return false;
        GafFrame fr;
        size_t blockLen = 0;
        if (!decodeBmpBlock(pix.data() + pos, pix.size() - pos, fr, blockLen)) return false;
        const uint8_t* d = desc.data() + i * 44;
        fr.originX = static_cast<int>(rd32(d + 16));
        fr.originY = static_cast<int>(rd32(d + 20));
        fr.flippedOriginX = static_cast<int32_t>(rd32(d + 28));
        fr.flippedOriginY = static_cast<int32_t>(rd32(d + 32));
        decoded.frames.push_back(std::move(fr));
        pos += blockLen;
    }
    if (pos != pix.size()) return false;
    const size_t sequenceCount = rd32(headAnm->payload.data());
    if (sequenceCount == 0 || sequenceCount > 100000) return false;
    std::vector<std::vector<uint8_t>> animComps;
    if (!gobStreams(*anm, animComps) || animComps.size() < 3 ||
        animComps[0].size() != sequenceCount * 32 || animComps[2].size() % 28 != 0) return false;

    const auto& headers = animComps[0];
    const auto& subframes = animComps[2];
    decoded.sequences.reserve(sequenceCount);
    size_t subframeIndex = 0;
    for (size_t s = 0; s < sequenceCount; ++s) {
        const size_t count = rd32(headers.data() + s * 32);
        if (count > (subframes.size() / 28) - subframeIndex) return false;
        GafSequence sequence;
        sequence.flags = rd32(headers.data() + s * 32 + 4);
        sequence.loopStart = rd32(headers.data() + s * 32 + 8);
        sequence.subframes.reserve(count);
        for (size_t j = 0; j < count; ++j) {
            const uint8_t* p = subframes.data() + (subframeIndex + j) * 28;
            const uint32_t number = rd32(p);
            if (number > frameCount) return false;
            sequence.subframes.push_back({number == 0 ? GafSubframe::Blank : number - 1,
                                          static_cast<int32_t>(rd32(p + 4)),
                                          static_cast<int32_t>(rd32(p + 8)), rd32(p + 24), rd32(p + 12),
                                          rd32(p + 16), rd32(p + 20)});
        }
        subframeIndex += count;
        decoded.sequences.push_back(std::move(sequence));
    }
    if (subframeIndex != subframes.size() / 28) return false;
    out = std::move(decoded);
    return true;
}

bool decodeGafSprite(const std::vector<uint8_t>& data, Image& out) {
    GafAnimation anim;
    if (!decodeGafAnimation(data, anim)) return false;
    out = anim.frames.front().image;
    return true;
}

} // namespace vyruz
