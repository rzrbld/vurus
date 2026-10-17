#pragma once
#include <bit>
#include <cstdint>

namespace vyruz {
// 436690 inputs: integer surface coordinates, projected camera offsets and
// display dimensions. Context800 replaces the rectangle with player +/- half
// the level's surface extent; it does not use half the display dimensions.
struct NativeEffectCamera {
    int32_t offsetX=0,offsetY=0,viewWidth=640,viewHeight=480;
    uint32_t contextFlags=0,modeFlags=0;
    int32_t surfaceWidth=0,surfaceHeight=0;
    float playerX=0,playerY=0;

    bool admits(int32_t x,int32_t y) const {
        if(contextFlags&0x800) {
            const long double halfX=surfaceWidth>>1,halfY=surfaceHeight>>1;
            return static_cast<long double>(playerX)-halfX<=x &&
                   x<=static_cast<long double>(playerX)+halfX &&
                   static_cast<long double>(playerY)-halfY<=y &&
                   y<=static_cast<long double>(playerY)+halfY;
        }
        const auto add=[](int32_t a,int32_t b){
            return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));
        };
        const int32_t marginX=modeFlags&0x18000?64:640;
        const int32_t marginY=modeFlags&0x18000?64:480;
        const int32_t qx=add(x,offsetX),qy=add(y,offsetY);
        return qx>=-marginX && qx<add(viewWidth,marginX) &&
               qy>=-marginY && qy<add(viewHeight,marginY);
    }
};
}
