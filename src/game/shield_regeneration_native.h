#pragma once
#include <algorithm>
#include <cstdint>

namespace vyruz {
// Original4413d0: this is a per-admitted-actor-pass step, not a rate.
inline bool nativeShieldRegenerationSubtype(unsigned subtype) {
    return ((std::uint32_t{1}<<((subtype-0x10u)&31u))&0x1ff40u)!=0;
}
inline float nativeRegenerateShield(float shield,float step,float maximum) {
    if(step==0)return shield;
    // x87 compares the unrounded sum after storing its float result.
    const double sum=double(shield)+double(step);
    return sum>double(maximum)?maximum:static_cast<float>(sum);
}
inline float nativeReleaseShieldSurprise(float step) {
    return std::max(0.f,step-10.f);
}
}
