#pragma once
#include <cmath>
#include <cstdint>
namespace vyruz {
// Original4ffaf8 uses FISTP int64 and callers retain only EAX. Masked x87
// invalid (NaN/infinity/out-of-int64) produces INT64_MIN: its low word is zero.
inline uint32_t nativeImpactCoordinateWord(float value) {
    const long double n=value;
    if(!std::isfinite(value)||n< -9223372036854775808.L||n>=9223372036854775808.L)return 0;
    return static_cast<uint32_t>(static_cast<int64_t>(std::trunc(n)));
}
//43dc40 tests only x87 C3, hence unordered is admitted as equality. It still
//searches just the cell selected from the unclamped coordinate words.
inline bool nativeImpactCoordinateEqual(float a,float b) {
    return std::isnan(a)||std::isnan(b)||a==b;
}
//43d350 tests C0 for below-zero before its upper bound: NaN clamps to zero.
inline float nativeImpactFxClamp(float value,float extent) {
    if(std::isnan(value)||value<0)return 0;
    if(value>=extent)return extent-1;
    return value;
}
}
