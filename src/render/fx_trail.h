#pragma once
#include "game/content.h"
#include <cmath>
#include <cstdint>
namespace vyruz {
// Native 433026..4330dc repeats the authored FX pose along a surface segment.
// The previous endpoint is already integer-truncated by CREATE at437d0e.
template<class Emit>void visitFxTrail(ContentPosition previous,ContentPosition current,float step,const Emit& emit) {
    if(!std::isfinite(step)||step<=0)return;
    const double dx=double(current.x)-previous.x,dy=double(current.y)-previous.y;
    const double count=std::trunc(std::hypot(dx,dy)/step);
    if(!std::isfinite(count)||count<=0||count>100000)return;
    const auto n=static_cast<uint32_t>(count);
    const float incrementX=static_cast<float>(dx/n),incrementY=static_cast<float>(dy/n);
    for(uint32_t i=n;i>0;--i)
        emit(ContentPosition{previous.x+static_cast<float>(std::trunc(double(incrementX)*i)),
                             previous.y+static_cast<float>(std::trunc(double(incrementY)*i))});
}
}
