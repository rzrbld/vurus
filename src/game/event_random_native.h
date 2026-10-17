#pragma once
#include "game/native_random.h"
#include "game/surprise.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace vyruz {
// 43809c..4380d9. Consume a draw even for certain/impossible entries.
// Normal play uses the active global stream.438096 skips RANDOM entries
// entirely under replay/demo flags18000; the caller must enforce that gate.
inline bool nativeDeathRandomDrop(NativeRandomState& random,int numerator,int denominator) {
    return random.inclusive(1,denominator)<=numerator;
}
// 41ba00 normal active-global context, or World+2590 in replay/demo.
inline SurpriseKind nativeRandomSurprise(NativeRandomState& random) {
    return surpriseRandom(static_cast<unsigned>(random.inclusive(0,135)));
}
// 43d100 preserves the old offset and consumes nothing while523820 is set.
// Trap and Destroy call the two draws directly, after their other work.
inline std::array<int,2> nativeRandomShake(NativeRandomState& random,int amplitude,
                                         std::array<int,2> previous={},bool disabled=false) {
    if(disabled)return previous;
    const int x=random.inclusive(-amplitude,amplitude);
    const int y=random.inclusive(-amplitude,amplitude);
    return {x,y};
}
// 43ce8c..43cedc: radial player damage uses World+259c, independently of
// whether43ca30 applied health. Nonplayer/direct hits consume no shake draws.
// The authored positive finite damage range fits native signed __ftol.
inline std::array<int,2> nativeRadialDamageShake(NativeRandomState& worldRandom,
        float amount,bool targetIsPlayer,std::array<int,2> previous={},bool disabled=false) {
    if(!targetIsPlayer || !std::isfinite(amount) || amount<200)return previous;
    // Clamp before converting to avoid overflowing C++ conversion for stress
    // inputs. For all amounts >=2400 the original's final clamp is32.
    const int amplitude=amount>=2400?32:static_cast<int>(std::trunc(amount))/100+8;
    return nativeRandomShake(worldRandom,amplitude,previous,disabled);
}
}
