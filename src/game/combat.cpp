#include "game/combat.h"

#include <cmath>
#include <algorithm>

namespace vyruz {

ShotPositionOffset projectileStartOffset(int direction,float distance) {
    // Exact decimal float constants retained from owner PE at0050d440.
    constexpr ShotPositionOffset table[]={
        {-.0894427f,.0447214f},{-.0632456f,.0547723f},{-.0327383f,.0610905f},
        {0,.0632456f},{.0327383f,.0610905f},{.0632456f,.0547723f},
        {.0894427f,.0447214f},{.109545f,.0316228f},{.122181f,.0163692f},
        {.126491f,0},{.122181f,-.0163692f},{.109545f,-.0316228f},
        {.0894427f,-.0447214f},{.0632456f,-.0547723f},{.0327383f,-.0610905f},
        {0,-.0632456f},{-.0327383f,-.0610905f},{-.0632456f,-.0547723f},
        {-.0894427f,-.0447214f},{-.109545f,-.0316228f},{-.122181f,-.0163692f},
        {-.126491f,0},{-.122181f,.0163692f},{-.109545f,.0316228f}};
    if(direction<0 || direction>=24 || !std::isfinite(distance))return {};
    return {table[direction].x*distance,table[direction].y*distance};
}

ShotPositionOffset projectileBarrelOffset(int direction,float lateralOffset) {
    if(direction<0 || direction>=24 || !std::isfinite(lateralOffset))return {};
    const long double angle=direction*static_cast<long double>(-.2617993950843811f);
    const float cx=static_cast<float>(std::cos(angle)*lateralOffset);
    const long double sz=std::sin(angle)*lateralOffset;
    return {static_cast<float>((cx-sz)*.8944271802902222f),
            static_cast<float>((sz+cx)*.4472135901451111f)};
}

std::vector<ShotRequest> fireWeapon(const WeaponConfig& config, WeaponState& state,
                                  bool held, uint64_t nowMs, uint32_t& ammo,
                                  std::optional<bool> aiming,std::optional<uint32_t> activeAmmo,
                                  WeaponFireVisit visit) {
    // 438540 -> 42c210/42c280, before cooldown admission: slope changes
    // per held input tick, not per emitted bullet or elapsed milliseconds.
    // 440dd7/440f1b goes directly to443bd0 for a pending Circular sweep,
    // skipping438540 and its slope input helpers entirely.
    if(visit==WeaponFireVisit::Input && config.initialSlope!=0) {
        if(!aiming.value_or(held)){state.slopeHeld=false;state.slopeRadians=config.initialSlope;}
        else if(!state.slopeHeld){state.slopeHeld=true;state.slopeRadians=config.initialSlope;}
        else if(state.slopeRadians!=0)state.slopeRadians=std::min(0.f,state.slopeRadians+config.slopeStep);
    }
    std::vector<ShotRequest> shots;
    if (!held || ammo == 0 || !std::isfinite(config.weaponsGap) || config.weaponsGap < 0)
        return shots;
    //42d3df/4450b5: One-by-one bypasses the ordinary delay when the
    //emitter's raw +12c word is zero. Nonzero, including signed-negative
    //words, keeps the delay; it does not prohibit another volley outright.
    const bool bypassDelay=config.oneByOne && activeAmmo && *activeAmmo==0;
    if (!bypassDelay && state.lastFireMs && (nowMs < *state.lastFireMs ||
                            nowMs - *state.lastFireMs < config.delayMs))
        return shots;
    state.lastFireMs = nowMs;
    if (config.alternateSides) state.alternateLeft = !state.alternateLeft;
    const auto emit = [&](float offset) {
        if (ammo != 0) {
            shots.push_back({offset});
            --ammo;
        }
    };
    if (config.weaponsGap == 0) emit(0);
    else {
        const float offset = config.weaponsGap * 0.5f;
        if (config.leftSide && (!config.alternateSides || state.alternateLeft)) emit(offset);
        if (config.rightSide && (!config.alternateSides || !state.alternateLeft)) emit(-offset);
    }
    return shots;
}

ProjectileDisplacement advanceProjectileMotion(const ProjectileMotionConfig& c,
    ProjectileMotionState& s,float dx,float dz,float ms,float thrustMaximum) {
    if(thrustMaximum<0)thrustMaximum=c.speedMax;
    for (float v:{c.speedMax,c.acceleration,c.deceleration,c.weight,c.grip,c.frictionX,
                  c.frictionZ,c.thresholdX,c.thresholdZ,s.velocityX,s.velocityZ,
                  s.magnitude,s.thrust,dx,dz,ms,thrustMaximum})
        if (!std::isfinite(v)) return {};
    if (ms<=0 || ms>100 || c.weight<=0 || c.speedMax<0 || c.acceleration<0 ||
        c.deceleration<0 || c.grip<0 || c.frictionX<0 || c.frictionZ<0 ||
        c.thresholdX<0 || c.thresholdZ<0 || std::abs(std::hypot(dx,dz)-1)>0.001f)
        return {};
    auto next=s;
    float vx=c.inertia?s.velocityX*c.grip:0;
    float vz=c.inertia?s.velocityZ*c.grip:0;
    const float previousMagnitude=c.inertia?s.magnitude:0;
    if (c.inertia && c.launched) {
        if (next.thrust>c.deceleration) next.thrust-=c.deceleration;
        else if (next.thrust < -c.deceleration) next.thrust+=c.deceleration;
        else next.thrust=0;
    } else {
        next.thrust=std::min(thrustMaximum,next.thrust+c.acceleration);
        if (previousMagnitude<c.speedMax*.1f && next.thrust!=0) {
            vx+=dx*next.thrust*.1f/c.weight;
            vz+=dz*next.thrust*.1f/c.weight;
        }
    }
    vx*=c.frictionX;vz*=c.frictionZ;
    const float factor=ms/14;
    if (std::abs(vx)<=c.thresholdX*factor && std::abs(vz)<=c.thresholdZ*factor)
        vx=vz=0;
    next.velocityX=vx;next.velocityZ=vz;next.magnitude=std::hypot(vx,vz);
    const ProjectileDisplacement delta{vx*factor,vz*factor};
    if (!std::isfinite(next.magnitude) || !std::isfinite(next.thrust) ||
        !std::isfinite(delta.x) || !std::isfinite(delta.z)) return {};
    s=next;return delta;
}

float projectileBounceSignedAltitude(float magnitude,float age,float period) {
    if (!std::isfinite(magnitude) || !std::isfinite(age) || !std::isfinite(period) ||
        magnitude<0 || age<0 || period<0) return 0;
    // The executable loads a 32-bit 0.01 constant before extended precision
    // multiplication; retain that value rather than substituting double 0.01.
    const double phase=period==0?static_cast<double>(age)*static_cast<double>(.01f):static_cast<double>(age)/period;
    const float altitude=static_cast<float>(std::trunc(static_cast<double>(magnitude)*10*std::sin(phase)));
    return std::isfinite(altitude)?altitude:0;
}
float projectileBounceAltitude(float magnitude,float age,float period) {
    return std::abs(projectileBounceSignedAltitude(magnitude,age,period));
}

} // namespace vyruz
