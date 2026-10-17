#define main retainedWalkerTests
#include "ordinary_enemy_ai_world_test.cpp"
#undef main
#include "game/spider_contact_native.h"
#include <set>
namespace {
void set(std::vector<TextNode>& p,const std::string& key,const std::string& text){for(auto& n:p)if(n.key==key){n.values={text};return;}p.push_back(value(key,text));}
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
 Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);std::string e;
 // Original42c5a0 fresh action performs begin, then a search/selection wave,
 // then steering. A nearby target cannot immediately manufacture reverse208.
 auto close=jeep();close.objects[0].position={1120,1000};const auto closeAuthored=close;GameWorld w;need(w.load(close,l,e),e);need(w.actors()[1].ordinaryAIConfig.enabled,"Jeep uses native action consumer");
 w.tick(.017f,{});need(w.actors()[1].ordinaryAI->targeting.clock.searching&&w.actors()[1].enemyMoveCommand==0,"fresh Jeep starts native route without immediate close-target reverse");need(w.actors()[1].enemyMagnitude==0,"pending zero-velocity Jeep does not manufacture propulsion");continuation(w,closeAuthored,l,1);
 need(w.actors()[1].ordinaryAI->targeting.clock.active&&w.actors()[1].enemyMoveCommand==0,"Jeep selection admits route before first steering pass");w.tick(.017f,{});need(w.actors()[1].enemyMoveCommand==208&&w.actors()[1].enemyThrust<0,"native close-target status7 emits reverse on admitted steering");continuation(w,closeAuthored,l,35);
 // Preserve ordinary inertia, unlike the special Walker/Spider/Tank reset.
 auto coastScene=jeep("Fixed");coastScene.objects[0].position={5000,2500};const auto coastAuthored=coastScene;GameWorld coast;need(coast.load(coastScene,l,e),e);auto coasting=coast.snapshot();coasting.actors[1].contactVelocity={.5f,.75f};coasting.actors[1].enemyMagnitude=std::hypot(.5f,.75f);need(coast.restoreSnapshot(coasting,e),e);coast.tick(.014f,{});
 need(coast.actors()[1].enemyMoveCommand==0&&std::abs(coast.actors()[1].contactVelocity.x-.485f)<1e-5f&&std::abs(coast.actors()[1].contactVelocity.z-.7275f)<1e-5f,"Jeep neutral successful movement retains nonzero native inertia");continuation(coast,coastAuthored,l,15);
 // Actual nonzero speed with turning: no zero-speed fixture can detect a
 // retained-momentum regression or a body pose that never advances.
 for(const std::string mode:{"Seeker","Tracker"})for(const std::string base:{"Patrol","Random"}){
  auto scene=jeep(base,mode);scene.objects[0].position={1500,1000};const auto authored=scene;GameWorld chase;need(chase.load(scene,l,e),e);const auto start=scene.objects[1].position;float peak=0;std::set<int> poses;
  for(int tick=0;tick<180;++tick){chase.tick(.017f,{});peak=std::max(peak,chase.actors()[1].enemyMagnitude);poses.insert(chase.actors()[1].enemyTurn->bodyPose);}
  need(peak>2&&nativeDistanceSquared(start,scene.objects[1].position)>1600,"Jeep admitted pursuit accumulates original nonzero propulsion");need(poses.size()>=4,"live Jeep steering visits intermediate authored direction banks");need(chase.actors()[1].route.empty()&&!chase.actors()[1].routeSearch,"Jeep route ownership does not fall back to port A*");continuation(chase,authored,l,15);
  auto baseScene=jeep(base,mode);baseScene.objects[0].position={5000,2500};const auto baseAuthored=baseScene;GameWorld action;need(action.load(baseScene,l,e),e);action.tick(.017f,{});need(action.actors()[1].ordinaryAI->base.clock.searching&&action.actors()[1].enemyMoveCommand==0,"Jeep independent base begins search with authored Move-during-path-search0");continuation(action,baseAuthored,l,35);
 }
 // Existing checkpoints can lack Jeep action state; migrate retained Patrol
 // progress without inventing lost graph identities or resetting momentum.
 auto oldScene=jeep("Patrol");GameWorld old;need(old.load(oldScene,l,e),e);auto legacy=old.snapshot();legacy.actors[1].ordinaryAI.reset();legacy.actors[1].brainState.initialized=true;legacy.actors[1].brainState.point=2;legacy.actors[1].brainState.reverse=true;legacy.actors[1].contactVelocity={2,3};need(old.restoreSnapshot(legacy,e),e);need(old.actors()[1].ordinaryAI->patrol.point==2&&old.actors()[1].ordinaryAI->patrol.reverse,"Jeep migration retains Patrol cursor/reverse");need(old.actors()[1].contactVelocity.x==2&&old.actors()[1].contactVelocity.z==3,"Jeep migration preserves current momentum");old.damage(1,1e9f,0);need(!old.actors()[1].alive&&!old.actors()[1].ordinaryAI,"Jeep terminal death releases native action ownership");need(old.restoreSnapshot(old.snapshot(),e)&&!old.actors()[1].ordinaryAI,"dead Jeep restoration cannot recreate action state");
 auto fixed=jeep("Fixed");fixed.classes[1].behaviours={"Fixed"};fixed.objects[1].behaviours={"Fixed"};GameWorld inert;need(inert.load(fixed,l,e)&&!inert.actors()[1].ordinaryAI,"non-targeting Fixed-only Jeep remains excluded");

 std::cout<<"Jeep native pending/active/close reverse, nonzero coast/pursuit/turn, bases, memory/disk and legacy/death controls passed\n";
 return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
