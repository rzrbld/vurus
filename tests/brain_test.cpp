#include "game/brain.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
using namespace vyruz;
int main() {
    TextDocument authored;std::string error;
    assert(parseContentText("TYPE Seeker [\nTarget = Vyruz\nSensor-Range = 500\nScan-Delay = 400\nMin-distance = 250\n]\nTYPE Patrol [\nPath-ID = 4\nClosed = 1\nReverse = 1\n]\nTYPE Guardian [\nLocation = -1, -1\nCenter = -1, -1\nRange = 300\n]\n", "synthetic", authored, error));
    ObjectPlacement object;object.position={100,200};object.properties=authored.nodes;object.behaviours={"Patrol","Seeker","Guardian"};
    SceneContent scene;ObjectClass cl;cl.id={3};cl.name="Vyruz";scene.classes.push_back(cl);
    const auto parsed=makeBrainConfig(object,scene);
    assert(parsed.base==BrainBase::Patrol&&parsed.seeker&&parsed.guardian&&parsed.targetClass->value==3);
    assert(parsed.sensorRange==500&&parsed.scanDelayMs==400&&parsed.minimumDistance==250&&parsed.pathId==4&&parsed.closed&&parsed.reverse);
    assert(!parsed.guardianCenterFollowsActor&&parsed.guardianRange==300&&parsed.home.x==100&&parsed.home.y==200);
    assert(nativeDistanceSquared({0,0},{4,2})==20);
    BrainConfig c;c.base=BrainBase::Patrol;
    ContentPosition path[]={{0,0},{100,0},{200,0}};
    BrainInput in;in.position={150,0};in.path=path;
    BrainState state;
    auto intent=advanceBrain(c,state,in);assert(state.point==2&&intent.destination->x==200&&intent.patrolDestination);
    in.reachedDestination=true;intent=advanceBrain(c,state,in);assert(state.point==1&&state.reverse);
    advanceBrain(c,state,in);assert(state.point==0);
    advanceBrain(c,state,in);assert(state.point==1&&!state.reverse);
    c.closed=true;state={};in.position={200,0};in.reachedDestination=false;
    advanceBrain(c,state,in);in.reachedDestination=true;advanceBrain(c,state,in);assert(state.point==0);
    in.path={};state={};assert(advanceBrain(c,state,in).unsupported);

    c={};c.base=BrainBase::Patrol;c.seeker=true;c.targetClass=ClassId{1};c.sensorRange=100;c.minimumDistance=30;c.scanDelayMs=400;
    in={};in.targetClass={1};in.position={0,0};in.targetPosition={100,0};in.facing=9;in.weaponRange=100;state={};
    intent=advanceBrain(c,state,in);assert(state.acquired&&intent.fire&&intent.destination);
    in.directRoute=false;intent=advanceBrain(c,state,in);assert(!intent.fire&&!intent.destination&&intent.unsupported);
    in.targetVisible=false;intent=advanceBrain(c,state,in);assert(!state.acquired&&!intent.fire);
    in.targetVisible=true;in.directRoute=true;in.dtMs=399;advanceBrain(c,state,in);assert(!state.acquired);
    in.dtMs=1;advanceBrain(c,state,in);assert(state.acquired);
    in.targetPosition={10,0};intent=advanceBrain(c,state,in);assert(intent.fire&&!intent.destination);
    in.facing=8;assert(!advanceBrain(c,state,in).fire);
    in.targetAlive=false;assert(!advanceBrain(c,state,in).fire);
    in.targetAlive=true;in.dtMs=400;in.targetClass={2};advanceBrain(c,state,in);assert(!state.acquired);
    in.targetClass={1};in.targetPosition={0,100};advanceBrain(c,state,in);assert(!state.acquired); // Native vertical range differs.
    c.base=BrainBase::Fixed;in.targetPosition={100,0};in.facing=9;intent=advanceBrain(c,state,in);assert(state.acquired&&!intent.destination&&intent.fire);
    in.fixedSeekerMoves=true;assert(advanceBrain(c,state,in).destination);in.fixedSeekerMoves=false;
    c.guardian=true;c.guardianCenterFollowsActor=false;c.center={1000,0};c.home={900,0};c.guardianRange=50;
    intent=advanceBrain(c,state,in);assert(!state.acquired&&intent.destination->x==900);
    in.targetPosition={1000,0};intent=advanceBrain(c,state,in);assert(state.acquired&&!intent.fire);
    c.guardian=false;c.visionDegrees=30;in.targetPosition={-50,0};in.facing=9;advanceBrain(c,state,in);assert(!state.acquired);
    c.base=BrainBase::Random;assert(advanceBrain(c,state,in).unsupported);
    int calls=0,cells=0;
    auto candidate=chooseRandomDestination({0,0},64,10,10,[&](int low,int high){assert(low==0);++calls;assert(high==(calls%2?1:2));return high;},[&](int,int){++cells;return true;});
    assert(candidate&&candidate->x==96&&candidate->y==80&&calls==2&&cells==20);
    calls=0;
    candidate=chooseRandomDestination({0,0},0,10,5,[&](int low,int high){assert(low==0);++calls;assert(high==(calls%2?9:4));return high;},[](int,int){return false;});
    assert(!candidate&&calls==20);
    calls=0;
    candidate=chooseRandomDestination({500,200},0,3,3,[&](int low,int high){++calls;return calls<=2?low:high;},[](int x,int y){return x!=0||y!=0;});
    assert(!candidate&&calls==20); // Every 5x5 neighborhood includes the occupied corner.
    assert(!chooseRandomDestination({0,0},0,0,3,[](int a,int){return a;},[](int,int){return true;}));
    // Spider detection alone does not seize control from its authored patrol.
    c={};c.base=BrainBase::Patrol;c.seeker=true;c.targetClass=ClassId{1};
    c.sensorRange=800;c.minimumDistance=400;c.scanDelayMs=1;
    in={};in.position={150,0};in.path=path;in.targetClass={1};
    in.targetPosition={160,0};in.stationaryAttack=true;in.seekerRouteAccepted=false;
    state={};intent=advanceBrain(c,state,in);
    assert(state.acquired && intent.patrolDestination && intent.destination);
    in.seekerRouteAccepted=true;intent=advanceBrain(c,state,in);
    assert(state.acquired && !intent.patrolDestination && !intent.destination && intent.aim);
    in.seekerRouteAccepted=false;intent=advanceBrain(c,state,in);
    assert(intent.patrolDestination && intent.destination); // leave attack axis -> resume route
    in.seekerRouteAccepted=true;in.targetPosition={750,0};intent=advanceBrain(c,state,in);
    assert(!intent.destination); // native admitted Spider seeker clears forward drive

    // Isometric axis (100,50) is native heading 6, not surface heading 7.
    in.position={0,0};in.targetPosition={100,50};in.weaponRange=320;
    in.spiderFacing=true;in.facing=7;
    assert(!advanceBrain(c,state,in).fire);
    in.facing=6;assert(advanceBrain(c,state,in).fire);
    in.facing=5;assert(!advanceBrain(c,state,in).fire);
    in.facing=6;in.targetPosition={500,250};
    assert(!advanceBrain(c,state,in).fire && state.acquired);
    in.targetPosition={100,50};in.directRoute=false;
    assert(!advanceBrain(c,state,in).fire);

}
