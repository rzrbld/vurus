// Counterexamples from verified original42c9d0/4075c0 and40f9b0/40fbc0.
#define main retainedWalkerTests
#include "ordinary_enemy_ai_world_test.cpp"
#undef main
static void headingRegression(){
 Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);std::string e;
 auto s=fixture("Fixed");const auto authored=s;GameWorld w;need(w.load(s,l,e),e);
 for(int k=0;k<15;++k)w.tick(.017f,{});
 need(w.actors()[1].ordinaryAI->targeting.clock.active,"active Walker route before target returns");
 const auto start=w.actors()[1].ordinaryAI->targeting.segmentStart;
 w.setPlayerPosition(1000,1000);
 const auto target=spiderSurfaceToNative(1000,1000);
 need(start.x==target.x&&start.y==target.y,"target returns exactly to retained route start");
 w.tick(.017f,{});
 const auto& route=w.actors()[1].ordinaryAI->targeting;
 std::cout<<"after return: active="<<route.clock.active<<" status="<<route.status<<" desired="<<route.desiredHeading<<" body="<<w.actors()[1].direction<<" start="<<route.segmentStart.x<<','<<route.segmentStart.y<<" end="<<route.segmentEnd.x<<','<<route.segmentEnd.y<<'\n';
 need(route.clock.active&&(route.status&4)&&route.desiredHeading==0,"native zero-vector route heading is0");
 // Heading is consumed only when the native turn countdown becomes due.
 need(w.actors()[1].enemyTurn.has_value(),"Walker has native turn state");
 const int untilTurn=std::max(1,(w.actors()[1].enemyTurn->bodyRemainingMs+16)/17);
 for(int tick=0;tick<untilTurn;++tick)w.tick(.017f,{});
 need(w.actors()[1].enemyTurn->bodyDesiredPose==1,"due live turn consumes native heading0");
 continuation(w,authored,l,30);
 std::cout<<"PASS native zero-vector heading;30 memory/disk continuation ticks\n";
}
#include <array>
static void queryRegression(){
 Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);
 for(const auto b:std::array<NativeNavObstacle,2>{{{2274.99853515625f,-1059.0733642578125f,2289.99853515625f,-1009.0733642578125f},{2389.99853515625f,-994.739990234375f,2404.99853515625f,-944.739990234375f}}}){
  NativeNavigationDecor d;d.minX=b.minX;d.minY=b.minY;d.maxX=b.maxX;d.maxY=b.maxY;d.rectangle=true;d.cellX=40;d.cellY=20;l.nativeNavigationDecor.push_back(d);
 }
 const NativeNavPoint source{2159.99853515625f,-977.4066772460938f},target{2504.99853515625f,-1003.4066772460938f};
 auto scene=fixture("Fixed");
 for(size_t id=0;id<2;++id){auto n=id?source:target;auto p=spiderNativeToSurface(n.x,n.y);scene.objects[id].position={p.x,p.y};}
 const auto authored=scene;GameWorld w;std::string e;need(w.load(scene,l,e),e);auto checkpoint=w.snapshot();
 for(size_t id=0;id<2;++id){auto n=id?source:target;checkpoint.actors[id].nativeMotion=SpiderMotionHistory{{n.x,n.y},checkpoint.objects[id].position};}
 need(w.restoreSnapshot(checkpoint,e),e);w.tick(.017f,{});
 const auto& pending=w.actors()[1].ordinaryAI->targeting;need(pending.search.has_value(),"World begins native Walker search");
 need(pending.search->seen[0].x==target.x&&pending.search->seen[0].y==target.y&&pending.search->seen[1].x==source.x&&pending.search->seen[1].y==source.y,"World uses exact retained native source/goal");
 {auto snapshot=w.snapshot();auto graph=*snapshot.spiderNavigation;auto search=*pending.search;
 auto query=[&](NativeNavPoint a,NativeNavPoint b,NativeNavPoint h,bool overlap){return queryNativeNavigation(graph,a,b,h,overlap,[&](uint64_t ref){return bool(ref&(uint64_t(1)<<63));});};
 auto hit=query(search.seen[1],search.seen[0],search.halfExtent,false);
 need(!hit,"original clear line at Walker footprint30");
 std::cout<<"radius="<<w.actors()[1].radius<<" query="<<bool(hit)<<" max="<<search.maximumDistance<<"\n";
 if(hit)std::cout<<"hit="<<hit->minX<<','<<hit->minY<<','<<hit->maxX<<','<<hit->maxY<<"\n";
 auto raw=advanceNativeNavigation(search,query,[&](NativeNavPoint p){return insertNativeNavigationPoint(graph,p);},true);
 need(raw==NativeNavProgress::Found&&search.candidates.size()==1&&search.seen.size()==2,"original direct Walker wave and ordered points");
 std::cout<<"manual wave="<<int(raw)<<" candidates="<<search.candidates.size()<<" seen="<<search.seen.size()<<"\n";}
 w.tick(.017f,{});const auto& route=w.actors()[1].ordinaryAI->targeting;
 std::cout<<"World after first wave: active="<<route.clock.active<<" searching="<<route.clock.searching<<" candidates="<<(route.search?route.search->candidates.size():0)<<" seen="<<(route.search?route.search->seen.size():route.ownedPoints.size())<<"; original case355 direct active=1/seen=2\n";
 need(route.clock.active&&!route.clock.searching&&route.ownedPoints.size()==2,"native query admits original direct Walker route");
 continuation(w,authored,l,60);std::cout<<"60 exact memory/disk continuation ticks; synthetic geometry, no authored campaign counterexample claimed\n";
}
int main(int argc,char**argv){try{need(argc==2,"usage: walker_navigation_world_test heading|query");if(std::string(argv[1])=="heading")headingRegression();else if(std::string(argv[1])=="query")queryRegression();else need(false,"unknown regression");return 0;}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
