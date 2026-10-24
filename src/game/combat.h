#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace vyruz {

// Ordinary TYPE Weapon cadence and barrel selection; see formats/combat.md.
// All times are milliseconds. Lateral offsets are native world-space units,
// before the 2:1 projection. This module does not implement projectile physics.
struct WeaponConfig {
    uint32_t delayMs = 0;
    float weaponsGap = 0;
    bool leftSide = false;
    bool rightSide = false;
    bool alternateSides = false;
    float initialSlope=0,slopeStep=0; // radians; authored, rebuilt on restore
    bool oneByOne=false; // TYPE Weapon bit8; class-derived, rebuilt on restore
};
struct WeaponState {
    std::optional<uint64_t> lastFireMs;
    bool alternateLeft = false;
    float slopeRadians=0;
    bool slopeHeld=false;
};
struct ShotRequest {
    float lateralOffset = 0;
};
enum class WeaponFireVisit { Input, PendingSweep, AimAlreadyUpdated };

// 42c210/42c280: one slope visit, independent of shot-delay admission.
// Spider invokes these from steering; a rescan without steering retains state.
void advanceWeaponAim(const WeaponConfig&,WeaponState&,bool aiming);

// One volley per call at most, without accumulating shots during long frames.
// A zero gap means one center barrel regardless of side flags. A nonzero gap
// enables up to two authored barrels. Each emitted request consumes one round.
// Release keeps cooldown; the caller may reset state when selecting a weapon.
// Invalid negative/nonfinite gap or a clock moving backwards produces no shot.
// Pending Circular visits bypass native input processing. AimAlreadyUpdated
// emits after an AI steering visit without advancing or resetting slope again.
std::vector<ShotRequest> fireWeapon(const WeaponConfig& config, WeaponState& state,
                                  bool held, uint64_t nowMs, uint32_t& ammo,
                                  std::optional<bool> aiming={},
                                  std::optional<uint32_t> activeAmmo={},
                                  WeaponFireVisit visit=WeaponFireVisit::Input);

struct ShotPositionOffset { float x=0,y=0; };
// 443e97: explicit TYPE Weapon Start-distance times PE surface table50d440.
// The table already includes the native scale; distance is not surface pixels.
// Zero leaves placement unchanged; the separate native randomized-zero branch
// needs the shooter's rendered bounds and is not represented by this helper.
ShotPositionOffset projectileStartOffset(int direction,float distance);
// 443d0a: positive lateralOffset is the original Left barrel. Player and
// enemy emitters share this transform, retaining native barrel order.
ShotPositionOffset projectileBarrelOffset(int direction,float lateralOffset);

// Confirmed ordinary horizontal solver (438b20). Velocities are internal world
// displacement per native 14 ms unit; direction must be an internal unit vector.
// Acceleration/deceleration are per simulation tick, as in the native solver.
// Collision response, tracking and vertical bounce remain caller responsibilities.
struct ProjectileMotionConfig {
    float speedMax=0, acceleration=0, deceleration=0, weight=1, grip=1;
    float frictionX=.97f, frictionZ=.97f, thresholdX=.1f, thresholdZ=.1f;
    bool inertia=false, launched=false;
};
struct ProjectileMotionState {
    float velocityX=0, velocityZ=0, magnitude=0, thrust=0;
};
struct ProjectileDisplacement { float x=0,z=0; };
// thrustMaximum supplies authored Thrust.Max separately from Speed-max. A
// negative value preserves the legacy adapter for other ammunition callers.
// Invalid/nonfinite inputs leave state unchanged and return zero displacement.
ProjectileDisplacement advanceProjectileMotion(const ProjectileMotionConfig&,
    ProjectileMotionState&,float directionX,float directionZ,float milliseconds,float thrustMaximum=-1);
// 441566: vertical-bounce dry-ground branch, not gravitational acceleration.
// Negative halfwaves are reflected on dry ground; caller handles water removal.
float projectileBounceAltitude(float nativeMagnitude,float ageMs,float period=0);
// Signed, integer-truncated native result. Test <0 for the water branch before
// reflecting it; a negative fractional result truncating to zero is not a splash.
float projectileBounceSignedAltitude(float nativeMagnitude,float ageMs,float period=0);

} // namespace vyruz
