#include "game/missile_guidance.h"
#include <cassert>
#include <iostream>
using namespace vyruz;
int main(){
 MissileGuidanceState s; MissileGuidanceInput in; in.dtMs=10;
 int starts=0,steps=0; MissileGuidanceHooks hooks;
 hooks.beginSearch=[&]()->std::optional<MissileTargetSample>{++starts;return MissileTargetSample{10,20,1,2};};
 hooks.stepSearch=[&]{++steps;return steps==1?MissilePathResult::Pending:MissilePathResult::Ready;};
 advanceMissileGuidance(s,in,hooks);assert(starts==1&&steps==0&&s.phase==MissileGuidancePhase::Searching);
 advanceMissileGuidance(s,in,hooks);assert(steps==1&&s.pathRemainingMs==7&&s.scanRemainingMs==10);
 auto r=advanceMissileGuidance(s,in,hooks);assert(steps==2&&!r.steer&&s.phase==MissileGuidancePhase::Following&&s.scanRemainingMs==30);
 in.targetValid=true;in.target=s.sampledTarget;
 for(int i=0;i<3;++i)assert(advanceMissileGuidance(s,in,hooks).steer);
 assert(s.scanRemainingMs==0);
 // Interior route allows movement inside the sampled tile, even invisibility.
 in.target.x=11;in.targetInvisible=true;
 r=advanceMissileGuidance(s,in,hooks);assert(!r.steer&&!r.clearRoute&&s.scanRemainingMs==20);
 s.scanRemainingMs=0;in.routeAtEnd=true;
 r=advanceMissileGuidance(s,in,hooks);assert(r.clearRoute&&s.phase==MissileGuidancePhase::Idle);
 s.phase=MissileGuidancePhase::Following;s.scanRemainingMs=9;in.inCorridor=false;
 r=advanceMissileGuidance(s,in,hooks);assert(r.clearRoute&&s.scanRemainingMs==9);
 // Exact scan boundary validates on following invocation, not crossing frame.
 s.phase=MissileGuidancePhase::Following;s.scanRemainingMs=1;in.inCorridor=true;in.targetValid=false;
 assert(advanceMissileGuidance(s,in,hooks).steer);
 assert(advanceMissileGuidance(s,in,hooks).clearRoute);
 // One 48-pose step per tick, with reverse shortest-path tie convention.
 s={};assert(missileGuidanceTurn(s,1,100,6)==0&&s.pose48==2&&s.turnRemainingMs==0);
 assert(missileGuidanceTurn(s,1,100,6)==1&&s.pose48==3);
 s.pose48=1;missileGuidanceTurn(s,12,10,6);assert(s.pose48==48);
 s.pose48=25;missileGuidanceTurn(s,0,10,6);assert(s.pose48==26);
 s.pose48=1;missileGuidanceTurn(s,23,10,6,true,true);assert(s.pose48==47);
 s.turnRemainingMs=5;missileGuidanceTurn(s,24,10,6);assert(s.pose48==47&&s.turnRemainingMs==5);
 s.turnRemainingMs=5;missileGuidanceTurn(s,5,10,6,false);assert(s.pose48==47&&s.turnRemainingMs==5);
 assert(missileGuidanceInsideCorridor(-5,15,0,0,10,10,2,3));
 assert(!missileGuidanceInsideCorridor(-5.01f,15,0,0,10,10,2,3));
 // Failed search adds one scan period and releases the world route.
 s={};advanceMissileGuidance(s,in,hooks);hooks.stepSearch=[] {return MissilePathResult::Failed;};
 r=advanceMissileGuidance(s,in,hooks);assert(r.clearRoute&&s.phase==MissileGuidancePhase::Idle&&s.scanRemainingMs==40);
 std::cout<<"missile guidance policy passed\n";
}
