#pragma once
#include "game/content.h"
#include <cstdint>
#include <functional>
#include <span>
#include <optional>

namespace vyruz {
// 42e400 / 42c5a0 action flags and the two independent countdowns.
struct SpiderActionState {
    bool searching=false, active=false;
    float scanRemainingMs=0, searchRemainingMs=0;
};
enum class SpiderSearchResult { Failure, Pending, Accepted, Rejected };
struct SpiderActionInput {
    float dtMs=0, scanDelayMs=0;
    bool seeker=false, routeInBounds=true, targetUnchanged=true;
};
struct SpiderActionCallbacks {
    std::function<bool()> beginSearch;
    std::function<SpiderSearchResult()> searchStep;
    std::function<void()> steer;
    std::function<void()> dropRoute;
    std::function<void()> clearSavedSearch;
    std::function<void()> cancelPatrol;
};
// actorFlags: native actor+13c bits 0 (active) and 1 (rejected/base action).
void advanceSpiderAction(SpiderActionState&,uint8_t& actorFlags,
                         const SpiderActionInput&,const SpiderActionCallbacks&);
struct SpiderPatrolState { bool initialized=false,reverse=false; int point=-1; };
// completedFinalSegment is route cursor==null && (last route status&1).
// A retry or interrupted route deliberately retains the authored goal.
std::optional<ContentPosition> selectSpiderPatrolWaypoint(SpiderPatrolState&,
    std::span<const ContentPosition>,ContentPosition position,bool initialReverse,
    bool closed,bool completedFinalSegment);
} // namespace vyruz

namespace vyruz {
struct SpiderSteeringInput {
    // Native-coordinate squared distances, never surface-coordinate lengths.
    float endpointDistanceSquared=0,targetDistanceSquared=0;
    float arrivalRadius=4,minimumDistance=0;
    bool terminalSegment=false,hasTarget=false;
    bool canTurnOnPlace=true,targetHeadingAllowed=true;
};
// 42cd30 and42ce40: 1=advance,3=advance+inner arrival,5=near,7=brake.
unsigned spiderRouteStatus(const SpiderSteeringInput&);
// 42d820 native bounds span is maxX-minX, not a circular radius.
bool spiderInsideRouteCorridor(float x,float z,float fromX,float fromZ,
    float toX,float toZ,float actorBoundsSpan,float cachedMagnitude);
} // namespace vyruz
