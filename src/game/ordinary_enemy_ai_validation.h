#pragma once
#include "game/ordinary_enemy_ai_native.h"
#include <set>
namespace vyruz {
// Shared disk/live boundary. A failed check does not consume ownership claims.
inline bool validOrdinaryEnemyAI(const OrdinaryEnemyNativeState& state,size_t actors,
        const std::optional<NativeNavGraph>& graph,std::set<size_t>& claimed) {
    auto pending=claimed;
    const auto finite=[](NativeNavPoint p){return std::isfinite(p.x)&&std::isfinite(p.y);};
    const auto surface=[](ContentPosition p){return std::isfinite(p.x)&&std::isfinite(p.y);};
    const auto member=[&](NativeNavPoint p){
        if(!finite(p)||!graph||p.graphNode==SIZE_MAX||p.graphNode>=graph->objects.size())return false;
        const auto& o=graph->objects[p.graphNode];
        return o.alive&&o.point&&o.bounds.minX==p.x&&o.bounds.minY==p.y;
    };
    const auto route=[&](const NativeEnemyRouteCore& r,const std::set<size_t>& actionOwned){
        if(!finite(r.segmentStart)||!finite(r.segmentEnd)||!surface(r.sampledTarget)||
           r.cursor>r.points.size()||(r.target&&*r.target>=actors)||r.desiredHeading<0||r.desiredHeading>24||
           r.status>7)return false;
        for(int32_t timer:{r.clock.scanRemainingMs,r.clock.pathRemainingMs})
            if(timer< -100000000||timer>100000000)return false;
        if(r.search) {
            const auto& q=*r.search;
            if(!r.ownedPoints.empty()||r.clock.active||
               !finite(q.halfExtent)||q.halfExtent.x<0||q.halfExtent.y<0||
               !std::isfinite(q.maximumDistance)||q.maximumDistance<0||q.seen.size()<2)return false;
        } else if(r.clock.searching)return false;
        if(r.points.empty()) {
            if(r.clock.active||r.cursor||!r.ownedPoints.empty())return false;
        } else if(r.points.size()<2 || (r.clock.active&&r.cursor==0))return false;
        const auto& owned=r.search?r.search->seen:r.ownedPoints;
        std::set<size_t> local;
        for(auto p:owned)if(!member(p)||!local.insert(p.graphNode).second||!pending.insert(p.graphNode).second)return false;
        const auto ownedMember=[&](NativeNavPoint p){return member(p)&&local.contains(p.graphNode);};
        //Selected references may alias a snapshot's ownership during replans.
        for(auto p:r.points){
            if(p.graphNode==SIZE_MAX&&!r.clock.active&&r.search){if(!finite(p))return false;continue;}
            if(!member(p)||!actionOwned.contains(p.graphNode))return false;
        }
        if(r.search)for(const auto& c:r.search->candidates){
            if(c.points.empty()||c.cursor>=c.points.size())return false;
            for(auto p:c.points)if(!ownedMember(p))return false;
        }
        return true;
    };
    if(state.lastAction!=0&&state.lastAction!=1&&state.lastAction!=2&&state.lastAction!=4&&state.lastAction!=12)return false;
    if(state.patrol.point< -1 || (state.patrol.goal&&!surface(*state.patrol.goal)))return false;
    for(const auto* r:{&state.targeting,&state.base}) {
        std::set<size_t> actionOwned;
        const auto add=[&](const NativeEnemyRouteCore& c){for(auto p:c.search?c.search->seen:c.ownedPoints)actionOwned.insert(p.graphNode);};
        add(*r);for(const auto& saved:r->saved)add(saved);
        if(!route(*r,actionOwned))return false;
        for(const auto& saved:r->saved)if(!route(saved,actionOwned))return false;
    }
    claimed=std::move(pending);return true;
}
}
