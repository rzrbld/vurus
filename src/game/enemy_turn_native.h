#pragma once
#include <cstdint>

namespace vyruz {
// 438390 / 42cfa0 / 42d150. Poses are native ONE-based slots, not headings.
// This contract uses integer native world milliseconds. Fixed-step callers must
// supply their own elapsed adapter; there is deliberately no catch-up loop.
// Player and Spider have their own accepted controllers; sharing this pure
// arithmetic does not authorize replacing those controllers.
struct EnemyTurnConfig {
    int subtype=0;
    bool twoPieces=false,canTurnOnPlace=true;
    int authoredBodyPeriodMs=0,authoredTurretPeriodMs=0;
};
struct EnemyTurnState {
    int bodyPose=1,bodyDesiredPose=1,bodyPreviousPose=1;
    int gunPose=1,gunDesiredPose=1,gunPreviousPose=1;
    int bodyPeriodMs=0,gunPeriodMs=0,bodyRemainingMs=0,gunRemainingMs=0;
    int logicalHeading=0,orthogonalHeading=3,turnCommand=0,lean=0;
    // 18c/228/100 and 128&80 are retained independently of the gun poses.
    // Multipart idle permits a negative gun countdown and pose/ring divergence.
    int gunLogicalHeading=0,gunCommand=0,gunPreviousCommand=0;
    bool gunInRange=false;
    bool gunStateKnown=false; // Legacy saves require class-aware reconstruction.
    bool fastTurn=false;
};
inline int enemyTurnPoseRing(int subtype) {
    return subtype>=0x19 && subtype<=0x20 && subtype!=0x1a?24:48;
}
inline int enemyTurnPoseMultiplier(int subtype) {
    return subtype==0x1f?6:enemyTurnPoseRing(subtype)==24?1:2;
}
inline bool enemyTurnSubtypeExcluded(int subtype) {
    return ((std::uint32_t{1}<<((static_cast<unsigned>(subtype)-0x10u)&31u))&0x1eu)!=0;
}
inline EnemyTurnState initializeEnemyTurn(const EnemyTurnConfig& config,int bodyHeading,int gunHeading) {
    EnemyTurnState state;
    const int multiplier=enemyTurnPoseMultiplier(config.subtype);
    const int pose=config.subtype==0x1f?bodyHeading/6*6+1:bodyHeading*multiplier+1;
    state.bodyPose=state.bodyDesiredPose=state.bodyPreviousPose=pose;
    state.gunPose=state.gunDesiredPose=state.gunPreviousPose=gunHeading*multiplier+1;
    state.logicalHeading=bodyHeading;state.orthogonalHeading=(bodyHeading+3)%24;
    state.gunLogicalHeading=gunHeading;
    state.gunStateKnown=true;
    state.bodyPeriodMs=config.authoredBodyPeriodMs;state.gunPeriodMs=config.authoredTurretPeriodMs;
    return state;
}
inline void stepEnemyTurnPose(const EnemyTurnConfig& config,EnemyTurnState& state,bool gun,bool carryGun) {
    if(enemyTurnSubtypeExcluded(config.subtype))return;
    int& current=gun?state.gunPose:state.bodyPose;
    const int desired=gun?state.gunDesiredPose:state.bodyDesiredPose;
    if(current==desired){state.fastTurn=false;return;}
    const int ring=enemyTurnPoseRing(config.subtype),half=ring/2,old=current;
    int next=current;
    // The native fast branch assumes a particular pose parity. Its explicit
    // boundary cases are preserved, including 0/ring+1 stress-case results.
    if(state.fastTurn) {
        if(current<desired)next=desired-current<half?current+2:current==1?ring-1:current-2;
        else next=current-desired<half?current-2:current==ring-1?1:current+2;
    } else {
        if(current<desired)next=desired-current<half?current+1:current==1?ring:current-1;
        else next=current-desired<half?current-1:current==ring?1:current+1;
    }
    current=next;
    (gun?state.gunPreviousPose:state.bodyPreviousPose)=old;
    if(carryGun && config.twoPieces) {
        const int delta=state.bodyPose-state.bodyPreviousPose;
        int* poses[]={&state.gunPose,&state.gunPreviousPose};
        if(delta)for(int* pose:poses) {
            *pose+=delta;
            if(*pose<1)*pose+=ring;else if(*pose>ring)*pose-=ring;
        }
    }
}
// 436520 is a turn-command classifier, NOT a geometry refresh. The renderer
// lean command uses numeric current-vs-desired order, not the short arc.
inline int enemyNativeTurnCommand(int subtype,int current,int desired) {
    if(enemyTurnSubtypeExcluded(subtype) || current==desired)return 0;
    if(desired)return current<desired?0xcb:0xcd;
    const int threshold=subtype==0x1f?6:
        subtype>=0x19 && subtype<=0x1e && subtype!=0x1a?1:2;
    return current>threshold?0xcb:0xcd;
}
// Native actor dispatch440fe1..440fff clears turnCommand before the AI pass.
// The body handler itself leaves it unchanged on an early/not-due return.
inline void advanceEnemyBodyTurn(const EnemyTurnConfig& config,EnemyTurnState& state,
        int desiredHeading,bool targetFacing,int elapsedMs,float cachedMagnitude,float authoredSpeedMax) {
    if(config.twoPieces && targetFacing)return;
    if(!config.canTurnOnPlace) {
        if(cachedMagnitude<1.f)return;
        if(config.authoredBodyPeriodMs) {
            // Native fields are float, but x87 keeps this expression extended
            // until __ftol truncates. Authored content supplies finite positive
            // Speed-max and an in-range resulting signed period.
            state.bodyPeriodMs=static_cast<int>((1.L-static_cast<long double>(cachedMagnitude)/
                static_cast<long double>(authoredSpeedMax))*config.authoredBodyPeriodMs);
            if(state.bodyRemainingMs>state.bodyPeriodMs)state.bodyRemainingMs=state.bodyPeriodMs;
        }
    }
    state.bodyRemainingMs-=elapsedMs;
    if(state.bodyRemainingMs>0)return;
    state.bodyRemainingMs+=state.bodyPeriodMs;
    if(state.bodyRemainingMs<0)state.bodyRemainingMs=0;
    const int multiplier=enemyTurnPoseMultiplier(config.subtype);
    state.bodyDesiredPose=(config.subtype==0x1f?((desiredHeading+3)/6*6)%24:desiredHeading*multiplier)+1;
    stepEnemyTurnPose(config,state,false,true);
    const auto heading=(static_cast<std::uint32_t>(state.bodyPose)-1u)/static_cast<unsigned>(multiplier);
    state.logicalHeading=static_cast<int>(heading)*(config.subtype==0x1f?multiplier:1);
    state.orthogonalHeading=(state.logicalHeading+3)%24;
    state.turnCommand=enemyNativeTurnCommand(config.subtype,state.bodyPose,state.bodyDesiredPose);
}
inline void advanceEnemyGunTurn(const EnemyTurnConfig& config,EnemyTurnState& state,
        int desiredHeading,bool targetFacing,int elapsedMs) {
    if(!config.twoPieces || !targetFacing)return;
    state.gunRemainingMs-=elapsedMs;
    if(state.gunRemainingMs>0)return;
    state.gunRemainingMs+=state.gunPeriodMs;
    if(state.gunRemainingMs<0)state.gunRemainingMs=0;
    state.gunDesiredPose=desiredHeading*enemyTurnPoseMultiplier(config.subtype)+1;
    stepEnemyTurnPose(config,state,true,false);
}
// 42d220 runs after BOTH route-facing handlers, including their not-due
// returns. Idle444a40 does not call it: visual gun and logical ring may then
// legitimately diverge until another admitted route-facing pass.
inline void synchronizeEnemyGunTurnHeading(const EnemyTurnConfig& config,EnemyTurnState& state) {
    if(config.twoPieces)
        state.gunLogicalHeading=(state.gunPose-1)/enemyTurnPoseMultiplier(config.subtype);
}
// 4333f0 is called once by an admitted render of an actor with BOTH lean banks.
// Callers must retain that admission/timing; advancing on every physics tick
// would make this visual controller differ from the original renderer.
inline int advanceEnemyLean(const EnemyTurnConfig& config,EnemyTurnState& state) {
    const bool cop=config.subtype==0x16 || config.subtype==0x18,burner=config.subtype==0x1a;
    if(state.turnCommand==0xcb || state.turnCommand==0xcd) {
        if(cop || burner) {
            state.lean+=state.turnCommand==0xcb?-1:1;
            const int bound=burner?1:15;
            if(state.lean>bound)state.lean=bound;else if(state.lean< -bound)state.lean= -bound;
        }
    } else if(state.lean)state.lean+=state.lean>0?-1:1;
    if(!state.lean)return -1;
    const int magnitude=state.lean<0?-state.lean:state.lean;
    if(cop)return magnitude<6?0:magnitude<=10?1:2;
    return burner?0:-1;
}
}
