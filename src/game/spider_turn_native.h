#pragma once
#include "game/enemy_turn_native.h"
#include <cassert>

namespace vyruz {
// Spider uses the same native actor pose/countdown fields as ordinary enemies,
// but its idle controller is 443990 (a catch-up loop), not the AI single-step
// controller 42cfa0. Keep visible body pose and the heading-ring node separate.
inline EnemyTurnState initializeSpiderTurn(const EnemyTurnConfig& config,int heading) {
    assert(config.subtype==0x1f && !config.twoPieces);
    // 4178a0 floors the visible pose to a cardinal bank while 4198f0 retains
    // the authored logical heading. The unused one-piece gun fields stay valid.
    return initializeEnemyTurn(config,heading,0);
}

inline void beginSpiderTurnUpdate(EnemyTurnState& state) {
    state.turnCommand=0; // 440fe1: exactly once, before seeker/base AI dispatch.
}

inline void advanceSpiderRouteTurn(const EnemyTurnConfig& config,EnemyTurnState& state,
        int desiredHeading,int elapsedMs,float cachedMagnitude,float authoredSpeedMax) {
    assert(config.subtype==0x1f && !config.twoPieces);
    if(!config.canTurnOnPlace) {
        if(cachedMagnitude<1.f)return;
        if(config.authoredBodyPeriodMs) {
            state.bodyPeriodMs=static_cast<int>((1.L-static_cast<long double>(cachedMagnitude)/
                static_cast<long double>(authoredSpeedMax))*config.authoredBodyPeriodMs);
            if(state.bodyRemainingMs>state.bodyPeriodMs)state.bodyRemainingMs=state.bodyPeriodMs;
        }
    }
    state.bodyRemainingMs-=elapsedMs;
    if(state.bodyRemainingMs>0)return;
    state.bodyRemainingMs+=state.bodyPeriodMs;
    if(state.bodyRemainingMs<0)state.bodyRemainingMs=0;
    int snapped=((desiredHeading+3)/6)*6;
    if(snapped>=24)snapped%=24;
    state.bodyDesiredPose=snapped+1;
    stepEnemyTurnPose(config,state,false,true);
    // 42d0f4 uses SIGNED division for Spider, unlike other actor families.
    state.logicalHeading=((state.bodyPose-1)/6)*6;
    state.orthogonalHeading=(state.logicalHeading+3)%24;
    state.turnCommand=enemyNativeTurnCommand(0x1f,state.bodyPose,state.bodyDesiredPose);
    // 42d220 then seeks actor+188 to this logical heading. No separate node
    // allocation is needed when the port represents the circular node by value.
}

struct SpiderIdleTurnAdmission {
    bool seekerOrTracker=false;
    float currentX=0,currentZ=0,previousX=0,previousZ=0;
    int movementCommand=0;
};
inline bool spiderIdleTurnAdmitted(const EnemyTurnConfig& config,const EnemyTurnState& state,
                                  const SpiderIdleTurnAdmission& input) {
    // 441314..44135f. These are actual position/history comparisons, not speed,
    // route activity or thrust tests. Save validation admits finite coordinates.
    return config.canTurnOnPlace && input.seekerOrTracker &&
        input.currentX==input.previousX && input.currentZ==input.previousZ &&
        input.movementCommand==0 && state.turnCommand==0;
}

inline bool spiderTurnAllowsForward(const EnemyTurnState& state) {
    return (state.bodyPose-1)%6==0; // 42ccf0: visible cardinal pose, not ring node.
}
inline bool spiderTurnAllowsFire(const EnemyTurnState& state,int targetHeading) {
    // Turning/weapon cadence and terminal-route admission are separate gates.
    return state.turnCommand==0 && state.logicalHeading==targetHeading &&
        state.bodyPose-1==state.logicalHeading; // 42d2d0 Spider-specific final gate.
}

inline void advanceSpiderIdleBodyController(const EnemyTurnConfig& config,EnemyTurnState& state,
                                            int elapsedMs) {
    assert(config.subtype==0x1f && !config.twoPieces);
    // Shipped Spider turn periods are positive. A zero period with a negative
    // countdown does not terminate in the native controller either.
    assert(state.bodyPeriodMs>0);
    state.bodyRemainingMs-=elapsedMs;
    if(state.bodyRemainingMs>0)return;
    do {
        state.bodyRemainingMs+=state.bodyPeriodMs;
        if(state.bodyPose!=state.bodyDesiredPose) {
            stepEnemyTurnPose(config,state,false,false);
            if(state.bodyPose==state.bodyDesiredPose && state.turnCommand==0)return;
            if(state.bodyRemainingMs>=0)return;
        }
        if(state.turnCommand!=0 && state.bodyPose==state.bodyDesiredPose) {
            state.logicalHeading=(state.logicalHeading+(state.turnCommand==0xcb?6:18))%24;
            state.bodyDesiredPose=state.logicalHeading+1;
            state.orthogonalHeading=(state.logicalHeading+3)%24;
        }
    } while(state.bodyRemainingMs<0);
}

template<class InclusiveRandom>
inline void advanceSpiderIdleTurn(const EnemyTurnConfig& config,EnemyTurnState& state,
                                 int elapsedMs,InclusiveRandom&& random) {
    assert(config.subtype==0x1f && !config.twoPieces);
    // 444aee..444b74. Caller enters the original world RNG context for these
    // draws; this helper neither seeds nor substitutes a per-actor generator.
    if(state.bodyPose==state.bodyDesiredPose && random(1,150)==75)
        state.bodyDesiredPose=random(0,23)+1;
    if(state.bodyRemainingMs<0)stepEnemyTurnPose(config,state,false,true);
    state.turnCommand=enemyNativeTurnCommand(0x1f,state.bodyPose,state.bodyDesiredPose);
    advanceSpiderIdleBodyController(config,state,elapsedMs);
}
}
