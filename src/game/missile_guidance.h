#pragma once
#include "game/content.h"
#include "game/native_navigation.h"
#include <functional>
#include <optional>

namespace vyruz {
// Native 42c5a0 policy. Route construction/storage belongs to the world adapter.
enum class MissileGuidancePhase { Idle, Searching, Following };
struct MissileTargetSample {
    float x=0,y=0;
    int tileX=0,tileY=0;
};
struct MissileGuidanceState {
    MissileGuidancePhase phase=MissileGuidancePhase::Idle;
    int scanRemainingMs=0,pathRemainingMs=0,turnRemainingMs=0;
    int pose48=1;
    MissileTargetSample sampledTarget{};
};
// Native action owns every search-created point until replacement/disposal,
// including branches omitted from the selected route (42c2c0/42d520).
struct MissileNativeRoute {
    std::optional<NativeNavState> search;
    std::vector<NativeNavPoint> points,ownedPoints;
};
struct MissileSavedRoute {
    MissileNativeRoute route;
    MissileGuidanceState state;
    std::optional<size_t> target;
    // Existing surface route cache belongs to the saved native action too.
    std::vector<ContentPosition> surfaceRoute;
    size_t point=0;
    ContentPosition start{};
};
struct MissileNativeNavigation : MissileNativeRoute {
    std::optional<MissileSavedRoute> saved;
};
enum class MissilePathResult { Pending, Failed, Ready };
struct MissileGuidanceInput {
    int dtMs=0,scanPeriodMs=20;
    bool inCorridor=true,routeAtEnd=false,targetValid=false,targetInvisible=false;
    MissileTargetSample target{};
};
struct MissileGuidanceHooks {
    std::function<std::optional<MissileTargetSample>()> beginSearch;
    std::function<MissilePathResult()> stepSearch;
};
struct MissileGuidanceResult {
    bool clearRoute=false;
    bool steer=false; // Run route-node advancement and then missileGuidanceTurn.
};
MissileGuidanceResult advanceMissileGuidance(MissileGuidanceState&,
    const MissileGuidanceInput&,const MissileGuidanceHooks&);
// 42cfa0/438390: one half-pose at most per invocation; no dt catch-up loop.
// Caller supplies the effective native turn timeout (authored Missile value 6).
int missileGuidanceTurn(MissileGuidanceState&,int desiredHeading,int dtMs,
    int turnPeriodMs,bool mayTurn=true,bool fastTurn=false);
bool missileGuidanceInsideCorridor(float x,float y,float startX,float startY,
    float endX,float endY,float nativeBoundsWidth,float speed);
}
