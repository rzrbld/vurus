#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace vyruz {
enum class NativeFontByteKind { Glyph, Space, Tab, Newline, Ignore, Stop };
struct NativeFontByte {
    NativeFontByteKind kind=NativeFontByteKind::Stop;
    uint8_t glyph=0;
};

// 44bf1f..44bfc5: the executable consumes unsigned single-byte characters.
constexpr NativeFontByte nativeFontByte(uint8_t byte) {
    constexpr std::array<uint8_t,32> remap={
        0x85,0xa0,0x83,0,0x84,0,0,0x87,0x8a,0x82,0x88,0x89,0x8d,0xa1,0x8c,0x8b,
        0,0,0x95,0xa2,0x93,0,0x94,0,0,0x97,0xa3,0x96,0x81,0,0,0};
    if(byte>=0xe0 && remap[byte-0xe0])byte=remap[byte-0xe0];
    switch(byte) {
    case '\t':return {NativeFontByteKind::Tab,0};
    case '\n':return {NativeFontByteKind::Newline,0};
    case '\r':return {NativeFontByteKind::Ignore,0};
    case ' ':return {NativeFontByteKind::Space,0};
    case '~':return {NativeFontByteKind::Glyph,'%'};
    default:return byte<0x20 ? NativeFontByte{} : NativeFontByte{NativeFontByteKind::Glyph,byte};
    }
}

// 44bf77..44bf8c uses signed division of the absolute cursor, not row-relative X.
// Native authored metrics are positive. Guard malformed metrics/adapter overflow.
constexpr int64_t nativeFontTabX(int64_t absoluteX,int32_t fixedAdvance) {
    if(fixedAdvance<=0)return absoluteX;
    const int64_t unit=int64_t(fixedAdvance)*8;
    const int64_t advance=unit-absoluteX%unit;
    if(absoluteX>std::numeric_limits<int64_t>::max()-advance)
        return std::numeric_limits<int64_t>::max();
    return absoluteX+advance;
}

// Linux UI adapter only: valid UTF-8 becomes Latin-1 bytes before native parsing.
// Invalid UTF-8 bytes remain untouched so original authored byte strings survive.
inline std::string nativeFontInputBytes(std::string_view text) {
    std::string out;out.reserve(text.size());
    for(size_t i=0;i<text.size();) {
        const auto lead=static_cast<uint8_t>(text[i]);
        unsigned length=0;uint32_t scalar=0,minimum=0;
        if(lead>=0xc2 && lead<=0xdf){length=2;scalar=lead&0x1f;minimum=0x80;}
        else if(lead>=0xe0 && lead<=0xef){length=3;scalar=lead&0xf;minimum=0x800;}
        else if(lead>=0xf0 && lead<=0xf4){length=4;scalar=lead&7;minimum=0x10000;}
        bool valid=length && length<=text.size()-i;
        for(unsigned j=1;valid && j<length;++j) {
            const auto next=static_cast<uint8_t>(text[i+j]);
            if((next&0xc0)!=0x80)valid=false;
            else scalar=(scalar<<6)|(next&0x3f);
        }
        valid=valid && scalar>=minimum && scalar<=0x10ffff && !(scalar>=0xd800 && scalar<=0xdfff);
        if(valid){out+=scalar<=0xff?static_cast<char>(scalar):'?';i+=length;}
        else {out+=text[i];++i;}
    }
    return out;
}
} // namespace vyruz
