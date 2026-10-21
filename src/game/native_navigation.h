#pragma once
#include <functional>
#include <cstdint>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include "game/native_graph_axis.h"
namespace vyruz {
// Coordinates and bounds are the executable's native XY plane, not surface pixels.
struct NativeNavPoint { float x=0,y=0; size_t graphNode=SIZE_MAX; bool operator==(const NativeNavPoint& b) const{return x==b.x&&y==b.y;} };
struct NativeNavObstacle { float minX=0,minY=0,maxX=0,maxY=0; };
struct NativeNavCandidate { std::vector<NativeNavPoint> points; size_t cursor=0; };
struct NativeNavState {
 std::vector<NativeNavCandidate> candidates;
 std::vector<NativeNavPoint> seen; // index zero is original destination, excluded by native dedup
 NativeNavPoint halfExtent;
 float maximumDistance=0;
};
// Must reproduce native nearest centreline obstruction and overlap ordering.
// overlap=true queries body extents at from (the preceding segment exists).
using NativeNavQuery=std::function<std::optional<NativeNavObstacle>(NativeNavPoint from,NativeNavPoint to,NativeNavPoint halfExtent,bool overlap)>;
enum class NativeNavProgress { Failed=0,Complete=1,Working=-2,Found=-1 };
using NativeNavPointFactory=std::function<NativeNavPoint(NativeNavPoint)>;
NativeNavState beginNativeNavigation(NativeNavPoint start,NativeNavPoint goal,NativeNavPoint halfExtent,float maximumDistance=0,const NativeNavPointFactory& createPoint={});
// One original 410460 iteration: visits only routes present at entry, new branches wait.
// nativeCornerPrecision also retains410250's extended squared-distance gate;
// the default preserves accepted Spider corner/radius arithmetic.
NativeNavProgress advanceNativeNavigation(NativeNavState&,const NativeNavQuery&,const NativeNavPointFactory& createPoint={},bool nativeCornerPrecision=false);
// Simplifies completed routes (411120), selects native 42d6c0 score; ties keep first.
std::optional<NativeNavCandidate> selectNativeNavigation(NativeNavState&,const NativeNavQuery&);
std::vector<NativeNavPoint> nativeNavigationCorners(NativeNavPoint from,NativeNavPoint to,NativeNavPoint halfExtent,NativeNavObstacle);
// 40f310 x87 spill/order fidelity. Opt in for migrated Missile routes; accepted
// Spider routes retain their previous corner arithmetic through the helper above.
std::vector<NativeNavPoint> nativeNavigationCornersExact(NativeNavPoint from,NativeNavPoint to,NativeNavPoint halfExtent,NativeNavObstacle);
}
namespace vyruz {
// Obstacles must be in native graph insertion order (oldest first), filtered for
// actor self/target and graph eligibility by caller. Boundaries are inserted max
// before min; the ordered graph inserts newer equal coordinates before older.
std::optional<NativeNavObstacle> queryNativeNavigation(const std::vector<NativeNavObstacle>&,NativeNavPoint from,NativeNavPoint to,NativeNavPoint halfExtent,bool overlap);
}
namespace vyruz {
struct NativeNavGraphObject { uint64_t reference=0; NativeNavObstacle bounds; bool alive=true,point=false; };
struct NativeNavGraph {
 std::vector<NativeNavGraphObject> objects;
 NativeNavGraphAxis xEdges,yEdges;
 // Derived free-slot heap preserves the existing lowest-free-ID birth order.
 std::vector<size_t> freeObjects;
 size_t indexedObjectCount=SIZE_MAX;
};
using NativeNavEligible=std::function<bool(uint64_t)>;
NativeNavPoint insertNativeNavigationPoint(NativeNavGraph&,NativeNavPoint);
size_t insertNativeNavigationObject(NativeNavGraph&,uint64_t reference,NativeNavObstacle);
void updateNativeNavigationObject(NativeNavGraph&,size_t index,NativeNavObstacle);
void removeNativeNavigationObject(NativeNavGraph&,size_t index);
std::optional<NativeNavObstacle> queryNativeNavigation(const NativeNavGraph&,NativeNavPoint from,NativeNavPoint to,NativeNavPoint halfExtent,bool overlap,const NativeNavEligible&,bool nativeIntersectionPrecision=false);
}

namespace vyruz {
// 40e630 can leave a head minimum or tail maximum out of coordinate order.
// Validate graph membership, not a stronger sortedness invariant than native.
inline bool validNativeNavigationGraph(const NativeNavGraph& g) {
 for(const auto& o:g.objects){auto b=o.bounds;if(!std::isfinite(b.minX)||!std::isfinite(b.minY)||!std::isfinite(b.maxX)||!std::isfinite(b.maxY)||b.minX>b.maxX||b.minY>b.maxY)return false;if(o.point&&(b.minX!=b.maxX||b.minY!=b.maxY))return false;}
 for(bool x:{true,false}){const auto& edges=x?g.xEdges:g.yEdges;std::vector<unsigned char> used(g.objects.size(),0);
  for(auto edge:edges){if(edge.object>=g.objects.size())return false;auto o=g.objects[edge.object];if(!o.alive||(o.point&&edge.maximum))return false;unsigned char bit=edge.maximum?2:1;if(used[edge.object]&bit)return false;used[edge.object]|=bit;}
  for(size_t i=0;i<g.objects.size();++i)if(used[i]!=(g.objects[i].alive?(g.objects[i].point?1:3):0))return false;
 }return true;
}
}
