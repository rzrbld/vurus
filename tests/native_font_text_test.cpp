#include "assets/native_font_text.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool condition,const char* message) {
    if(!condition){std::cerr<<message<<'\n';std::exit(1);}
}
}
int main() {
    using namespace vyruz;
    constexpr std::array<uint8_t,32> nativeExpected={
        0x85,0xa0,0x83,0xe3,0x84,0xe5,0xe6,0x87,0x8a,0x82,0x88,0x89,0x8d,0xa1,0x8c,0x8b,
        0xf0,0xf1,0x95,0xa2,0x93,0xf5,0x94,0xf7,0xf8,0x97,0xa3,0x96,0x81,0xfd,0xfe,0xff};
    for(unsigned value=0;value<256;++value) {
        const auto parsed=nativeFontByte(static_cast<uint8_t>(value));
        if(value>=0xe0)require(parsed.kind==NativeFontByteKind::Glyph && parsed.glyph==nativeExpected[value-0xe0],"extended-byte native table mismatch");
        else if(value>=0x21)require(parsed.kind==NativeFontByteKind::Glyph && parsed.glyph==(value=='~'?'%':value),"ordinary unsigned glyph changed");
        else if(value=='\t')require(parsed.kind==NativeFontByteKind::Tab,"TAB dispatch");
        else if(value=='\n')require(parsed.kind==NativeFontByteKind::Newline,"LF dispatch");
        else if(value=='\r')require(parsed.kind==NativeFontByteKind::Ignore,"CR dispatch");
        else if(value==' ')require(parsed.kind==NativeFontByteKind::Space,"space dispatch");
        else require(parsed.kind==NativeFontByteKind::Stop,"unsupported control does not stop");
    }
    require(nativeFontTabX(0,6)==48 && nativeFontTabX(48,6)==96,"tab must advance at an existing stop");
    require(nativeFontTabX(17,6)==48 && nativeFontTabX(65,6)==96,"tab must use absolute cursor");
    require(nativeFontTabX(-1,6)==48 && nativeFontTabX(-49,6)==0,"signed native remainder boundary");
    require(nativeFontTabX(17,0)==17 && nativeFontTabX(17,-1)==17,"invalid metrics bounded");
    require(nativeFontTabX(std::numeric_limits<int64_t>::max(),6)==std::numeric_limits<int64_t>::max(),"overflow bounded");

    const std::string raw="caf\xe9";
    require(nativeFontInputBytes(raw)==raw,"authored raw Latin-1 byte changed");
    require(nativeFontInputBytes("caf\xc3\xa9")==raw,"UTF-8 accent not converted");
    require(nativeFontInputBytes("\xc3\x80\xc3\xbf")==std::string("\xc0\xff",2),"Latin-1 boundaries");
    require(nativeFontInputBytes("\xe2\x82\xac\xf0\x9f\x9a\x80")=="??","unsupported Unicode fallback");
    const std::string malformed="\xc0\xaf\xed\xa0\x80\xf4\x90\x80\x80\xe9\xc3";
    require(nativeFontInputBytes(malformed)==malformed,"malformed UTF-8 raw bytes changed");
    require(nativeFontInputBytes(std::string("A\0B\x12",4))==std::string("A\0B\x12",4),"bounded view/control preservation");
    const auto mapped=nativeFontByte(static_cast<uint8_t>(nativeFontInputBytes("\xc3\xa9")[0]));
    require(mapped.glyph==0x82,"UTF-8 adapter/native remap sequence");
    std::cout<<"native_font_text_test passed\n";
}
