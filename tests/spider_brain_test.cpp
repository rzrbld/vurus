#include "game/spider_brain.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
using namespace vyruz;
int main() {
    SpiderActionState s;uint8_t flags=0;int begins=0,steps=0,steers=0,drops=0,cancels=0;
    SpiderSearchResult result=SpiderSearchResult::Accepted;
    SpiderActionCallbacks cb{[&]{++begins;return true;},[&]{++steps;return result;},
        [&]{++steers;},[&]{++drops;},{},[&]{++cancels;}};
    SpiderActionInput in{16,500,true,true,true};
    advanceSpiderAction(s,flags,in,cb);
    assert(begins==1 && steps==0 && s.searching && s.scanRemainingMs==500 && flags==0);
    advanceSpiderAction(s,flags,in,cb);
    assert(steps==1 && s.active && !s.searching && flags==1 && cancels==1 && s.scanRemainingMs==1000);
    s.scanRemainingMs=1;advanceSpiderAction(s,flags,in,cb);
    assert(steers==1 && s.scanRemainingMs==-15);
    advanceSpiderAction(s,flags,in,cb);
    assert(steers==1 && s.scanRemainingMs==485 && s.active);
    s.scanRemainingMs=0;in.targetUnchanged=false;advanceSpiderAction(s,flags,in,cb);
    assert(!s.active && flags==1 && begins==1); // defer reacquisition to next invocation
    advanceSpiderAction(s,flags,in,cb);assert(s.searching && flags==0 && begins==2);
    result=SpiderSearchResult::Rejected;advanceSpiderAction(s,flags,in,cb);
    assert(!s.active && !s.searching && flags==2 && drops==1);
    s={false,true,3,0};in.routeInBounds=false;advanceSpiderAction(s,flags,in,cb);
    assert(!s.active && flags==2 && drops==1); // seeker retains escaped route
    in.seeker=false;s={false,true,3,0};advanceSpiderAction(s,flags,in,cb);
    assert(drops==2 && !s.active); // patrol drops escaped route
    in.routeInBounds=true;s={false,true,0,0};flags=3;
    advanceSpiderAction(s,flags,in,cb);assert(!s.active && flags==3 && steers==1);
    s={true,false,-4,0};result=SpiderSearchResult::Pending;
    advanceSpiderAction(s,flags,in,cb);assert(s.searching && s.scanRemainingMs==-20 && s.searchRemainingMs==1);
    result=SpiderSearchResult::Failure;advanceSpiderAction(s,flags,in,cb);
    assert(!s.searching && s.scanRemainingMs==480 && flags==0);
    // 42d760 restores the snapshot made after the rescan period was added.
    SpiderActionState rollback{true,false,18,0};
    const SpiderActionState saved{false,true,700,2};
    auto restore=cb;restore.dropRoute=[&]{rollback=saved;};
    result=SpiderSearchResult::Rejected;in.seeker=true;
    advanceSpiderAction(rollback,flags,in,restore);
    assert(rollback.active && !rollback.searching && rollback.scanRemainingMs==1200);
    rollback={true,false,18,0};result=SpiderSearchResult::Failure;
    advanceSpiderAction(rollback,flags,in,restore);
    assert(rollback.active && rollback.scanRemainingMs==700);
    const ContentPosition path[]={{0,0},{10,0},{20,0}};
    SpiderPatrolState p;
    auto goal=selectSpiderPatrolWaypoint(p,path,{5.9f,0},false,false,false);
    assert(goal && p.point==1); // integer distance tie visits reverse order
    goal=selectSpiderPatrolWaypoint(p,path,{0,0},false,false,false);assert(p.point==1);
    goal=selectSpiderPatrolWaypoint(p,path,{0,0},false,false,true);assert(p.point==2);
    goal=selectSpiderPatrolWaypoint(p,path,{0,0},false,false,true);assert(p.point==1 && p.reverse);
    goal=selectSpiderPatrolWaypoint(p,path,{0,0},false,false,true);assert(p.point==0);
    goal=selectSpiderPatrolWaypoint(p,path,{0,0},false,true,true);assert(p.point==2 && p.reverse);
    SpiderSteeringInput status;status.terminalSegment=true;
    status.endpointDistanceSquared=9;assert(spiderRouteStatus(status)==3);
    status.endpointDistanceSquared=16;assert(spiderRouteStatus(status)==1);
    status.endpointDistanceSquared=16.1f;assert(spiderRouteStatus(status)==0);
    status.terminalSegment=false;status.endpointDistanceSquared=64;assert(spiderRouteStatus(status)==1);
    status.terminalSegment=true;status.hasTarget=true;status.minimumDistance=400;
    status.targetDistanceSquared=40000;assert(spiderRouteStatus(status)==7);
    status.targetDistanceSquared=160000;assert(spiderRouteStatus(status)==5);
    status.targetDistanceSquared=160001;assert(spiderRouteStatus(status)==0);
    status.targetDistanceSquared=10;status.canTurnOnPlace=false;status.targetHeadingAllowed=false;
    assert(spiderRouteStatus(status)==0);
    assert(spiderInsideRouteCorridor(112,0,0,0,100,0,10,2));
    assert(!spiderInsideRouteCorridor(112.01f,0,0,0,100,0,10,2));
    std::cout<<"Spider native action, waypoint, status and corridor checks passed\n";
}
