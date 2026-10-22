#include "game/native_navigation.h"
#include <iostream>
#include <string>
using namespace vyruz;
namespace {
int checks=0,failures=0;
void check(bool ok,const std::string& label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
NativeNavPoint transform(NativeNavPoint p,bool y,int sign){p.x*=sign;return y?NativeNavPoint{p.y,p.x}:p;}
NativeNavObstacle transform(NativeNavObstacle b,bool y,int sign){auto lo=transform(NativeNavPoint{b.minX,b.minY},y,sign),hi=transform(NativeNavPoint{b.maxX,b.maxY},y,sign);return {std::min(lo.x,hi.x),std::min(lo.y,hi.y),std::max(lo.x,hi.x),std::max(lo.y,hi.y)};}
bool same(std::optional<NativeNavObstacle> a,NativeNavObstacle b){return a&&a->minX==b.minX&&a->minY==b.minY&&a->maxX==b.maxX&&a->maxY==b.maxY;}
}
int main(){
 // Raw40e630 preserves a head-min (or mirrored tail-max) even when it is
 // unordered. Raw40f9b0/40fce0 traverse links from the source, not the head.
 for(bool y:{false,true})for(int sign:{-1,1})for(bool moved:{false,true})for(bool lateSource:{false,true}){
  NativeNavGraph g;auto t=[&](NativeNavObstacle b){return transform(b,y,sign);};
  auto head=insertNativeNavigationObject(g,10,t({0,-2,100,2}));
  const auto obstacle=t({1,-1,10,1});insertNativeNavigationObject(g,11,obstacle);
  NativeNavPoint a,b;
  auto points=[&]{a=insertNativeNavigationPoint(g,transform(NativeNavPoint{5,0},y,sign));b=insertNativeNavigationPoint(g,transform(NativeNavPoint{15,0},y,sign));};
  if(!lateSource)points();
  if(moved)updateNativeNavigationObject(g,head,t({20,-2,120,2}));
  if(lateSource)points();
  auto label="line axis="+std::to_string(y)+" sign="+std::to_string(sign)+" moved="+std::to_string(moved)+" late-source="+std::to_string(lateSource);
  check(validNativeNavigationGraph(g),label+" graph membership");
  // Inserting points after the bad head update places source ahead of the
  // displaced endpoint20. goal15 + extent6 must permit traversing past20.
  check(same(queryNativeNavigation(g,a,b,{6,6},false,{}),obstacle),label);
  check(!queryNativeNavigation(g,a,b,{6,6},false,[](uint64_t ref){return ref!=11;}),label+" filter");
  if(moved&&!lateSource){auto detached=a;detached.graphNode=SIZE_MAX;check(!queryNativeNavigation(g,detached,b,{6,6},false,{}),label+" source-less legacy control");}
 }
 // A new source inserted beyond the displaced tail-max sees that endpoint
 // first.40fade subtracts extent.y for decreasing X, unlike40fce0.
 {
  NativeNavGraph g;auto head=insertNativeNavigationObject(g,10,{-100,-2,0,2});
  const NativeNavObstacle obstacle{-10,-1,-1,1};insertNativeNavigationObject(g,11,obstacle);
  updateNativeNavigationObject(g,head,{-120,-2,-20,2});
  auto a=insertNativeNavigationPoint(g,{5,0}),b=insertNativeNavigationPoint(g,{-15,0});
  check(same(queryNativeNavigation(g,a,b,{1,6},false,{}),obstacle),"original decreasing-X extent.y");
 }
 // 40f840's overlap query likewise begins at source.prev, then source.next.
 for(int sign:{-1,1})for(bool moved:{false,true}){
  NativeNavGraph g;auto t=[&](NativeNavObstacle b){return transform(b,false,sign);};
  auto head=insertNativeNavigationObject(g,10,t({0,-2,100,2}));
  const auto obstacle=t({6,-1,10,1});insertNativeNavigationObject(g,11,obstacle);
  auto a=insertNativeNavigationPoint(g,transform(NativeNavPoint{5,0},false,sign));
  if(moved)updateNativeNavigationObject(g,head,t({20,-2,120,2}));
  auto label="overlap sign="+std::to_string(sign)+" moved="+std::to_string(moved);
  check(same(queryNativeNavigation(g,a,a,{6,6},true,{}),!moved&&sign==1?t({0,-2,100,2}):obstacle),label);
  auto filtered=queryNativeNavigation(g,a,a,{6,6},true,[](uint64_t ref){return ref!=11;});
  check(moved?!filtered:same(filtered,t({0,-2,100,2})),label+" filter");
  if(moved)check(same(queryNativeNavigation(g,a,a,{20,20},true,[](uint64_t ref){return ref!=11;}),t({20,-2,120,2})),label+" wrong-side-coordinate native overlap");
 }
 // 40e360 inserts equal-coordinate nodes before existing nodes. Forward
 // traversal sees the newer boundary first; backward sees the older one.
 {
  NativeNavGraph g;const NativeNavObstacle older{4,-2,6,2},newer{4,-1,6,1};
  insertNativeNavigationObject(g,1,older);insertNativeNavigationObject(g,2,newer);
  auto left=insertNativeNavigationPoint(g,{0,0}),right=insertNativeNavigationPoint(g,{10,0});
  check(same(queryNativeNavigation(g,left,right,{1,1},false,{}),newer),"equal-edge forward insertion order");
  check(same(queryNativeNavigation(g,right,left,{1,1},false,{}),older),"equal-edge backward insertion order");
  left.graphNode=right.graphNode=SIZE_MAX;
  check(same(queryNativeNavigation(g,left,right,{1,1},false,{}),newer),"sorted source-less forward control");
  check(same(queryNativeNavigation(g,right,left,{1,1},false,{}),older),"sorted source-less backward control");
 }
 // Identical fallback searches must not repeat admission work. Counters only
 // observe calls; each predicate's answer is stable throughout the query.
 for(bool y:{false,true})for(int sign:{-1,1})for(bool detached:{false,true}){
  NativeNavGraph graph;const auto obstacle=transform(NativeNavObstacle{4,-2,6,2},y,sign);
  insertNativeNavigationObject(graph,1,obstacle);
  auto from=insertNativeNavigationPoint(graph,transform(NativeNavPoint{0,0},y,sign));
  auto to=transform(NativeNavPoint{10,0},y,sign);if(detached)from.graphNode=SIZE_MAX;
  unsigned visits=0;auto label="reuse axis="+std::to_string(y)+" sign="+std::to_string(sign)+" detached="+std::to_string(detached);
  check(same(queryNativeNavigation(graph,from,to,{1,1},false,[&](uint64_t){++visits;return true;}),obstacle),label+" selected bounds");
  check(visits==1,label+" one admission visit on hit");
  visits=0;check(!queryNativeNavigation(graph,from,to,{1,1},false,[&](uint64_t){++visits;return false;}),label+" filtered miss");
  check(visits==2,label+" one pass over rejected endpoints");
  visits=0;check(same(queryNativeNavigation(graph,from,to,{1,2},false,[&](uint64_t){++visits;return true;}),obstacle),label+" unequal extent selected bounds");
  check(visits==(!y&&sign<0&&!detached?2u:1u),label+" preserve distinct decreasing-X cutoffs");
 }
 // Exact original-instruction audit fixture: source5/goal15, extent1,
 // retained X order[20,1,5,10,15,120]; native selects object11 at X10.
 NativeNavGraph g;auto head=insertNativeNavigationObject(g,10,{0,-2,100,2});
 insertNativeNavigationObject(g,11,{1,-1,10,1});auto a=insertNativeNavigationPoint(g,{5,0}),b=insertNativeNavigationPoint(g,{15,0});
 updateNativeNavigationObject(g,head,{20,-2,120,2});
 check(same(queryNativeNavigation(g,a,b,{1,1},false,{}),{1,-1,10,1}),"exact audited native fixture");
 std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
