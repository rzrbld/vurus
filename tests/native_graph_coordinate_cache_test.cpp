#include "game/native_contact_graph.h"
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace vyruz;
unsigned checks=0;
void need(bool ok){++checks;if(!ok)throw std::runtime_error("Coordinate lifecycle check failed");}
void check(NativeNavGraph& g){
 for(bool x:{true,false}){auto& a=x?g.xEdges:g.yEdges;
  const auto value=[&](auto e){const auto& b=g.objects[e.object].bounds;return x?(e.maximum?b.maxX:b.minX):(e.maximum?b.maxY:b.minY);};
  a.prepareCoordinates(value);
  for(auto n=a.first();n!=NativeNavGraphAxis::none;n=a.next(n))need(std::bit_cast<uint32_t>(a.coordinate(n))==std::bit_cast<uint32_t>(value(a.edge(n))));
 }
}
int main()try{
 for(unsigned k=0;k<1000;++k){
  NativeNavGraph g;auto anchor=insertNativeNavigationObject(g,1,{-20,-20,-15,-15});
  auto id=insertNativeNavigationObject(g,2,{-5,-5,5,5});
  insertNativeNavigationPoint(g,{10,10});check(g);
  updateNativeContactGraphObject(g,id,{0,-5,10,5},anchor);check(g);
  insertNativeNavigationPoint(g,{10,11});check(g);
  updateNativeNavigationObject(g,id,{-0.f,-0.f,0.f,0.f});check(g);
  auto copy=g;check(copy);g=std::move(copy);check(g);
  removeNativeNavigationObject(g,id);check(g);
  need(insertNativeNavigationObject(g,3,{-50,-60,70,80})==id);check(g);
  // Raw fixture/restore edge construction has no known coordinates. Lazy
  // initialization must rebuild them before a subsequent cached insertion.
  auto x=g.xEdges,y=g.yEdges;g.xEdges.assign(x.begin(),x.end());g.yEdges.assign(y.begin(),y.end());
  insertNativeNavigationPoint(g,{3,4});check(g);
  auto edge=static_cast<const NativeNavGraphAxis&>(g.xEdges).front();g.xEdges.front()=edge;
  insertNativeNavigationPoint(g,{4,5});check(g);
  // External fixture bounds writes use explicit cache/hint invalidation.
  g.objects[id].bounds={1,2,30,40};g.xEdges.invalidateCoordinates();g.yEdges.invalidateCoordinates();
  g.xEdges.invalidateInsertionHint();g.yEdges.invalidateInsertionHint();
  insertNativeNavigationPoint(g,{20,20});check(g);
 }
 const float nan=std::bit_cast<float>(uint32_t(0x7fc01234));
 for(float v:{-0.f,0.f,-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::infinity(),nan}){
  NativeNavGraph g;auto id=insertNativeNavigationObject(g,1,{v,v,v,v});check(g);
  insertNativeNavigationPoint(g,{v,v});check(g);
  updateNativeNavigationObject(g,id,{nan,v,nan,v});check(g);
  insertNativeNavigationPoint(g,{0,0});check(g);
 }
 need(sizeof(NativeNavGraphAxis::Node)==20);
 std::cout<<"PASS "<<checks<<" coordinate publication/copy/restore/reuse/unordered controls; node="<<sizeof(NativeNavGraphAxis::Node)<<"\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
