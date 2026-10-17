#pragma once
#include "game/enemy_turn_native.h"

namespace vyruz {
//436600: the ring classifier returns D0 for an exact opposite when requested.
//D0 is reverse propulsion, not a turning command.24 is the invalid heading.
inline int nativePlayerTargetAction(int currentHeading,int targetHeading,
        bool oppositeReverse=true) {
    if(currentHeading<0 || currentHeading>=24 || targetHeading<0 || targetHeading>=24 ||
       currentHeading==targetHeading)return 0;
    const int forward=(targetHeading-currentHeading+24)%24;
    if(forward==12 && oppositeReverse)return 0xd0;
    return forward<=12?0xcb:0xcd;
}
//41da67..41da95: only moving mouse aim gets the adjacent-node deadband.
//moving means previous/current SURFACE differ, not nonzero thrust or speed.
inline int nativePlayerMouseAction(int currentHeading,int targetHeading,bool moving) {
    if(moving && currentHeading>=0 && currentHeading<24 && targetHeading>=0 && targetHeading<24) {
        const int delta=(targetHeading-currentHeading+24)%24;
        if(delta<=1 || delta==23)return 0;
    }
    return nativePlayerTargetAction(currentHeading,targetHeading);
}
//438540 control admission for mouse-only turning after the action classifier.
//The caller owns mouse-vector preparation and keyboard/strafe suppression.
inline int nativePlayerAimTurnCommand(int currentHeading,int targetHeading,
        bool moving,bool reverseCommands,bool blockedMotor) {
    if(blockedMotor)return 0; //128&02000000 clears220 before input polling.
    const int action=nativePlayerMouseAction(currentHeading,targetHeading,moving);
    if(action!=0xcb && action!=0xcd)return 0;
    return reverseCommands?(action==0xcb?0xcd:0xcb):action;
}
// Original443990's Vyruz branch only. This is independent of the accepted
// PlayerMotion solver and PlayerTurnPresentation adapter: neither may be
// silently replaced by this state. Native P/T/previous are ONE-based1..48.
struct NativePlayerFacingState {
    int pose=1,desiredPose=1,previousPose=1;
    int logicalHeading=0,orthogonalHeading=3;
    int remainingMs=0,periodMs=18;
    bool fastTurn=false;
};
inline NativePlayerFacingState initializeNativePlayerFacing(int heading,int periodMs=18) {
    NativePlayerFacingState s;
    s.logicalHeading=(heading%24+24)%24;
    s.orthogonalHeading=(s.logicalHeading+3)%24;
    s.pose=s.desiredPose=s.previousPose=s.logicalHeading*2+1;
    s.periodMs=periodMs;
    return s;
}
// Precondition: positive authored period and in-range signed time arithmetic.
// command is actor+220 (0,CB,CD), NOT a changed-heading observation.
inline void advanceNativePlayerFacing(NativePlayerFacingState& state,int command,int elapsedMs) {
    state.remainingMs-=elapsedMs;
    if(state.remainingMs>0)return;
    do {
        state.remainingMs+=state.periodMs;
        if(state.pose!=state.desiredPose) {
            EnemyTurnState step;
            step.bodyPose=state.pose;step.bodyDesiredPose=state.desiredPose;
            step.bodyPreviousPose=state.previousPose;step.fastTurn=state.fastTurn;
            stepEnemyTurnPose({0x18},step,false,false);
            state.pose=step.bodyPose;state.previousPose=step.bodyPreviousPose;
            state.fastTurn=step.fastTurn;
            if(state.pose==state.desiredPose && command==0)return;
            //443a10: reaching the desired pose does NOT necessarily commit
            //the next logical node during this invocation.
            if(state.remainingMs>=0)return;
        }
        if(command!=0 && state.pose==state.desiredPose) {
            state.logicalHeading=(state.logicalHeading+(command==0xcb?1:23))%24;
            state.orthogonalHeading=(state.logicalHeading+3)%24;
            state.desiredPose=state.logicalHeading*2+1;
        }
    } while(state.remainingMs<0);
}
inline bool nativePlayerFacingRested(const NativePlayerFacingState& state) {
    return state.pose==state.desiredPose;
}
} // namespace vyruz
