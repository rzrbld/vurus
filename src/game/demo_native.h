#pragma once
#include "game/controls.h"
namespace vyruz {
//438989..4389cf: Y/32 explicitly spills to float; X/64 remains x87.
inline Displacement nativeDemoRecordedVelocity(float x,float y,float previousX,float previousY) {
    constexpr float scale=35.777088165283203125f;
    const long double sx=(static_cast<long double>(x)-previousX)*.015625L;
    const float sy=static_cast<float>((static_cast<long double>(y)-previousY)*.03125L);
    return {static_cast<float>((sx+sy)*scale),static_cast<float>((sy-sx)*scale)};
}
}
