#pragma once
#include "game/ammo_collision_native.h"
#include "game/actor_ammo_contact_native.h"

namespace vyruz {
inline constexpr uint64_t NativeContactDecorBit=uint64_t(1)<<63;
inline constexpr uint64_t NativeContactProjectileBit=uint64_t(1)<<62;
inline constexpr uint64_t NativeContactDisabledProjectileBit=uint64_t(1)<<61;
inline constexpr uint64_t NativeContactProjectileNodeMask=NativeContactDisabledProjectileBit-1;

// One 4105d0 winner, before the caller's Item/player/water exclusions. A caller
// must consume this winner for every contact kind: an independent Ammo sweep
// after an ordinary collision does not preserve Item priority or wall blocking.
struct NativeBodyContactQuery {
    bool used=false,unsupportedShape=false,callerIgnored=false,hasAmmoCandidate=false;
    std::optional<NativeAmmoSelectedContact> selected;
    std::optional<NativeAmmoShapeTarget> selectedShape;
    std::optional<size_t> closestNonContactNode;
    // A transient lookup result, not a persistent ID. Copy the projectile
    // before invoking damage/FX/CREATE paths which may grow/compact its vector.
    std::optional<size_t> projectile;
};
}
