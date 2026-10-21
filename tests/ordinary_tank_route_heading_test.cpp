#define main retainedTankWalkerTests
#include "ordinary_enemy_ai_world_test.cpp"
#undef main
#include "game/spider_contact_native.h"
namespace {
void setTank(auto& p,const std::string& key,const std::string& text){
 for(auto& n:p)if(n.key==key){n.values={text};return;}p.push_back(value(key,text));
}
SceneContent tank(const std::string& mode,bool powered){
 auto s=fixture("Fixed");auto& c=s.classes[1];c.name="Tank";c.behaviours={"Fixed",mode};
 for(const auto&[key,text]:{std::pair{"Sub-type","Tank"},{"Two-pieces","1"},{"Move-during-path-search","0"},{"Bounding-box-size","25"},{"Speed-max",powered?"35":"0"},{"Speed-min",powered?"-35":"0"},{"Turn-timeout","150"},{"Turret-turn-timeout","50"},{"Use-inertia","1"},{"Grip","1"},{"Friction-factor","0.97"},{"Friction-threshold","0.01"},{"Acceleration-step","5"},{"Decceleration-step","5"}})setTank(c.properties,key,text);
 c.properties.push_back(block("Weight",{value("Initial","100")}));
 c.properties.push_back(block("Thrust",{value("Initial","500"),value("Min","-500"),value("Max","500")}));
 for(auto& n:c.properties)if(n.key=="TYPE"&&!n.arguments.empty()&&n.arguments[0]=="Seeker"){
  n.arguments[0]=mode;setTank(n.children,"Scan-Delay","500");setTank(n.children,"Min-distance","400");
 }
 s.objects[1].properties=c.properties;s.objects[1].behaviours=c.behaviours;return s;
}
}
int main(){try{
 Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);std::string e;
 unsigned cases=0;
 for(const std::string mode:{"Seeker","Tracker"})for(bool powered:{false,true})for(bool coincident:{false,true}){
  auto scene=tank(mode,powered);const auto authored=scene;GameWorld w;need(w.load(scene,l,e),e);
  for(int k=0;k<15;++k)w.tick(.017f,{});
  need(w.actors()[1].ordinaryAI->targeting.clock.active,"Tank route active before target return");
  const auto start=w.actors()[1].ordinaryAI->targeting.segmentStart;
  const auto home=spiderSurfaceToNative(1000,1000);
  need(start.x==home.x&&start.y==home.y,"retained Tank route starts at original actor center");
  w.setPlayerPosition(coincident?1000:1001,1000);w.tick(.017f,{});
  const auto& route=w.actors()[1].ordinaryAI->targeting;
  need(route.clock.active&&(route.status&4),"Tank retains target-facing route");
  if(coincident){
   need(route.segmentStart==route.segmentEnd,"target returns exactly to retained segment start");
   // Original42c9d0 ->4075c0 returns0 for this route vector, not sentinel24.
   need(route.desiredHeading==0,"Tank zero route vector must retain original heading0");
   const int previous=w.actors()[1].enemyTurn->gunLogicalHeading;
   for(int tick=0;tick<4;++tick)w.tick(.017f,{});
   need(w.actors()[1].enemyTurn->gunLogicalHeading!=previous,"Tank turret continues turning after zero-vector refresh");
  }else{
   need(!(route.segmentStart==route.segmentEnd)&&route.desiredHeading<24,"nearby nonzero Tank route remains valid");
   need(route.desiredHeading==spiderContactHeading({route.segmentEnd.x-start.x,route.segmentEnd.y-start.y}),"nonzero Tank heading retains existing conversion");
  }
  continuation(w,authored,l,30);++cases;
 }
 need(spiderContactHeading({0,0})==24,"contact zero-vector sentinel stays24");
 std::cout<<"PASS "<<cases<<" Tank Seeker/Tracker stationary/powered zero/nonzero route controls, turret turning,240 memory/disk continuation ticks and contact sentinel\n";
 return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
