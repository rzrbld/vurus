#pragma once
#include <algorithm>
#include <cmath>

namespace vyruz {
// Port UI extension for long selected save labels; independent of World time.
inline float saveLabelScrollOffset(double elapsedMs,float textWidth,float viewportWidth) {
    const double overflow=std::max(0.f,textWidth-viewportWidth);
    if(overflow==0)return 0;
    constexpr double pauseMs=1000,speed=.04; // 40 logical pixels per second.
    const double travel=overflow/speed;
    double phase=std::fmod(std::max(0.0,elapsedMs),2*(pauseMs+travel));
    if(phase<pauseMs)return 0;
    phase-=pauseMs;if(phase<travel)return float(phase*speed);
    phase-=travel;if(phase<pauseMs)return float(overflow);
    return float(std::max(0.0,overflow-(phase-pauseMs)*speed));
}
}
