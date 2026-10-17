#include "game/native_navigation.h"
#include <bit>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static float f(unsigned u){return std::bit_cast<float>(u);}
// Recorded original PE40f310 outputs. Non-dyadic cases expose x87 stores.
struct Fixture{std::array<unsigned,18> words;size_t count;};
int main(){try{
const Fixture fixtures[]={
 {{{0xc2ddc1bdu,0xc28ee9dau,0xc4afdd34u,0xc480d7aeu,0x4026978du,0x42179ad6u,0xc2f2f679u,0xc1cc71beu,0xc262aa9fu,0xc0b5ba72u,0xc304159bu,0xc28ee9dau,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u}},1}, // original record 8
 {{{0xc301d8b5u,0xc375f350u,0x4460e05bu,0xc3661f90u,0x416eed91u,0x40922c38u,0xc2d5d3b7u,0xc39f5225u,0xc2926b6eu,0xc3814259u,0xc2491b78u,0xc375f350u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u}},1}, // original record 120
 {{{0x439abb65u,0xc4cb290du,0xc3c635b1u,0xc4034bd4u,0x4159020cu,0x402dd3b9u,0x43888b09u,0xc4c9d223u,0x438ff355u,0xc4bc80c2u,0x437b85f1u,0xc4cb290du,0x439abb65u,0xc4bb29d8u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u}},2}, // original record 884
 {{{0xc47e01e5u,0x44503d97u,0x441a71c0u,0xc42e69d8u,0x41f34fdfu,0x421d0974u,0xc492205eu,0x44503d97u,0xc483ce32u,0x4469fb37u,0xc47e01e5u,0x44446d00u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u}},1}, // original record 1916
 {{{0xc499b812u,0x44b33f2du,0xc4a72128u,0x440ba910u,0x413eb439u,0x3f5d3c02u,0xc499b812u,0x44b45ad5u,0xc4985090u,0x44bd1ec8u,0xc49c357au,0x44b33f2du,0xc495d328u,0x44b33f2du,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u}},2}, // original record 2308
 {{{0xc455fb38u,0xc4e3d6dfu,0x44c4edcbu,0xc4d63a6eu,0x41150e56u,0x404587ddu,0xc46292ecu,0x4383aaa4u,0xc44b25e4u,0x438f84b6u,0xc466e725u,0x437c3f29u,0xc446d1abu,0x437c3f29u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u}},2}, // original record 2537
 {{{0x446acdffu,0xc1d1947fu,0xc4e9c3e8u,0x44f4d480u,0x4206428fu,0x41b2c3ebu,0x446069d6u,0xc2c87e62u,0x447192ebu,0xc2622c35u,0x445605adu,0xc1d1947fu,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u}},1}, // original record 2720
 {{{0x43191618u,0x43f3f350u,0x4419a451u,0xc40d8b09u,0x410153f7u,0x40d5554bu,0x430a4581u,0x42612f5eu,0x4373947fu,0x43310973u,0x42f46083u,0x433fb41du,0x4381d4dfu,0x433fb41du,0x4996b438u,0x4996b438u,0x4996b438u,0x4996b438u}},2}, // original record 2797
};
for(const auto& fixture:fixtures){const auto&w=fixture.words;NativeNavPoint a{f(w[0]),f(w[1])},b{f(w[2]),f(w[3])},h{f(w[4]),f(w[5])};NativeNavObstacle obstacle{f(w[6]),f(w[7]),f(w[8]),f(w[9])};
auto result=nativeNavigationCornersExact(a,b,h,obstacle);need(result.size()==fixture.count,"original PE corner count");for(size_t i=0;i<result.size();++i)need(std::bit_cast<unsigned>(result[i].x)==w[10+i*2]&&std::bit_cast<unsigned>(result[i].y)==w[11+i*2],"original PE corner float output");
auto native=beginNativeNavigation(a,b,h);auto legacy=native;auto query=[&](auto,auto,auto,bool){return std::optional<NativeNavObstacle>(obstacle);};
advanceNativeNavigation(native,query,{},true);advanceNativeNavigation(legacy,query);need(native.candidates.size()==fixture.count,"native corner wave dispatch");auto previous=nativeNavigationCorners(a,b,h,obstacle);need(legacy.candidates.size()==previous.size(),"accepted Spider default corner arithmetic");
for(size_t i=0;i<native.candidates.size();++i)need(native.candidates[i].points[1]==result[result.size()-1-i],"native corner reverse insertion order");
}
// Native initial-segment discovery is centreline, not an inflated body sweep.
// A clear narrow centreline is admitted even if the authored half extent would
// overlap both surrounding boxes. Later nodes use the separate overlap query.
std::vector<NativeNavObstacle> obstacles={{40,10,60,40},{40,-40,60,-10}};
auto query=[&](auto a,auto b,auto h,bool overlap){return queryNativeNavigation(obstacles,a,b,h,overlap);};
auto narrow=beginNativeNavigation({0,0},{100,0},{20,20});
need(advanceNativeNavigation(narrow,query,{},true)==NativeNavProgress::Found,"native narrow centreline admission");
auto selected=selectNativeNavigation(narrow,query);need(selected&&selected->points.size()==2,"narrow route has no lattice detour");
obstacles={{40,-20,60,20}};auto detour=beginNativeNavigation({0,0},{100,0},{10,10});
std::optional<NativeNavCandidate> route;
for(int wave=0;wave<20;++wave){auto progress=advanceNativeNavigation(detour,query,{},true);if(progress==NativeNavProgress::Found||progress==NativeNavProgress::Complete){route=selectNativeNavigation(detour,query);break;}}
need(route&&route->points.size()==4,"native symmetrical detour");
need(route->points[1]==NativeNavPoint{22,38}&&route->points[2]==NativeNavPoint{78,38},"reverse corner insertion and first score tie");
// Search distance is anchored at the original start and is checked before a
// new graph point is enrolled, rather than imposing an arbitrary queue budget.
auto limited=beginNativeNavigation({0,0},{100,0},{10,10},10);
need(advanceNativeNavigation(limited,query,{},true)==NativeNavProgress::Working&&limited.candidates.empty(),"bounded corner admission");
need(advanceNativeNavigation(limited,query,{},true)==NativeNavProgress::Failed,"bounded search fails on following wave");
// Native410250 compares an extended-precision distance against a float-spilled
// radius square. A tiny perpendicular component must not disappear at600².
for(int boundary:{-1,0,1}){
 NativeNavObstacle box{boundary<0?std::nextafter(608.f,0.f):608.f,boundary>0?8.01f:8.f,620,20};
 auto atBoundary=beginNativeNavigation({0,0},{700,0},{0,0},600);auto acceptedDefault=atBoundary;
 auto hit=[&](auto,auto,auto,bool){return std::optional<NativeNavObstacle>(box);};
 advanceNativeNavigation(atBoundary,hit,{},true);advanceNativeNavigation(acceptedDefault,hit);
 need(atBoundary.candidates.empty()==(boundary>0),"native radius inside/equal/outside admission");
 need(!acceptedDefault.candidates.empty(),"accepted Spider float-radius behavior retained");
}
std::cout<<"PASS recorded original PE corner precision; native opt-in; accepted default; narrow/detour/tie/distance\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
