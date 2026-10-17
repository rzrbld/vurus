#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace vyruz {
// 43cce2..43cd54, scalar portion only. Native Damage-zone is INTEGER (parser
// 0xa1); the retained decompilation's float argument annotation is misleading.
// Caller owns source/target centers, cell visitation, filters and hit ordering.
inline float nativeAmmoRadialDamage(float damage,float authoredRadius,long double squared) {
    if(!std::isfinite(damage) || !std::isfinite(authoredRadius) ||
       authoredRadius<1 || authoredRadius>46340 || !std::isfinite(squared) || squared<0)return 0;
    const int radius=static_cast<int>(authoredRadius);
    const int radiusSquared=radius*radius;
    // For these valid positive integer radii, this avoids __ftol overflow
    // without changing its subsequent strict integer-distance comparison.
    if(damage==0 || squared>=radiusSquared)return 0;
    const auto integerSquared=static_cast<std::int32_t>(std::trunc(squared));
    if(integerSquared==0)return damage;
    const auto distance=static_cast<int>(std::trunc(std::sqrt(static_cast<long double>(integerSquared))));
    // Native keeps division and multiplication extended until the final
    // argument float spill, rather than rounding (1-distance/radius) first.
    return static_cast<float>((static_cast<long double>(radius-distance)/radius)*damage);
}
}
