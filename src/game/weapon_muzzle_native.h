#pragma once
#include "game/native_random.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vyruz {
// 443e1f..443e97. Native TYPE Weapon Start-distance is an integer. The zero
// branch draws454080 once per emitted barrel, using native actor bound spans.
// Record/playback flags replace zero with300 and consume no random value.
inline float nativeWeaponStartDistance(float authored,float spanX,float spanZ,
                                      std::uint32_t modeFlags,NativeRandomState& random) {
    if(!std::isfinite(authored) || authored < std::numeric_limits<int>::min() ||
       authored >= static_cast<double>(std::numeric_limits<int>::max())+1)return 0;
    const int distance=static_cast<int>(authored);
    if(distance)return static_cast<float>(distance);
    if(modeFlags&0x18000)return 300;
    // __ftol truncates after BOTH x87 multiplications, without a float spill
    // between .4f and10f. Invalid synthetic bounds have no native valid result
    // (the original would divide by zero); keep them inert without drawing RNG.
    const long double divisor=std::trunc(static_cast<long double>(std::min(spanX,spanZ))*.4f*10.f);
    if(!std::isfinite(divisor) || divisor<1 || divisor>std::numeric_limits<int>::max())return 0;
    return static_cast<float>(random.raw()%static_cast<std::uint32_t>(divisor));
}
}
