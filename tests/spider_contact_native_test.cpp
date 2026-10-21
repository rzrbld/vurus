#include "game/spider_contact_native.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace vyruz;
bool near(float a,float b){return std::abs(a-b)<1e-5f;}
int main(){
    const auto spawn650=spiderSurfaceToNative(2015,1263);
    assert(spawn650.x==2538.49609375f && spawn650.y==285.657684326171875f);
    const auto rollback650=spiderNativeToSurface(spawn650.x,spawn650.y);
    assert(rollback650.x==2014.9998779296875f && rollback650.y==1263.f);
    assert(rollback650.x!=2015.f); // native436ac0 takes stored-velocity branch

    NativeNavGraph graph;
    graph.objects={ {0,{-10,-10,10,10},true},{1,{12,-10,32,10},true},{2,{12,-10,32,10},true} };
    graph.xEdges={{0,false},{0,true},{2,false},{1,false},{2,true},{1,true}};
    graph.yEdges={{2,false},{1,false},{0,false},{2,true},{1,true},{0,true}};
    const auto order=spiderPhysicalContactOrder(graph,0,{5,0},10);
    assert(order.size()==2 && order[0]==2 && order[1]==1);
    auto withPoint=graph;
    withPoint.objects.push_back({0,{15,0,15,0},true,true});
    withPoint.xEdges.insert(std::next(withPoint.xEdges.begin(),3),{3,false});
    withPoint.yEdges.insert(std::next(withPoint.yEdges.begin(),3),{3,false});
    assert(spiderPhysicalContactOrder(withPoint,0,{5,0},10)==order);
    // Endpoint distance minus two accepts a visible gap; no swept-path contact.
    auto p=spiderCircleContact({0,0},10,{22,0},10);
    assert(p && near(p->x,11) && near(p->z,0));
    assert(!spiderCircleContact({0,0},10,{22.01f,0},10));
    assert(!spiderCircleContact({100,0},10,{50,0},10));
    assert(spiderContactAccepted({1,0},{0,1}));
    assert(!spiderContactAccepted({-1,0},{1,0}));
    ContactVelocity edge{};
    assert(spiderCircleEdgeEndpoint({9,0},2,{10,-10},{10,10},edge));
    assert(near(edge.x,10) && near(edge.z,0));
    assert(!spiderCircleEdgeEndpoint({0,0},2,{10,-10},{10,10},edge));
    assert(spiderCircleEdgeCrossing({0,0},{20,0},2,{10,-10},{10,10},edge));
    assert(near(edge.x,10) && near(edge.z,0));
    auto square=spiderSquareContact({}, {9,0},2,{20,0},14.142136f);
    assert(square && near(square->x,10) && near(square->z,0));
    assert(!spiderSquareContact({}, {0,0},2,{20,0},14.142136f));
    assert(spiderContactAccepted({},{1,0})); // 404330 sentinel path
    // Angular quantization admits some mildly separating vectors, unlike dot>0.
    assert(spiderContactAccepted({-.05f,1},{1,0}));
    auto r=resolveSpiderActorContact({2,1},{},{0,0},{20,0},{10,0},1,1,1,1,true,true);
    assert(r.applied && near(r.first.x,0) && near(r.first.z,0));
    assert(near(r.second.x,2) && near(r.second.z,0));
    // The proposed contact can be off the rolled-back centers' connecting line.
    r=resolveSpiderActorContact({2,0},{0,2},{0,0},{20,0},{10,10},1,1,1,1,true,true);
    assert(r.applied && near(r.first.x,-1) && near(r.first.z,1));
    assert(near(r.second.x,1) && near(r.second.z,1));
    r=resolveSpiderActorContact({2,1},{},{0,0},{20,0},{10,0},1,1,.5f,1,true,false);
    assert(r.applied && near(r.first.x,-1) && near(r.first.z,0));
    r=resolveSpiderActorContact({2,0},{-2,0},{0,0},{20,0},{10,0},1,1,1,.5f,false,true);
    assert(r.applied && near(r.first.x,2) && near(r.second.x,1));
    // A stored float normal angle does not make a tangential projection
    // exactly zero. The old dot test incorrectly returned a 0.2 kick here.
    const auto tangent=resolveSpiderTerrainContact({0,.004f},{-35,0},0,1,true);
    assert(tangent.applied && !tangent.clearCommand);
    assert(std::abs(tangent.velocity.x)<1e-7f && near(tangent.velocity.z,.004f));
    const auto horizontal=resolveSpiderTerrainContact({.004f,0},{0,35},0,1,true);
    assert(horizontal.applied && near(horizontal.velocity.x,.004f) && std::abs(horizontal.velocity.z)<1e-7f);
    const auto hit=resolveSpiderTerrainContact({-.4f,0},{-35,0},4,.5f,true);
    assert(hit.applied && near(hit.velocity.x,.2f) && std::abs(hit.velocity.z)<1e-6f);
    const auto neutral=resolveSpiderTerrainContact({},{-35,0},4,1,false);
    assert(!neutral.applied);
    const auto zeroThrust=resolveSpiderTerrainContact({},{-35,0},0,1,true);
    assert(zeroThrust.applied && zeroThrust.clearCommand && near(zeroThrust.velocity.x,.2f) && zeroThrust.velocity.z==0);
    const auto synthetic=resolveSpiderTerrainContact({},{-35,0},4,1,true);
    assert(synthetic.applied && synthetic.clearCommand && near(synthetic.velocity.x,1.2f));

}
