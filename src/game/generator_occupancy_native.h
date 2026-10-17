#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <algorithm>
#include <cmath>
namespace vyruz {
// Axis-aligned kind3 square constructor: duplicated corner coordinates remain
// identical under translation. Six floats preserve its query rounding history.
struct NativeGeneratorSquareState {
    float centerX=0,centerZ=0,minX=0,minZ=0,maxX=0,maxZ=0;
};
inline bool validNativeGeneratorSquare(const NativeGeneratorSquareState& s) {
    return std::isfinite(s.centerX)&&std::isfinite(s.centerZ)&&
        std::isfinite(s.minX)&&std::isfinite(s.minZ)&&std::isfinite(s.maxX)&&std::isfinite(s.maxZ)&&
        s.minX<=s.maxX&&s.minZ<=s.maxZ;
}
inline NativeGeneratorSquareState makeNativeGeneratorSquare(float x,float y,std::int32_t classSize) {
    const long double xx=static_cast<long double>(x)*.015625f,yy=static_cast<long double>(y)*.03125f;
    const float nx=static_cast<float>((xx+yy)*35.7770881652832f);
    const float nz=static_cast<float>((yy-xx)*35.7770881652832f);
    //41a740 stores half-size before every corner subtraction/addition.
    const float h=static_cast<float>(static_cast<long double>(classSize)*.7071067690849304f);
    return {nx,nz,static_cast<float>(static_cast<long double>(nx)-h),
        static_cast<float>(static_cast<long double>(nz)-h),
        static_cast<float>(static_cast<long double>(h)+nx),
        static_cast<float>(static_cast<long double>(h)+nz)};
}
inline void refreshNativeGeneratorSquare(NativeGeneratorSquareState& s,float x,float y) {
    const long double xx=static_cast<long double>(x)*.015625f,yy=static_cast<long double>(y)*.03125f;
    const float nx=static_cast<float>((yy+xx)*35.7770881652832f);
    const long double nz=(yy-xx)*35.7770881652832f;
    const float dx=static_cast<float>(static_cast<long double>(nx)-s.centerX);
    const float dz=static_cast<float>(nz-s.centerZ);
    s.minX=static_cast<float>(static_cast<long double>(dx)+s.minX);
    s.maxX=static_cast<float>(static_cast<long double>(dx)+s.maxX);
    s.minZ=static_cast<float>(static_cast<long double>(dz)+s.minZ);
    s.maxZ=static_cast<float>(static_cast<long double>(dz)+s.maxZ);
    s.centerX=nx;s.centerZ=static_cast<float>(nz);
}
// Current native BBOX at actor+1c is17 DWORDs. The last5 words are guard storage
// used by the independent full-record probe; no algorithm reads/writes them.
// Preserve unused/tag/sample words; callers configure subtype/class/phase flags.
struct NativeGeneratorBounds {
    std::array<std::uint32_t,22> words{};
    float value(unsigned n)const{return std::bit_cast<float>(words[n]);}
    void value(unsigned n,float f){words[n]=std::bit_cast<std::uint32_t>(f);}
};
inline bool nativeGeneratorCandidate(std::uint32_t kind,std::uint32_t bboxFlags,
                                     std::uint32_t subtype,bool playerOnlyItemType) {
    return kind==3 && !(bboxFlags&1) && !(subtype==0x12 && playerOnlyItemType);
}
// Actual41b4b0: surface inputs are floats (FLD), not truncated integer cells.
// X is stored before polygon delta. Z is stored without popping, so its
// unrounded product is retained through delta-Z's separate float spill.
inline void refreshNativeGeneratorBounds(NativeGeneratorBounds& b,float x,float y) {
    constexpr float sx=.015625f,sy=.03125f,scale=35.7770881652832f;
    const float previousX=b.value(9),previousZ=b.value(10);
    const long double yy=static_cast<long double>(y)*sy;
    const long double xx=static_cast<long double>(x)*sx;
    const float nx=static_cast<float>((yy+xx)*scale);
    const long double nz=(yy-xx)*scale;
    b.value(9,nx);b.value(10,static_cast<float>(nz));
    const unsigned shape=b.words[0];
    if(shape==3||shape==4){
        const float dx=static_cast<float>(static_cast<long double>(nx)-previousX);
        const float dz=static_cast<float>(nz-previousZ);
        for(unsigned n=0;n<(shape==3?2u:4u);++n){
            b.value(1+2*n,static_cast<float>(static_cast<long double>(dx)+b.value(1+2*n)));
            b.value(2+2*n,static_cast<float>(static_cast<long double>(dz)+b.value(2+2*n)));
        }
    }
    //405cb0 uses already refreshed point words for line/polygon and separately
    // stores four extrema. Circle radius is a float in word1.
    if(shape==1||shape==2){b.value(11,b.value(9));b.value(12,b.value(10));b.value(13,b.value(9));b.value(14,b.value(10));}
    else if(shape==5){
        b.value(11,static_cast<float>(static_cast<long double>(b.value(9))-b.value(1)));
        b.value(12,static_cast<float>(static_cast<long double>(b.value(10))-b.value(1)));
        b.value(13,static_cast<float>(static_cast<long double>(b.value(1))+b.value(9)));
        b.value(14,static_cast<float>(static_cast<long double>(b.value(10))+b.value(1)));
    }else if(shape==3||shape==4){
        float loX=999999,loZ=999999,hiX=-999999,hiZ=-999999;
        for(unsigned n=0;n<(shape==3?2u:4u);++n){const float px=b.value(1+2*n),pz=b.value(2+2*n);loX=std::min(loX,px);loZ=std::min(loZ,pz);hiX=std::max(hiX,px);hiZ=std::max(hiZ,pz);}
        b.value(11,loX);b.value(12,loZ);b.value(13,hiX);b.value(14,hiZ);
    }
}
inline bool nativeGeneratorBoundsOverlap(const NativeGeneratorBounds&a,const NativeGeneratorBounds&b){
    return a.value(11)<b.value(13)&&b.value(11)<a.value(13)&&a.value(12)<b.value(14)&&b.value(12)<a.value(14);
}
}
