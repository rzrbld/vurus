#include "game/navigation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace vyruz;
int main() {
    Level level;level.mapW=8;level.mapH=8;level.hasTerrainCollision=true;
    auto route=findRoute(level,{32,32},{400,200},5);assert(route.status==RouteStatus::Found&&route.points.size()==1);
    level.collisionEdges.push_back({224,0,224,160});
    route=findRoute(level,{64,64},{400,64},5);assert(route.status==RouteStatus::Found&&route.points.size()>1);
    ContentPosition from{64,64};bool around=false;
    for(auto p:route.points){assert(terrainMoveAllowed(level,from.x,from.y,p.x,p.y,5));around|=p.y>160;from=p;}
    assert(around&&from.x==400&&from.y==64);
    assert(findRoute(level,{64,64},{400,64},5,{},1).status==RouteStatus::SearchLimit);
    NavigationSearch incremental(level,{64,64},{400,64},5);
    incremental.advance(level,{},3);
    const auto checkpoint=incremental.capture();
    assert(checkpoint.result.status==RouteStatus::Pending && !checkpoint.open.empty());
    auto independent=incremental;
    independent.advance(level,{},3);
    assert(incremental.capture().result.expanded==checkpoint.result.expanded);
    NavigationSearch restored(level,{32,32},{64,64},0);
    assert(restored.restore(level,checkpoint));
    auto corrupt=checkpoint;corrupt.nodes.front().parent=corrupt.nodes.front().index;
    assert(!restored.restore(level,corrupt));
    corrupt=checkpoint;corrupt.width++;
    assert(!restored.restore(level,corrupt));
    NavigationRoute partial;size_t previous=0,calls=0;
    previous=checkpoint.result.expanded;
    do {
        partial=incremental.advance(level,{},3);
        const auto resumed=restored.advance(level,{},3);
        assert(resumed.status==partial.status && resumed.expanded==partial.expanded && resumed.points.size()==partial.points.size());
        for(size_t i=0;i<partial.points.size();++i)
            assert(resumed.points[i].x==partial.points[i].x && resumed.points[i].y==partial.points[i].y);
        assert(partial.expanded<=previous+3);previous=partial.expanded;++calls;
    } while(partial.status==RouteStatus::Pending && calls<10000);
    assert(calls>1 && partial.status==RouteStatus::Found);
    assert(partial.points.size()>1 && partial.points.back().x==400);
    from={64,64};
    for(auto p:partial.points){assert(terrainMoveAllowed(level,from.x,from.y,p.x,p.y,5));from=p;}
    const auto finished=incremental.advance(level,{},3);
    assert(finished.expanded==partial.expanded && finished.status==RouteStatus::Found);
    NavigationSearch noWork(level,{64,64},{400,64},5);
    assert(noWork.advance(level,{},0).status==RouteStatus::Pending);
    level.collisionEdges[0].y1=256;
    route=findRoute(level,{64,64},{400,64},5);assert(route.status==RouteStatus::Unreachable&&route.points.empty());
    level.collisionEdges.clear();
    auto wall=[](ContentPosition a,ContentPosition b,float){return (a.x<224&&b.x>=224)||(a.x>=224&&b.x<224);};
    assert(findRoute(level,{64,64},{400,64},5,wall).status==RouteStatus::Unreachable);
    assert(findRoute(level,{-1,0},{400,64},5).status==RouteStatus::InvalidInput);
    // Only diagonal moves allowed dynamically: no diagonal may cross blocked
    // orthogonal neighbors, even if the direct diagonal itself is allowed.
    auto corners=[](ContentPosition a,ContentPosition b,float){return (a.x==b.x||a.y==b.y)||std::abs(a.x-b.x)>32||std::abs(a.y-b.y)>16;};
    route=findRoute(level,{16,8},{240,120},0,corners);
    assert(route.status==RouteStatus::Unreachable);
}
