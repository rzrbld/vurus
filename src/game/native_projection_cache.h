#pragma once
#include "game/controls.h"
#include <bit>
#include <cmath>
#include <cstdint>

namespace vyruz {
// Derived presentation-independent coordinates; never part of a save. Keys
// retain signed zero and sub-pixel changes instead of comparing rounded cells.
class NativeProjectionCache {
    uint32_t xBits_=0,yBits_=0;
    Displacement projected_;
    bool valid_=false;
public:
    template<class Project>
    Displacement resolve(float x,float y,Project project) {
        // Invalid input must retain the uncached arithmetic's behavior, without
        // populating a reusable entry with nonfinite coordinates.
        if(!std::isfinite(x)||!std::isfinite(y))return project(x,y);
        const auto xb=std::bit_cast<uint32_t>(x),yb=std::bit_cast<uint32_t>(y);
        if(!valid_||xb!=xBits_||yb!=yBits_) {
            projected_=project(x,y);xBits_=xb;yBits_=yb;valid_=true;
        }
        return projected_;
    }
};
}
