#pragma once
#include "game/controls.h"
#include "game/motion_contact.h"
#include <algorithm>
#include <cmath>

namespace vyruz {
// 42cfa0 / 438390: Spider body poses count down and advance at most once
// per update, including the original asymmetric half-turn tie. The propulsion
// heading is independently floored to a cardinal sector by the caller.
inline void advanceSpiderBodyPose(int& pose,float& remaining,int desired,float ms,float timeout) {
    remaining-=ms;
    if(remaining>0)return;
    remaining=std::max(0.f,remaining+timeout);
    if(desired<0 || desired>=24 || pose==desired)return;
    const int delta=(desired-pose+24)%24;
    const bool positive=delta<12 || (delta==12 && pose>desired);
    pose=(pose+(positive?1:23))%24;
}
enum class EnemyThrottle { Neutral, Forward, Reverse };
enum class EnemyRouteMotion { None, Random, Patrol };
struct EnemyMotionConfig {
    float thrustMin=0,thrustMax=0,acceleration=0,deceleration=0;
    float speedThreshold=0,weight=1,grip=1;
    float frictionX=1,frictionZ=1,thresholdX=0,thresholdZ=0;
    bool inertia=true,launched=false,ignoreWeight=false;
};
struct EnemyMotionState {
    ContactVelocity velocity;
    float thrust=0,magnitude=0;
    bool impactLatch=false;
};
// Transient438b20 intermediates, not persisted actor state. Native X/Z have
// different spill points before addition to an actor's retained float velocity.
struct EnemyRouteCorrection { long double x=0,z=0; };
struct EnemyMotionCommand {
    int heading=0;
    EnemyThrottle throttle=EnemyThrottle::Neutral;
    float lateral=0;
    // Already transformed into the native solver's velocity correction.
    EnemyRouteCorrection routeCorrection;
    EnemyRouteMotion route=EnemyRouteMotion::None;
};

// 42cc60 -> 438b20. Inputs are native X/Z coordinates, not surface pixels.
// The absolute projection is intentional; there is no segment-end clamp.
inline EnemyRouteCorrection enemyRouteCorrection(ContactVelocity position,ContactVelocity start,
                                            ContactVelocity end,bool smallCorrection) {
    const ContactVelocity d{end.x-start.x,end.z-start.z};
    const ContactVelocity r{position.x-start.x,position.z-start.z};
    const float length2=static_cast<float>(static_cast<long double>(d.x)*d.x+
                                          static_cast<long double>(d.z)*d.z);
    if(!finiteContactVector(position) || !finiteContactVector(start) ||
       !finiteContactVector(end) || !(length2>0) || !std::isfinite(length2))return {};
    //42cc60 retains dot/projection and Z in x87, but spills projected X and
    //then corrected X separately. The two actor correction fields are float.
    const long double dot=static_cast<long double>(r.x)*d.x+static_cast<long double>(r.z)*d.z;
    const long double projection=std::sqrt((dot*dot/length2)/length2);
    const float projectedX=static_cast<float>(projection*d.x),x=projectedX-r.x;
    const long double z=projection*d.z-r.z;
    const float correctionX=static_cast<float>((static_cast<long double>(x)-z)*.008944272063672543f);
    const float correctionY=static_cast<float>((z+x)*.004472136031836271f);
    const long double first=static_cast<long double>(correctionX)*.015625f;
    const long double second=static_cast<long double>(correctionY)*.03125f;
    // PE0050952c is35.777088 (bd1b0f42), not the old transcription's1/180.
    //438b20 spills X before its family scale; Z stays extended until addition.
    const float outX=static_cast<float>((first+second)*35.7770881652832f);
    const long double outZ=(second-first)*35.7770881652832f;
    // Small-family X retains its product until addition; its Z product spills
    // at438fba. Ordinary Z remains extended until the addition at438fc6.
    EnemyRouteCorrection result{smallCorrection?static_cast<long double>(outX)*.01f:outX,
        smallCorrection?static_cast<float>(outZ*.01f):outZ};
    return std::isfinite(result.x)&&std::isfinite(result.z)?result:EnemyRouteCorrection{};
}

// 438b20's ordinary non-player branch. This does not perform navigation,
// turning, collision response, or the family-specific post-move velocity reset.
// Acceleration/friction are per native call; dt scales displacement/thresholds.
inline Displacement advanceEnemyMotion(EnemyMotionState& state,const EnemyMotionCommand& command,
                                      float seconds,const EnemyMotionConfig& config) {
    if(!(seconds>0) || !std::isfinite(seconds) || command.heading<0 || command.heading>=24 ||
       !finiteContactVector(state.velocity) || !std::isfinite(command.routeCorrection.x) ||
       !std::isfinite(command.routeCorrection.z))return {};
    for(float value:{state.thrust,state.magnitude,command.lateral,config.thrustMin,config.thrustMax,
                     config.acceleration,config.deceleration,config.speedThreshold,config.weight,
                     config.grip,config.frictionX,config.frictionZ,config.thresholdX,config.thresholdZ})
        if(!std::isfinite(value))return {};
    if(config.thrustMin>config.thrustMax || config.acceleration<0 || config.deceleration<0 ||
       config.grip<0 || config.frictionX<0 || config.frictionZ<0 ||
       config.thresholdX<0 || config.thresholdZ<0 || state.magnitude<0)return {};
    auto next=state;
    const bool retain=config.inertia || state.impactLatch;
    const float previousMagnitude=retain?state.magnitude:0;
    ContactVelocity velocity=retain?state.velocity:ContactVelocity{};
    if(config.inertia){velocity.x*=config.grip;velocity.z*=config.grip;}
    const auto throttle=state.impactLatch || (config.inertia && config.launched)?
        EnemyThrottle::Neutral:command.throttle;
    if(throttle==EnemyThrottle::Forward)
        next.thrust=std::min(config.thrustMax,next.thrust+config.acceleration);
    else if(throttle==EnemyThrottle::Reverse)
        next.thrust=std::max(config.thrustMin,next.thrust-config.deceleration*3);
    else if(next.thrust>config.deceleration)next.thrust-=config.deceleration;
    else if(next.thrust<-config.deceleration)next.thrust+=config.deceleration;
    else next.thrust=0;
    if(throttle!=EnemyThrottle::Neutral && previousMagnitude<config.speedThreshold*.1f && next.thrust!=0) {
        // Invalid mass safely declines propulsion; native shipped positive-mass
        // actors divide by Weight, Walker/Spider explicitly bypass it.
        if(!config.ignoreWeight && config.weight<=0)return {};
        const float mass=config.ignoreWeight?1:config.weight;
        const auto heading=nativeHeading(command.heading);
        velocity.x+=heading.x*next.thrust/mass;
        velocity.z+=heading.y*next.thrust/mass;
    }
    float lateral=command.lateral;
    if(lateral>=0)lateral=std::min(lateral,config.thrustMax);
    else lateral=std::max(lateral,config.thrustMin);
    if(lateral!=0) {
        const auto side=nativeHeading(command.heading-6);
        velocity.x+=side.x*lateral;velocity.z+=side.y*lateral;
    }
    if(command.route==EnemyRouteMotion::Random || command.route==EnemyRouteMotion::Patrol) {
        velocity.x=static_cast<float>(static_cast<long double>(velocity.x)+command.routeCorrection.x);
        velocity.z=static_cast<float>(static_cast<long double>(velocity.z)+command.routeCorrection.z);
    }
    velocity.x*=config.frictionX;velocity.z*=config.frictionZ;
    const float scale=seconds/.014f;
    if(std::abs(velocity.x)<=config.thresholdX*scale && std::abs(velocity.z)<=config.thresholdZ*scale)
        velocity={};
    next.magnitude=std::hypot(velocity.x,velocity.z);
    if(next.impactLatch && next.magnitude<1)next.impactLatch=false;
    next.velocity=velocity;
    const auto displacement=nativeToSurface(velocity.x*scale,velocity.z*scale);
    if(!std::isfinite(displacement.x) || !std::isfinite(displacement.y) || !std::isfinite(next.magnitude))return {};
    state=next;return displacement;
}
} // namespace vyruz
