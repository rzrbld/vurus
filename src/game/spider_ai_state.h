#pragma once
#include <cmath>
#include "game/spider_brain.h"
#include "game/native_navigation.h"
#include "game/enemy_turn_native.h"
namespace vyruz {
struct SpiderRouteCore {
    SpiderActionState action;
    std::optional<NativeNavState> search;
    std::vector<NativeNavPoint> points;
    size_t cursor=0;
    NativeNavPoint segmentStart{},segmentEnd{};
    ContentPosition sampledTarget{};
    std::optional<size_t> target;
    int status=0;
};
struct SpiderRouteState : SpiderRouteCore {
    std::optional<SpiderRouteCore> saved;
};
struct SpiderMotionHistory {
    NativeNavPoint center;
    ContentPosition previousSurface;
};
struct SpiderAIState {
    SpiderPatrolState patrol;
    SpiderRouteState patrolRoute,seekerRoute;
    uint8_t actorFlags=0;
    std::optional<SpiderMotionHistory> motion;
    // Route and idle share this controller. Actor.direction is the visible
    // zero-based pose; the logical heading may differ during an idle turn.
    std::optional<EnemyTurnState> turn;
};
}

namespace vyruz {
inline bool validSpiderAI(const SpiderAIState& state,size_t actors){
    const auto point=[](NativeNavPoint p){return std::isfinite(p.x)&&std::isfinite(p.y);};
    const auto route=[&](const SpiderRouteCore& r){
        if(r.cursor>r.points.size() || (r.target&&*r.target>=actors) || r.status<0 || r.status>7 ||
           !std::isfinite(r.action.scanRemainingMs)||!std::isfinite(r.action.searchRemainingMs)||
           !point(r.segmentStart)||!point(r.segmentEnd)||!std::isfinite(r.sampledTarget.x)||!std::isfinite(r.sampledTarget.y))return false;
        for(auto p:r.points)if(!point(p))return false;
        if(r.search){const auto& s=*r.search;
            if(!point(s.halfExtent)||s.halfExtent.x<0||s.halfExtent.y<0||!std::isfinite(s.maximumDistance)||s.maximumDistance<0)return false;
            for(auto p:s.seen)if(!point(p))return false;
            for(const auto& c:s.candidates){if(c.points.empty()||c.cursor>=c.points.size())return false;for(auto p:c.points)if(!point(p))return false;}
        }
        return true;
    };
    if(state.motion && (!point(state.motion->center)||!std::isfinite(state.motion->previousSurface.x)||!std::isfinite(state.motion->previousSurface.y)))return false;
    if(state.turn){const auto& t=*state.turn;
        for(int pose:{t.bodyPose,t.bodyDesiredPose,t.bodyPreviousPose,t.gunPose,t.gunDesiredPose,t.gunPreviousPose})if(pose<1||pose>24)return false;
        if(t.logicalHeading<0||t.logicalHeading>23||t.orthogonalHeading!=(t.logicalHeading+3)%24||
           (t.turnCommand!=0&&t.turnCommand!=203&&t.turnCommand!=205)||
           t.bodyPeriodMs<=0||t.bodyPeriodMs>100000000||t.bodyRemainingMs<0||t.bodyRemainingMs>100000000)return false;
    }
    return state.actorFlags<=3&&state.patrol.point>=-1&&route(state.patrolRoute)&&route(state.seekerRoute)&&
        (!state.patrolRoute.saved||route(*state.patrolRoute.saved))&&(!state.seekerRoute.saved||route(*state.seekerRoute.saved));
}
}
