#include "game/native_contact_graph.h"
#include <array>
#include <bit>
#include <cstdlib>
#include <iostream>
using namespace vyruz;
namespace {
void check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
// Original40e210/40e315/40e630/40e870/40e4b0 finite-history fingerprint.
// Synthetic inputs, not a Windows playthrough. Includes unordered head/tail,
// equal coordinates, pivot-crossing swaps, hinted relocation and empty rebirth.
void history(){
 NativeNavGraph graph;std::array<size_t,16> ids;ids.fill(SIZE_MAX);
 std::array<unsigned,16> kinds{};uint32_t rng=0x71942;unsigned steps=0;
 uint64_t hash=14695981039346656037ULL;
 const auto word=[&](uint32_t value){for(unsigned byte=0;byte<4;++byte){hash^=uint8_t(value>>(byte*8));hash*=1099511628211ULL;}};
 const auto random=[&](){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
 const auto key=[&](NativeNavGraphEdge edge){for(unsigned slot=0;slot<ids.size();++slot)if(ids[slot]==edge.object)return 1+slot*2+unsigned(edge.maximum);check(false,"Missing live endpoint owner");return 0u;};
 const auto operate=[&](unsigned op,unsigned slot,unsigned kind,unsigned anchor,float x,float y,float w,float h){
  const NativeNavObstacle bounds{x,y,kind==1?x:x+w,kind==1?y:y+h};
  if(op<2){kinds[slot]=kind;ids[slot]=kind==2?insertNativeNavigationObject(graph,slot+1,bounds):insertNativeNavigationPoint(graph,{x,y}).graphNode;}
  else if(op==4){removeNativeNavigationObject(graph,ids[slot]);ids[slot]=SIZE_MAX;}
  else if(op==3)updateNativeContactGraphObject(graph,ids[slot],bounds,ids[anchor]);
  else updateNativeNavigationObject(graph,ids[slot],bounds);
  check(validNativeNavigationGraph(graph),"Mixed history graph membership");
  for(bool xAxis:{true,false}){
   const auto& axis=xAxis?graph.xEdges:graph.yEdges;const auto state=axis.insertionState();
   check(state.native,"Fresh history retains native insertion policy");word(state.pivot?key(*state.pivot):0);word(std::bit_cast<uint32_t>(state.balance));word(uint32_t(axis.size()));
   for(const auto edge:axis){const auto& b=graph.objects[edge.object].bounds;word(key(edge));word(std::bit_cast<uint32_t>(xAxis?(edge.maximum?b.maxX:b.minX):(edge.maximum?b.maxY:b.minY)));}
  }++steps;
 };
 operate(0,0,2,99,0,0,100,100);
 for(unsigned slot=1;slot<5;++slot)operate(0,slot,2,99,slot*2+2,slot*2+2,0,0);
 operate(2,0,2,99,20,20,100,100);operate(1,5,1,99,9,9,0,0);
 operate(2,0,2,99,-120,-120,100,100);operate(1,6,1,99,0,0,0,0);
 for(unsigned slot=0;slot<7;++slot)operate(4,slot,kinds[slot],99,0,0,0,0);
 for(unsigned z=0;z<8;++z){const unsigned kind=z%2+1;operate(kind==2?0:1,0,kind,99,z,z,0,0);operate(2,0,kind,99,-float(z),z+1,1,2);operate(4,0,kind,99,0,0,0,0);}
 for(unsigned z=0;z<300;++z){const unsigned slot=random()%16,kind=ids[slot]!=SIZE_MAX?kinds[slot]:random()%2+1;unsigned op,anchor=99;
  const float x=float(int(random()%25)-12)*2,y=float(int(random()%25)-12)*2,w=float(random()%7),h=float(random()%7);
  if(ids[slot]==SIZE_MAX)op=kind==2?0:1;
  else {op=random()%5==0?4:2;for(unsigned j=1;j<16;++j){const auto t=(slot+j)%16;if(ids[t]!=SIZE_MAX&&kinds[t]==kind){anchor=t;break;}}if(op==2&&anchor!=99&&random()%2)op=3;}
  operate(op,slot,kind,anchor,x,y,w,h);
 }
 for(unsigned slot=0;slot<16;++slot)if(ids[slot]!=SIZE_MAX)operate(4,slot,kinds[slot],99,0,0,0,0);
 operate(1,0,1,99,0,0,0,0);operate(4,0,1,99,0,0,0,0);
 check(steps==355&&hash==0xa2988e6dbb5fc095ULL,"Original mixed insertion/pivot histories");
}
void obstacleQuery(){
 NativeNavGraph graph;const auto box=insertNativeNavigationObject(graph,1,{0,0,100,100});
 for(unsigned i=0;i<4;++i){const float x=4+i*2;insertNativeNavigationObject(graph,i+2,{x,0,x,0});}
 updateNativeNavigationObject(graph,box,{20,0,120,100});
 const auto start=insertNativeNavigationPoint(graph,{9,0}),end=insertNativeNavigationPoint(graph,{11,0});
 const auto hit=queryNativeNavigation(graph,start,end,{},false,{});
 check(hit&&hit->minX==10&&hit->maxX==10,"Native pivot prevents reproduced missed obstacle");
 const auto copy=graph;check(copy.xEdges.insertionState().pivot->object==graph.xEdges.insertionState().pivot->object,"Pivot identity survives graph copy");
}
}
int main(){history();obstacleQuery();std::cout<<"355 original pivot histories and missed-obstacle regression passed\n";}
