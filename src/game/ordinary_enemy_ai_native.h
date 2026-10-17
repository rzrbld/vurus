#pragma once
#include "game/content.h"
#include "game/native_navigation.h"
#include "game/native_random.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace vyruz {
// Native action+38 modes, not the attribute's independent kind6/7.
enum class NativeEnemyActionMode : int { Seeker=1,Tracker=2,Random=4,Patrol=12 };
struct NativeEnemyActionClock {
    bool searching=false,active=false;
    int32_t scanRemainingMs=0,pathRemainingMs=0;
};
struct NativeEnemyRouteCore {
    NativeEnemyActionClock clock;
    std::optional<NativeNavState> search;
    std::vector<NativeNavPoint> points,ownedPoints;
    size_t cursor=0;
    NativeNavPoint segmentStart{},segmentEnd{};
    ContentPosition sampledTarget{};
    int32_t sampledCellX=0,sampledCellY=0;
    std::optional<size_t> target;
    int desiredHeading=24;
    unsigned status=0;
};
struct NativeEnemyRouteState : NativeEnemyRouteCore {
    // Oldest first, newest last. Native42dce0 can retain a prior snapshot chain;
    // accepted replacement disposes oldest first, rollback restores the back.
    std::vector<NativeEnemyRouteCore> saved;
};
struct NativeEnemyPatrolState {
    bool initialized=false,reverse=false;
    int point=-1;
    std::optional<ContentPosition> goal;
    int32_t goalCellX=0,goalCellY=0;
};
struct NativeEnemyRandomState {
    int32_t reuseCount=0;
    NativeRandomState replayRandom{0,0};
};
struct OrdinaryEnemyNativeState {
    NativeEnemyRouteState targeting,base;
    NativeEnemyPatrolState patrol;
    NativeEnemyRandomState random;
    uint32_t actorFlags=0; // Native13c active/rejected bits; other bits retained.
    int lastAction=0;
};
struct NativeEnemyActionConfig {
    NativeEnemyActionMode mode=NativeEnemyActionMode::Seeker;
    int32_t scanPeriodMs=0;
    int subtype=0x19;
    bool straightBase=false,moveDuringPathSearch=false;
};
struct OrdinaryEnemyNativeConfig {
    bool enabled=false;
    NativeEnemyActionConfig targeting,base;
    int32_t sensorRange=0,minimumDistance=0,visionDegrees=360,randomRange=0;
    int patrolArrival=5;
    bool canTurnOnPlace=false,reverse=false,closed=false;
    // Class-derived native Guardian attributes. Negative authored pairs resolve
    // to the actor's birth center, not its current position on each restore.
    bool guardian=false;
    NativeNavPoint guardianHome{},guardianCenter{};
    int32_t guardianRange=0;
};
// 413400 stores native birth coordinates into either negative Guardian pair.
NativeNavPoint resolveNativeEnemyGuardianPoint(NativeNavPoint authored,NativeNavPoint birth);
struct NativeEnemyGuardianGoal {
    ContentPosition sample{};
    int32_t cellX=0,cellY=0;
    NativeNavPoint destination{};
};
// 42d8f0 deliberately treats the retained native Location as surface XY when
// initializing its fallback route. Preserve this original double transform.
NativeEnemyGuardianGoal nativeEnemyGuardianGoal(NativeNavPoint location);
// 42da80 keeps the acquisition window centered on the actor's CELL, but tests
// distance around the fixed Guardian center (or current center if either native
// center component remains negative). Guardian ignores the authored view cone.
NativeNavPoint nativeEnemyDetectionCenter(bool guardian,NativeNavPoint stored,NativeNavPoint current);
bool nativeEnemyTargetDetected(NativeNavPoint center,NativeNavPoint target,
    int32_t range,int facing,int cone);
struct NativeEnemyWeaponAimInput {
    NativeNavPoint source{},target{};
    int bodyHeading=0,gunHeading=0,desiredHeading=0,subtype=0x16,bodyPose=1;
    int32_t range=0;
    unsigned status=0;
    int movementCommand=0;
    bool targetPresent=false,terminal=false,guided=false,twoPieces=false;
    bool canTurnOnPlace=true,weaponPresent=false,ready=true;
    int turnCommand=0;
};
struct NativeEnemyWeaponAimResult {
    bool admitted=false,fire=false;
    int movementCommand=0;
};
// 42d2d0 route/body/gun admission and range/fire bits. Native cooldown,
// ammunition and barrel availability feed ready; execution remains with fire().
NativeEnemyWeaponAimResult nativeEnemyWeaponAim(const NativeEnemyWeaponAimInput&);
// 42c5a0 validates identity then surface XY/invisibility at a terminal cursor,
// but only native cell coordinates while an interior route edge is retained.
bool nativeEnemyTargetUnchanged(bool identityExists,bool nativeActor,bool invisible,
    bool terminal,ContentPosition current,ContentPosition sampled,
    int32_t currentCellX,int32_t currentCellY,int32_t sampledCellX,int32_t sampledCellY);
// 42e040: terminal routes do not touch the Random attribute reuse counter.
bool reuseNativeEnemyRandomGoal(NativeEnemyRandomState&,bool terminal,ContentPosition);
std::optional<ContentPosition> selectNativeEnemyPatrolGoal(NativeEnemyPatrolState&,
    std::span<const ContentPosition>,ContentPosition,bool initialReverse,bool closed,
    bool completedFinalSegment);
struct NativeEnemySteeringInput {
    NativeNavPoint position{},endpoint{};
    std::optional<NativeNavPoint> target;
    int32_t minimumDistance=0,arrivalRadius=0;
    int orthogonalHeading=0;
    bool terminal=false,targetTerminalOrGuided=false,canTurnOnPlace=true;
};
unsigned nativeEnemySteeringStatus(const NativeEnemySteeringInput&);
//43c4a0 is dispatched for4371b0-counted enemies. Scan is signed, sensor is
//native unsigned; both multiply in x87 precision then __ftol truncates.
void applyNativeEnemyDetectionDifficulty(int32_t& scan,int32_t& sensor,
    bool countedEnemy,float scanRatio,float sensorRatio);
struct NativeEnemyTargetWindow {int left=0,right=-1,top=0,bottom=-1;};
NativeEnemyTargetWindow nativeEnemyTargetWindow(int32_t sensorRange,int cellX,int cellY,int width,int height);
struct NativeEnemyActionInput {
    int32_t elapsedMs=0;
    bool inCorridor=true,targetUnchanged=true,targetIsPlayer=false;
};
struct NativeEnemyActionActor {
    uint32_t flags=0;
    int movementCommand=0,lastAction=0;
};
struct NativeEnemyActionHooks {
    std::function<bool()> beginSearch;
    std::function<int()> searchWave; // Original410460 raw0/-2/-1/1 return.
    std::function<bool()> selectAndAdvance;
    // MUST restore complete action, including this same mutable clock. Failure
    // and completed-route rejection have different post-rollback ordering.
    std::function<void()> rollback,discardSaved,cancelBase,steer;
};
struct NativeEnemyActionResult { bool escaped=false,locked=false; };
bool nativeEnemyBaseActionAdmitted(uint32_t actorFlags);
NativeEnemyActionResult advanceNativeEnemyAction(NativeEnemyActionClock&,
    NativeEnemyActionActor&,const NativeEnemyActionConfig&,
    const NativeEnemyActionInput&,const NativeEnemyActionHooks&);
}
