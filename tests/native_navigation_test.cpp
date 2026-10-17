#include "game/native_navigation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <random>
using namespace vyruz;
int main(){
 std::vector<NativeNavObstacle> obstacles;
 auto query=[&](NativeNavPoint a,NativeNavPoint b,NativeNavPoint h,bool overlap){return queryNativeNavigation(obstacles,a,b,h,overlap);};
 auto s=beginNativeNavigation({0,0},{100,0},{10,10});
 assert(advanceNativeNavigation(s,query)==NativeNavProgress::Found);
 auto direct=selectNativeNavigation(s,query);assert(direct&&direct->points.size()==2);
 obstacles.push_back({40,-20,60,20});
 s=beginNativeNavigation({0,0},{100,0},{10,10});
 assert(advanceNativeNavigation(s,query)==NativeNavProgress::Working);
 assert(s.candidates.size()==2);assert((s.candidates[0].points[1]==NativeNavPoint{22,38}));
 std::optional<NativeNavCandidate> route;
 for(int n=0;n<20;++n){auto p=advanceNativeNavigation(s,query);if(p==NativeNavProgress::Found||p==NativeNavProgress::Complete){route=selectNativeNavigation(s,query);break;}}
 assert(route&&route->points.size()==4);for(size_t i=1;i<route->points.size();++i)assert(!query(route->points[i-1],route->points[i],{10,10},false));
 auto corners=nativeNavigationCorners({0,0},{100,0},{10,10},obstacles[0]);assert(corners.size()==2);assert((corners[0]==NativeNavPoint{22,-38}));
 // Native shortcut selects the farthest clear node, not first clear node.
 obstacles.clear();s={};s.halfExtent={10,10};s.candidates={{{{0,0},{0,10},{30,10},{30,0}},3}};
 route=selectNativeNavigation(s,query);assert(route&&route->points.size()==2);
 // Native route score is sum of squared distances from first node.
 obstacles={{5,-100,6,100}};
 // Body-overlap query scans boundary events, so a body entirely inside a
 // gigantic obstacle with no X edge inside its extents reports no overlap.
 assert(!query({50,0},{100,0},{10,10},true));
 obstacles={{40,-20,60,20}};
 assert(query({35,0},{100,0},{10,10},true));
 NativeNavGraph graph;
 auto first=insertNativeNavigationObject(graph,1,{10,-10,20,10});
 auto second=insertNativeNavigationObject(graph,2,{30,-10,40,10});
 updateNativeNavigationObject(graph,second,{10,-10,20,10});
 // Moving onto an equal coordinate keeps list history, unlike rebuilding.
 assert(graph.xEdges[0].object==first);
 auto oldOrder=graph.xEdges;
 updateNativeNavigationObject(graph,first,{10,-10,20,10});
 assert(graph.xEdges[0].object==oldOrder[0].object);
 removeNativeNavigationObject(graph,first);assert(!graph.objects[first].alive);
 for(auto e:graph.xEdges)assert(e.object!=first);
 // 40e630 treats a minimum at head as ordered when its maximum remains
 // ordered: retain this executable quirk and accept its structural state.
 NativeNavGraph headGraph;
 auto head=insertNativeNavigationObject(headGraph,10,{0,0,100,100});
 insertNativeNavigationObject(headGraph,11,{1,10,10,20});
 updateNativeNavigationObject(headGraph,head,{2,0,102,100});
 assert(headGraph.xEdges.front().object==head);
 assert(validNativeNavigationGraph(headGraph));
 NativeNavGraph pointGraph;
 auto p0=insertNativeNavigationPoint(pointGraph,{0,0});
 insertNativeNavigationObject(pointGraph,3,{0,-10,0,10});
 // Boundaries inserted later at equal X lie BEFORE the existing start node.
 assert(!queryNativeNavigation(pointGraph,p0,{10,0},{1,1},false,{}));
 assert(validNativeNavigationGraph(pointGraph));
 auto broken=pointGraph;broken.xEdges.push_back(broken.xEdges.front());assert(!validNativeNavigationGraph(broken));
 broken=pointGraph;broken.xEdges[0].object=999;assert(!validNativeNavigationGraph(broken));
 auto fresh=insertNativeNavigationPoint(pointGraph,{0,0});
 assert(queryNativeNavigation(pointGraph,fresh,{10,0},{1,1},false,{}));
 auto retired=fresh.graphNode;removeNativeNavigationObject(pointGraph,retired);auto reused=insertNativeNavigationPoint(pointGraph,{2,3});assert(reused.graphNode==retired);assert(validNativeNavigationGraph(pointGraph));
 std::mt19937 rng(17);
 for(int run=0;run<1000;++run){NativeNavGraph g;std::vector<NativeNavObstacle> obs;
  for(int i=0;i<12;++i){float x=float(int(rng()%40)-20),y=float(int(rng()%40)-20);obs.push_back({x,y,x+float(rng()%8),y+float(rng()%8)});insertNativeNavigationObject(g,i,obs.back());}
  NativeNavPoint a{float(int(rng()%40)-20),float(int(rng()%40)-20)},b{float(int(rng()%40)-20),float(int(rng()%40)-20)};
  for(bool overlap:{false,true}){auto one=queryNativeNavigation(obs,a,b,{5,5},overlap),two=queryNativeNavigation(g,a,b,{5,5},overlap,{});assert(bool(one)==bool(two));if(one)assert(one->minX==two->minX&&one->minY==two->minY&&one->maxX==two->maxX&&one->maxY==two->maxY);}
 }
 std::cout<<"native navigation checks passed\n";
}
