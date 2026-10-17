#include "assets/gaf_sprite.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
void put(Bytes& b, size_t off, uint32_t value) {
    for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>(value >> (8 * i));
}
void append(Bytes& b, const Bytes& tail) { b.insert(b.end(), tail.begin(), tail.end()); }
Bytes chunk(const char* tag, Bytes payload, uint32_t children = 0) {
    Bytes result(16);
    for (int i = 0; i < 4; ++i) result[i] = tag[i];
    put(result, 4, payload.size()); put(result, 12, children);
    append(result, payload); return result;
}
Bytes streams(const std::array<Bytes, 4>& components) {
    Bytes result(96);
    for (size_t i = 0; i < components.size(); ++i) {
        put(result, i * 24 + 16, components[i].size());
        append(result, components[i]);
        while (result.size() % 4) result.push_back(0);
    }
    return result;
}
// A synthetic odd-width black-keyed bitmap, followed by a timed blank pose.
Bytes fixture(uint32_t format = 0x40000322, uint32_t storedBytes = 8,
              uint32_t secondBitmap = 0) {
    Bytes descriptor(44); put(descriptor, 0, 1); put(descriptor, 4, 3);
    put(descriptor, 8, 1); put(descriptor, 16, static_cast<uint32_t>(-2));
    put(descriptor, 20, 5);
    // Deliberately differ from the usual -origin-size formula: preserve disk values.
    put(descriptor, 28, static_cast<uint32_t>(-7)); put(descriptor, 32, 9);
    Bytes bitmap(76);
    bitmap[0] = 'B'; bitmap[1] = 'M'; bitmap[2] = 'P'; bitmap[3] = '_';
    put(bitmap, 4, storedBytes); put(bitmap, 8, format); put(bitmap, 12, 56);
    put(bitmap, 16, format); put(bitmap, 20, 1); put(bitmap, 24, 16);
    put(bitmap, 28, 16); put(bitmap, 32, 3); put(bitmap, 36, 1);
    put(bitmap, 40, 8); put(bitmap, 60, 0);
    // RGB555 red, black, magenta, then padding (not a fourth pixel).
    bitmap[68] = 0; bitmap[69] = 0x7c;
    bitmap[72] = 0x1f; bitmap[73] = 0x7c;
    bitmap[74] = 0xaa; bitmap[75] = 0xbb;
    Bytes sequence(32); put(sequence, 0, 2); put(sequence, 4, 7); put(sequence, 8, 2);
    Bytes poses(56); put(poses, 0, 1); put(poses, 4, static_cast<uint32_t>(-3));
    put(poses, 8, 4); put(poses, 12, 0x108); put(poses, 24, 8);
    put(poses, 28, secondBitmap); put(poses, 52, 110);
    Bytes headG(36), headA(36); put(headG, 0, 1); put(headA, 0, 1);
    Bytes head; append(head, chunk("GOB_", headG)); append(head, chunk("ANM_", headA));
    Bytes data; append(data, chunk("GOB_", streams({descriptor, bitmap, {}, {}})));
    append(data, chunk("ANM_", streams({sequence, {}, poses, {}})));
    Bytes root; append(root, chunk("HEAD", head, 2)); append(root, chunk("DATA", data, 2));
    return chunk("GAF_", root, 2);
}
}

int main() {
    vyruz::GafAnimation out;
    check(vyruz::decodeGafAnimation(fixture(), out), "synthetic animation rejected");
    check(out.frames.size() == 1 && out.sequences.size() == 1, "wrong table counts");
    const auto& f = out.frames.front();
    check(f.originX == -2 && f.originY == 5, "signed origin lost");
    check(f.flippedOriginX == -7 && f.flippedOriginY == 9, "authored flipped origin lost");
    check(f.image.width == 3 && f.image.height == 1 && f.image.rgba.size() == 12,
          "bitmap padding treated as pixels");
    check(f.image.rgba[0] == 255 && f.image.rgba[1] == 0 && f.image.rgba[2] == 0 &&
          f.image.rgba[3] == 255 && f.image.rgba[7] == 0 && f.image.rgba[11] == 255,
          "RGB555 or authored black color key lost");
    check(f.colorKey == 0 && f.bitmapFlags == 0x40000322, "bitmap metadata lost");
    const auto& s = out.sequences.front();
    check(s.flags == 7 && s.loopStart == 2 && s.subframes.size() == 2, "sequence metadata lost");
    check(s.subframes[0].frameIndex == 0 && s.subframes[0].offsetX == -3 &&
          s.subframes[0].offsetY == 4 && s.subframes[0].flags == 0x108 &&
          s.subframes[0].duration == 8, "pose metadata lost");
    check(s.subframes[1].frameIndex == vyruz::GafSubframe::Blank &&
          s.subframes[1].duration == 110, "timed blank rejected or remapped");
    check(!vyruz::decodeGafAnimation(fixture(0x12345678), out), "unknown pixel format accepted");
    check(!vyruz::decodeGafAnimation(fixture(0x40000322, 4), out), "short pixel payload accepted");
    check(!vyruz::decodeGafAnimation(fixture(0x40000322, 12), out), "oversized pixel payload accepted");
    check(!vyruz::decodeGafAnimation(fixture(0x40000322, 8, 2), out), "invalid bitmap index accepted");
    auto truncated = fixture(); truncated.pop_back();
    check(!vyruz::decodeGafAnimation(truncated, out), "truncated chunk accepted");
    check(out.frames.size() == 1 && out.sequences[0].subframes.size() == 2,
          "failed decode modified prior result");
    check(vyruz::decodeGafAnimation(fixture(0x40000122), out), "second observed pixel flag rejected");
    check(vyruz::decodeGafAnimation(fixture(0x311), out), "explosion bitmap variant rejected");
    check(out.frames[0].bitmapFlags == 0x311 && out.frames[0].image.rgba[0] == 255 &&
          out.frames[0].image.rgba[1] == 0 && out.frames[0].image.rgba[7] == 0,
          "explosion variant lost RGB555 or color key");
    std::cout << "gaf sprite tests passed\n";
    check(vyruz::decodeGafAnimation(fixture(0x111), out), "terrain groundlight bitmap variant rejected");
    check(out.frames[0].bitmapFlags==0x111 && out.frames[0].image.rgba[0]==255 && out.frames[0].image.rgba[7]==0,
          "terrain variant lost RGB555 or color key");
}
