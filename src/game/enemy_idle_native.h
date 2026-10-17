#pragma once
#include "game/enemy_turn_native.h"
#include <cassert>

namespace vyruz {
// Bounded 444a40/443990 reconstruction for ordinary one-piece enemy families.
// Tank/Turret use a different gun controller (444b80); Player and Spider retain
// their own controllers. The logical ring node can differ from the visible pose.
inline bool supportsOnePieceEnemyIdle(const EnemyTurnConfig& config) {
    return !config.twoPieces && (config.subtype==0x16 || config.subtype==0x19 ||
        config.subtype==0x1a || config.subtype==0x1b || config.subtype==0x1c || config.subtype==0x1d);
}
inline bool supportsMultipartEnemyIdle(const EnemyTurnConfig& config) {
    return config.twoPieces && (config.subtype==0x1e || config.subtype==0x20);
}
struct EnemyIdleTurnAdmission {
    bool seekerOrTracker=false;
    float currentX=0,currentZ=0,previousX=0,previousZ=0;
    int movementCommand=0;
};
inline bool onePieceEnemyIdleTurnAdmitted(const EnemyTurnConfig& config,const EnemyTurnState& state,
                                        const EnemyIdleTurnAdmission& input) {
    return supportsOnePieceEnemyIdle(config) && config.canTurnOnPlace && input.seekerOrTracker &&
        input.currentX==input.previousX && input.currentZ==input.previousZ &&
        input.movementCommand==0 && state.turnCommand==0;
}
inline bool multipartEnemyIdleTurnAdmitted(const EnemyTurnConfig& config,const EnemyTurnState& state,
                                         const EnemyIdleTurnAdmission& input) {
    return supportsMultipartEnemyIdle(config) && config.canTurnOnPlace && input.seekerOrTracker &&
        input.currentX==input.previousX && input.currentZ==input.previousZ &&
        input.movementCommand==0 && state.turnCommand==0;
}

inline void advanceOnePieceEnemyIdleBodyController(const EnemyTurnConfig& config,EnemyTurnState& state,
                                                  int elapsedMs) {
    assert(supportsOnePieceEnemyIdle(config) && state.bodyPeriodMs>0);
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
            // 443a5d..443a92: move one logical heading node, then select its
            // full pose (48-slot Cop/Burner, 24-slot other ordinary families).
            state.logicalHeading=(state.logicalHeading+(state.turnCommand==0xcb?1:23))%24;
            state.bodyDesiredPose=state.logicalHeading*enemyTurnPoseMultiplier(config.subtype)+1;
            state.orthogonalHeading=(state.logicalHeading+3)%24;
        }
    } while(state.bodyRemainingMs<0);
}

template<class InclusiveRandom>
inline void advanceOnePieceEnemyIdleTurn(const EnemyTurnConfig& config,EnemyTurnState& state,
                                        int elapsedMs,InclusiveRandom&& random) {
    assert(supportsOnePieceEnemyIdle(config));
    // The original random endpoint is 1..24 even for these 48-pose families.
    // Caller supplies the saved native world259c RNG context and clears the
    // body turn command before AI; this is the subsequent admitted idle pass.
    if(state.bodyPose==state.bodyDesiredPose && random(1,150)==75)
        state.bodyDesiredPose=random(0,23)+1;
    if(state.bodyRemainingMs<0)stepEnemyTurnPose(config,state,false,true);
    state.turnCommand=enemyNativeTurnCommand(config.subtype,state.bodyPose,state.bodyDesiredPose);
    advanceOnePieceEnemyIdleBodyController(config,state,elapsedMs);
}

// 444b80 is deliberately NOT a catch-up body controller. A positive countdown
// subtracts elapsed and returns, even if it becomes negative. A due countdown
// adds a period without subtracting elapsed, and advances at most one pose.
inline void advanceMultipartEnemyIdleGunController(const EnemyTurnConfig& config,EnemyTurnState& state,
                                                   int elapsedMs) {
    assert(supportsMultipartEnemyIdle(config));
    if(state.gunCommand!=state.gunPreviousCommand) {
        state.gunRemainingMs=0;state.gunPreviousCommand=state.gunCommand;
    }
    if(state.gunRemainingMs>0) {state.gunRemainingMs-=elapsedMs;return;}
    state.gunRemainingMs+=state.gunInRange?state.gunPeriodMs:state.bodyPeriodMs;
    if(state.gunPose!=state.gunDesiredPose) {stepEnemyTurnPose(config,state,true,false);return;}
    if(state.gunCommand) {
        state.gunLogicalHeading=(state.gunLogicalHeading+(state.gunCommand==0xcb?1:23))%24;
        state.gunDesiredPose=state.gunLogicalHeading+1;
    }
}

template<class InclusiveRandom>
inline void advanceMultipartEnemyIdleTurn(const EnemyTurnConfig& config,EnemyTurnState& state,
                                         int elapsedMs,InclusiveRandom&& random) {
    assert(supportsMultipartEnemyIdle(config));
    if(state.gunPose==state.gunDesiredPose) {
        if(random(1,150)==75)state.gunDesiredPose=random(0,23)+1;
        // This gun step occurs even with a positive countdown; carry=true is
        // native argument ordering, not permission to refresh the gun ring.
        stepEnemyTurnPose(config,state,true,true);
    }
    if(state.gunRemainingMs<0)stepEnemyTurnPose(config,state,false,true);
    state.turnCommand=state.gunCommand=enemyNativeTurnCommand(config.subtype,state.bodyPose,state.bodyDesiredPose);
    advanceMultipartEnemyIdleGunController(config,state,elapsedMs);
}
}
