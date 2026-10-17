#include "game/spider_brain.h"
#include <cmath>
#include <limits>

namespace vyruz {
namespace { void call(const std::function<void()>& fn) { if(fn)fn(); } }
void advanceSpiderAction(SpiderActionState& s,uint8_t& flags,
                         const SpiderActionInput& in,const SpiderActionCallbacks& cb) {
    // Preserve ordering: testing the rescan counter precedes decrementing it.
    if(s.active) {
        if(!in.routeInBounds) {
            flags&=~uint8_t(1);s.active=s.searching=false;
            if(!in.seeker)call(cb.dropRoute); // Patrol 42e030; seeker retains route.
            return;
        }
        flags|=1;
        if(s.scanRemainingMs>0) {
            s.scanRemainingMs-=in.dtMs;
            call(cb.steer);
        } else if(!in.seeker) {
            s.active=s.searching=false;
        } else if(!in.targetUnchanged) {
            s.active=s.searching=false;
        } else s.scanRemainingMs+=in.scanDelayMs;
        return;
    }
    flags&=~uint8_t(1);
    if(!s.searching) {
        if(s.scanRemainingMs>0) {s.scanRemainingMs-=in.dtMs;return;}
        s.scanRemainingMs+=in.scanDelayMs;
        if(cb.beginSearch && cb.beginSearch()) {
            s.searchRemainingMs=0;s.searching=true;
        } else if(in.seeker)flags|=2;
        else flags&=~uint8_t(2);
        return;
    }
    s.searchRemainingMs-=in.dtMs;
    if(s.searchRemainingMs>0)return;
    s.searchRemainingMs+=17;
    const auto result=cb.searchStep?cb.searchStep():SpiderSearchResult::Failure;
    if(result==SpiderSearchResult::Pending) {
        s.scanRemainingMs-=in.dtMs;return;
    }
    s.searching=false;
    if(result==SpiderSearchResult::Failure) {
        s.scanRemainingMs+=in.scanDelayMs;
        call(cb.dropRoute);
        if(!in.seeker)flags&=~uint8_t(2);
        return;
    }
    if(result==SpiderSearchResult::Rejected) {
        s.active=false;
        if(in.seeker)flags|=2;else flags&=~uint8_t(2);
        call(cb.dropRoute);
    } else {
        s.active=true;
        if(in.seeker)flags=(flags&~uint8_t(2))|1;else flags|=3;
        call(cb.clearSavedSearch);
        if(in.seeker)call(cb.cancelPatrol);
    }
    s.scanRemainingMs+=in.scanDelayMs;
}
std::optional<ContentPosition> selectSpiderPatrolWaypoint(SpiderPatrolState& s,
    std::span<const ContentPosition> path,ContentPosition p,bool initialReverse,
    bool closed,bool completedFinalSegment) {
    if(path.empty())return {};
    if(!s.initialized) {
        int best=std::numeric_limits<int>::max();s.point=-1;s.reverse=initialReverse;
        // 42e910 converts the actor surface position to integers before comparing.
        for(int i=int(path.size())-1;i>=0;--i) {
            const int d=std::abs(int(p.x)-int(path[i].x))+std::abs(int(p.y)-int(path[i].y));
            if(d<best) {best=d;s.point=i;}
        }
        s.initialized=true;
    } else if(completedFinalSegment && path.size()>1) {
        int next=s.point+(s.reverse?-1:1);
        if(next<0 || next>=int(path.size())) {
            if(closed)next=s.reverse?int(path.size())-1:0;
            else {s.reverse=!s.reverse;next=s.point+(s.reverse?-1:1);}
        }
        s.point=next;
    }
    if(s.point<0 || s.point>=int(path.size()))return {};
    return path[s.point];
}
} // namespace vyruz

namespace vyruz {
unsigned spiderRouteStatus(const SpiderSteeringInput& in) {
    if(in.terminalSegment && in.hasTarget &&
       (in.canTurnOnPlace || in.targetHeadingAllowed)) {
        const float minimum2=in.minimumDistance*in.minimumDistance;
        if(in.targetDistanceSquared<=minimum2*.25f)return 7;
        if(in.targetDistanceSquared<=minimum2)return 5;
    }
    if(in.terminalSegment) {
        if(in.hasTarget && !in.canTurnOnPlace && !in.targetHeadingAllowed)return 0;
        const float radius2=in.arrivalRadius*in.arrivalRadius;
        if(in.endpointDistanceSquared<=radius2*.5625f)return 3;
        return in.endpointDistanceSquared<=radius2?1:0;
    }
    return in.endpointDistanceSquared<=64?1:0;
}
bool spiderInsideRouteCorridor(float x,float z,float fromX,float fromZ,
    float toX,float toZ,float span,float magnitude) {
    const float padding=span+magnitude;
    return x>=std::min(fromX,toX)-padding && x<=std::max(fromX,toX)+padding &&
           z>=std::min(fromZ,toZ)-padding && z<=std::max(fromZ,toZ)+padding;
}
} // namespace vyruz
