#pragma once
#include "game/combat.h"
#include <cstdint>
namespace vyruz {
// 42d3df..42d409: the selected barrel starts with timestamp zero. The
// original uses unsigned World-clock subtraction, not actor age. Only the
// literal zero raw active-Ammo word bypasses Delay for One-by-one weapons.
// This is enemy AI admission; keep the Player's existing first-input policy.
inline bool nativeEnemyWeaponReady(const WeaponConfig& config,const WeaponState& state,
                                  uint64_t worldMs,uint32_t activeAmmo) {
    if(config.oneByOne && activeAmmo==0)return true;
    const auto elapsed=static_cast<uint32_t>(worldMs-state.lastFireMs.value_or(0));
    return elapsed>=config.delayMs;
}
}
