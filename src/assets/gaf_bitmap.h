#pragma once
#include "assets/tga.h"
#include <array>
#include <cstddef>

namespace vyruz {
// Lossless storage of the original little-endian RGB555 samples. Bit15 is
// preserved: color-key comparison uses the complete stored word, not RGB bits.
struct GafBitmap {
    int width=0,height=0;
    std::vector<uint16_t> rgb555;
    uint16_t colorKey=0x7c1f;

    bool valid()const{return width>0&&height>0&&rgb555.size()==size_t(width)*size_t(height);}
    std::array<uint8_t,4> pixel(size_t index)const{
        const auto v=rgb555[index];
        return {uint8_t(((v>>10)&31)*255/31),uint8_t(((v>>5)&31)*255/31),
                uint8_t((v&31)*255/31),uint8_t(v==colorKey?0:255)};
    }
    bool expand(std::vector<uint8_t>& rgba)const{
        if(!valid()){rgba.clear();return false;}
        rgba.resize(rgb555.size()*4);
        for(size_t i=0;i<rgb555.size();++i){const auto p=pixel(i);for(size_t c=0;c<4;++c)rgba[i*4+c]=p[c];}
        return true;
    }
    Image rgbaImage()const{
        Image out;if(expand(out.rgba)){out.width=width;out.height=height;}return out;
    }
};
}
