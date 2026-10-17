#include "assets/gaf_font.h"

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
Bytes fixture(uint32_t bitmap = 2,uint32_t bytes=72,uint32_t flags=1) {
    Bytes header(36); put(header,0,1); put(header,4,bytes); put(header,16,24);
    Bytes desc(bytes); put(desc,0,256); put(desc,4,flags); put(desc,8,3);
    put(desc,12,uint32_t(-2)); put(desc,24,8); put(desc,28,10);
    put(desc,32,0xcccccccc); // uninitialized style is legal when flags&2 is clear
    Bytes glyphs(256*24); const size_t zero=(48-1)*24;
    put(glyphs,zero,1); put(glyphs,zero+4,uint32_t(-4)); put(glyphs,zero+8,5);
    put(glyphs,zero+12,bitmap); put(glyphs,zero+16,9);
    Bytes root; append(root,chunk("HEAD",chunk("FNT_",header),1));
    append(root,chunk("DATA",chunk("FNT_",streams({desc,{},glyphs,{}})),1));
    return chunk("GAF_",root,2);
}
}
int main() {
    std::vector<vyruz::GafFont> fonts;
    check(vyruz::decodeGafFonts(fixture(),2,fonts),"font rejected");
    check(fonts.size()==1 && fonts[0].records.size()==256,"font count");
    const auto& f=fonts[0]; const auto* g=f.glyph(uint8_t('0'));
    check(g && g->layers[0].frameIndex==0 && g->layers[1].frameIndex==1,"one-based glyph/bitmap mapping");
    check(g->layers[0].offsetX==-4 && g->layers[0].offsetY==5,"signed offsets");
    check(!f.glyph(0) && f.glyph(255),"unsigned byte lookup");
    check(f.glyph('1')->layers[0].frameIndex==vyruz::GafGlyphLayer::Blank,"blank");
    check(f.advance(12)==10 && f.advance(0)==8 && f.lineAdvance()==13,"proportional metrics");
    check(!f.hasAuthoredStyle() && f.words[8]==0xcccccccc,"raw style preserved");
    auto fixed=f; fixed.words[1]=2;
    check(fixed.advance(12)==8 && fixed.lineAdvance()==10,"fixed metrics");
    check(!vyruz::decodeGafFonts(fixture(3),2,fonts) && fonts[0].glyph('0')->layers[1].frameIndex==1,"invalid secondary reference atomicity");
    auto truncated=fixture(); truncated.pop_back();
    check(!vyruz::decodeGafFonts(truncated,2,fonts),"truncated bank accepted");
    check(vyruz::decodeGafFonts(fixture(2,64),2,fonts) && fonts[0].advance(12)==10,"legacy inventory metrics");
    check(fonts[0].words[16]==0 && fonts[0].words[17]==0,"missing legacy style offsets normalized");
    check(!vyruz::decodeGafFonts(fixture(2,64,3),2,fonts),"untraced legacy authored styles rejected");
    std::cout<<"gaf_font_test passed\n";
}
