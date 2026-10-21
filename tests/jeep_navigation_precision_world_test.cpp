#define main retainedNavigationFixtureTests
#include "ordinary_enemy_ai_world_test.cpp"
#undef main
#include "game/spider_contact_native.h"
#include <set>
#include <array>
namespace {
void set(auto& p,const std::string& key,const std::string& text){for(auto& n:p)if(n.key==key){n.values={text};return;}p.push_back(value(key,text));}
SceneContent jeep(std::string base="Random",std::string mode="Seeker"){
 auto s=fixture(base);auto& c=s.classes[1];c.name="Jeep";c.behaviours={base,mode};
 for(const auto& [key,text]:{std::pair{"Sub-type","Jeep"},{"Move-during-path-search","0"},{"Turn-timeout","60"},{"Speed-max","25"},{"Speed-min","-10"},{"Use-inertia","1"},{"Grip","1"},{"Friction-factor","0.97"},{"Friction-threshold","0.01"},{"Acceleration-step","1"},{"Decceleration-step","1"}})set(c.properties,key,text);
 c.properties.push_back(block("Weight",{value("Initial","10")}));c.properties.push_back(block("Thrust",{value("Initial","30"),value("Min","-10"),value("Max","30")}));
 for(auto& n:c.properties)if(n.key=="TYPE"&&!n.arguments.empty()&&n.arguments[0]=="Seeker"){
  n.arguments[0]=mode;set(n.children,"Sensor-Range","600");set(n.children,"Min-distance","350");set(n.children,"Scan-Delay","500");
 }
 s.objects[1].properties=c.properties;s.objects[1].behaviours=c.behaviours;return s;
}
}
int main(){try{
 Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);
 for(const auto b:std::array<NativeNavObstacle,2>{{{2274.99853515625f,-1059.0733642578125f,2289.99853515625f,-1009.0733642578125f},{2389.99853515625f,-994.739990234375f,2404.99853515625f,-944.739990234375f}}}){
  NativeNavigationDecor d;d.minX=b.minX;d.minY=b.minY;d.maxX=b.maxX;d.maxY=b.maxY;d.rectangle=true;d.cellX=40;d.cellY=20;l.nativeNavigationDecor.push_back(d);
 }
 const NativeNavPoint source{2159.99853515625f,-977.4066772460938f},target{2504.99853515625f,-1003.4066772460938f};
 // Original case355 is clear. Exercise both query entry points and the
 // transposed40fce0/40ff00 path before the live Seeker caller below.
 for(bool transpose:{false,true}){
  auto point=[&](NativeNavPoint p){return transpose?NativeNavPoint{p.y,p.x}:p;};
  std::vector<NativeNavObstacle> obstacles;NativeNavGraph graph;
  for(const auto& decor:l.nativeNavigationDecor){
   auto lo=point({decor.minX,decor.minY}),hi=point({decor.maxX,decor.maxY});
   NativeNavObstacle bounds{lo.x,lo.y,hi.x,hi.y};obstacles.push_back(bounds);
   insertNativeNavigationObject(graph,obstacles.size(),bounds);
  }
  auto a=insertNativeNavigationPoint(graph,point(source)),b=insertNativeNavigationPoint(graph,point(target));
  need(!queryNativeNavigation(obstacles,a,b,{25,25},false),"original clear line through vector query");
  need(!queryNativeNavigation(graph,a,b,{25,25},false,{}),"original clear line through graph query");
 }
 auto scene=jeep("Fixed");set(scene.classes[1].properties,"Bounding-box-size","25");scene.objects[1].properties=scene.classes[1].properties;
 for(size_t id=0;id<2;++id){auto n=id?source:target;auto p=spiderNativeToSurface(n.x,n.y);scene.objects[id].position={p.x,p.y};}
 const auto authored=scene;GameWorld w;std::string e;need(w.load(scene,l,e),e);auto checkpoint=w.snapshot();
 for(size_t id=0;id<2;++id){auto n=id?source:target;checkpoint.actors[id].nativeMotion=SpiderMotionHistory{{n.x,n.y},checkpoint.objects[id].position};}
 need(w.restoreSnapshot(checkpoint,e),e);w.tick(.017f,{});
 const auto& pending=w.actors()[1].ordinaryAI->targeting;need(pending.search.has_value(),"World begins native Jeep search");
 need(pending.search->seen[0].x==target.x&&pending.search->seen[0].y==target.y&&pending.search->seen[1].x==source.x&&pending.search->seen[1].y==source.y,"World uses exact retained native source/goal");
 {auto snapshot=w.snapshot();auto graph=*snapshot.spiderNavigation;auto search=*pending.search;
 auto query=[&](NativeNavPoint a,NativeNavPoint b,NativeNavPoint h,bool overlap){return queryNativeNavigation(graph,a,b,h,overlap,[&](uint64_t ref){return bool(ref&(uint64_t(1)<<63));});};
 auto hit=query(search.seen[1],search.seen[0],search.halfExtent,false);
 need(!hit,"live World graph preserves original clear line");
 std::cout<<"radius="<<w.actors()[1].radius<<" query="<<bool(hit)<<" max="<<search.maximumDistance<<"\n";
 if(hit)std::cout<<"hit="<<hit->minX<<','<<hit->minY<<','<<hit->maxX<<','<<hit->maxY<<"\n";
 auto raw=advanceNativeNavigation(search,query,[&](NativeNavPoint p){return insertNativeNavigationPoint(graph,p);},true);
 need(raw==NativeNavProgress::Found&&search.candidates.size()==1&&search.seen.size()==2,"original direct wave status and ordered population");
 std::cout<<"manual wave="<<int(raw)<<" candidates="<<search.candidates.size()<<" seen="<<search.seen.size()<<"\n";}
 w.tick(.017f,{});const auto& route=w.actors()[1].ordinaryAI->targeting;
 std::cout<<"World after first wave: active="<<route.clock.active<<" searching="<<route.clock.searching<<" candidates="<<(route.search?route.search->candidates.size():0)<<" seen="<<(route.search?route.search->seen.size():route.ownedPoints.size())<<"; original case355 direct active=1/seen=2\n";
 need(route.clock.active&&!route.clock.searching&&route.ownedPoints.size()==2,"native arithmetic admits the original direct route");
 continuation(w,authored,l,60);std::cout<<"60 exact memory/disk continuation ticks; synthetic geometry, no authored campaign counterexample claimed\n";
 return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
