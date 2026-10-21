#define main retainedWalkerTests
#include "ordinary_enemy_ai_world_test.cpp"
#undef main
#include "game/spider_contact_native.h"
#include <set>
#include <array>
namespace {
void set(auto& p,const std::string& key,const std::string& text){for(auto& n:p)if(n.key==key){n.values={text};return;}p.push_back(value(key,text));}
SceneContent roller(std::string base="Random",std::string mode="Seeker"){
 auto s=fixture(base);auto& c=s.classes[1];c.name="Roller";c.behaviours={base,mode};
 for(const auto& [key,text]:{std::pair{"Sub-type","Roller"},{"Move-during-path-search","0"},{"Turn-timeout","42"},{"Speed-max","25"},{"Speed-min","-10"},{"Use-inertia","1"},{"Grip","1"},{"Friction-factor","0.97"},{"Friction-threshold","0.01"},{"Acceleration-step","0.5"},{"Decceleration-step","1"}})set(c.properties,key,text);
 c.properties.push_back(block("Weight",{value("Initial","10")}));c.properties.push_back(block("Thrust",{value("Initial","20"),value("Min","-10"),value("Max","20")}));
 for(auto& n:c.properties)if(n.key=="TYPE"&&!n.arguments.empty()&&n.arguments[0]=="Seeker"){
  n.arguments[0]=mode;set(n.children,"Sensor-Range","600");set(n.children,"Min-distance","300");set(n.children,"Scan-Delay","250");
 }
 s.objects[1].properties=c.properties;s.objects[1].behaviours=c.behaviours;return s;
}
void checkZeroRouteHeading(const Level& l){
 std::string e;
 // Original42caa0 refreshes the endpoint from the live target;42c9d0/4075c0
 // yields heading0 when it returns to the retained start. Exercise the World
 // path with both action modes and stationary/powered motion, without route injection.
 for(const std::string mode:{"Seeker","Tracker"})for(bool powered:{false,true}){
  auto scene=roller("Fixed",mode);
  if(!powered){set(scene.classes[1].properties,"Speed-max","0");scene.objects[1].properties=scene.classes[1].properties;}
  const auto authored=scene;GameWorld w;need(w.load(scene,l,e),e);
  for(int tick=0;tick<15;++tick)w.tick(.017f,{});
  need(w.actors()[1].ordinaryAI->targeting.clock.active,"Roller route admitted before target return");
  if(powered)need(w.actors()[1].enemyMagnitude>0,"powered zero-route fixture has live propulsion");
  const auto start=w.actors()[1].ordinaryAI->targeting.segmentStart;
  w.setPlayerPosition(1000,1000);const auto target=spiderSurfaceToNative(1000,1000);
  need(start.x==target.x&&start.y==target.y,"target returns exactly to retained Roller route start");
  w.tick(.017f,{});
  const auto& route=w.actors()[1].ordinaryAI->targeting;
  need(route.clock.active&&(route.status&4)&&route.segmentStart==route.segmentEnd,"Roller close-target refresh retains zero route vector");
  need(route.desiredHeading==0,"original Roller zero route vector yields heading0 rather than sentinel24");
  const int previousHeading=w.actors()[1].direction;
  for(int tick=0;tick<4;++tick)w.tick(.017f,{});
  need(w.actors()[1].direction!=previousHeading,"Roller keeps turning after coincident endpoint refresh");
  continuation(w,authored,l,30);
 }
 need(spiderContactHeading({0,0})==24,"Roller route fix preserves contact zero-vector sentinel");
}
void checkPostAiTurnCommand(const Level& l){
 std::string e;
 // Original441367..4413b8: low previous displacement clears only the final
 // command; a powered approach must retain the stepped pose and momentum.
 {
 auto scene=roller();const auto authored=scene;GameWorld w;need(w.load(scene,l,e),e);unsigned clears=0;float peak=0;
 for(int tick=0;tick<180;++tick){
  const auto pre=w.snapshot();const auto& before=pre.actors[1];
  const auto position=pre.objects[1].position;const auto previous=before.nativeMotion?before.nativeMotion->previousSurface:position;
  const long double dx=(long double)position.x-previous.x,dy=(long double)position.y-previous.y;
  auto expected=*before.enemyTurn;expected.turnCommand=0;
  const bool active=before.ordinaryAI->targeting.clock.active&&before.ordinaryAI->targeting.clock.scanRemainingMs>0;
  if(active&&tick<8)advanceEnemyBodyTurn(before.enemyTurnConfig,expected,before.ordinaryAI->targeting.desiredHeading,false,17,before.enemyMagnitude,before.speed);
  w.tick(.017f,{});const auto& after=w.actors()[1];peak=std::max(peak,after.enemyMagnitude);
  if(active&&tick<8){
   const bool clear=after.enemyMoveCommand!=0&&dx*dx+dy*dy<.25L;
   if(clear&&expected.turnCommand)++clears;
   need(after.enemyTurn->turnCommand==(clear?0:expected.turnCommand),"powered Roller applies original post-AI command clear");
   need(after.enemyTurn->bodyPose==expected.bodyPose&&after.direction==expected.logicalHeading,"command clear preserves already advanced pose/heading");
   need(after.enemyTurn->bodyRemainingMs==expected.bodyRemainingMs,"command clear preserves body countdown");
  }
  if(after.enemyMoveCommand&&dx*dx+dy*dy<.25L)need(after.enemyTurn->turnCommand==0,"native low-displacement rule remains active while powered");
 }
 need(clears==2&&peak>2,"powered approach reaches both reproduced command clears and accumulates propulsion");
 need(nativeDistanceSquared(authored.objects[1].position,scene.objects[1].position)>1600,"Roller still moves through live World");
 continuation(w,authored,l,60);
 }
 // Strict surface-space threshold, including diagonal and exact equality.
 for(const auto delta:std::array<ContentPosition,6>{{{0,0},{.25f,.25f},{.4999f,0},{.5f,0},{.5001f,0},{.5f,.5f}}}){
  auto scene=roller();set(scene.classes[1].properties,"Speed-max","0");
  scene.objects[1].properties=scene.classes[1].properties;const auto authored=scene;
  GameWorld w;need(w.load(scene,l,e),e);w.tick(.017f,{});w.tick(.017f,{});auto state=w.snapshot();
  auto& actor=state.actors[1];need(actor.ordinaryAI->targeting.clock.active,"threshold fixture has admitted native route");
  actor.ordinaryAI->targeting.clock.scanRemainingMs=1000;actor.enemyTurn=initializeEnemyTurn(actor.enemyTurnConfig,0,0);actor.enemyTurn->bodyRemainingMs=0;
  const auto position=state.objects[1].position;actor.nativeMotion->previousSurface={position.x-delta.x,position.y-delta.y};
  const auto previous=actor.nativeMotion->previousSurface;
  const long double dx=(long double)position.x-previous.x,dy=(long double)position.y-previous.y;
  auto expected=*actor.enemyTurn;advanceEnemyBodyTurn(actor.enemyTurnConfig,expected,actor.ordinaryAI->targeting.desiredHeading,false,17,0,0);
  need(expected.turnCommand!=0,"threshold fixture requests turn");
  const int command=dx*dx+dy*dy<.25L?0:expected.turnCommand;
  need(w.restoreSnapshot(state,e),e);w.tick(.017f,{});
  need(w.actors()[1].enemyMoveCommand==200&&w.actors()[1].enemyTurn->turnCommand==command,"Roller uses original strict surface threshold before motion");
  need(w.actors()[1].enemyTurn->bodyPose==expected.bodyPose&&w.actors()[1].direction==expected.logicalHeading&&w.actors()[1].enemyTurn->bodyRemainingMs==expected.bodyRemainingMs,"threshold clear preserves pose/heading/countdown");
  continuation(w,authored,l,8);
 }
 // Native zero-movement branch clears lateral input, not stationary idle turn.
 {
  auto scene=roller("Fixed");scene.objects[0].position={5000,2500};GameWorld w;need(w.load(scene,l,e),e);
  auto state=w.snapshot();auto& actor=state.actors[1];actor.nativeMotion->previousSurface=state.objects[1].position;
  actor.enemyTurn=initializeEnemyTurn(actor.enemyTurnConfig,0,0);actor.enemyTurn->bodyDesiredPose=5;
  need(w.restoreSnapshot(state,e),e);w.tick(.017f,{});
  need(w.actors()[1].enemyMoveCommand==0&&w.actors()[1].enemyTurn->turnCommand!=0,"Roller stationary idle turn remains enabled");
 }
}
}
int main(){try{
 Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);std::string e;
 checkZeroRouteHeading(l);
 checkPostAiTurnCommand(l);
 // Original42c5a0 fresh action performs begin, then a search/selection wave,
 // then steering. A nearby target cannot immediately manufacture reverse208.
 auto close=roller();close.objects[0].position={1120,1000};const auto closeAuthored=close;GameWorld w;need(w.load(close,l,e),e);need(w.actors()[1].ordinaryAIConfig.enabled,"Roller uses native action consumer");
 w.tick(.017f,{});need(w.actors()[1].ordinaryAI->targeting.clock.searching&&w.actors()[1].enemyMoveCommand==0,"fresh Roller starts native route without immediate close-target reverse");need(w.actors()[1].enemyMagnitude==0,"pending zero-velocity Roller does not manufacture propulsion");continuation(w,closeAuthored,l,1);
 need(w.actors()[1].ordinaryAI->targeting.clock.active&&w.actors()[1].enemyMoveCommand==0,"Roller selection admits route before first steering pass");w.tick(.017f,{});need(w.actors()[1].enemyMoveCommand==208&&w.actors()[1].enemyThrust<0,"native close-target status7 emits reverse on admitted steering");continuation(w,closeAuthored,l,35);
 // Preserve ordinary inertia, unlike the special Walker/Spider/Tank reset.
 auto coastScene=roller("Fixed");coastScene.objects[0].position={5000,2500};const auto coastAuthored=coastScene;GameWorld coast;need(coast.load(coastScene,l,e),e);auto coasting=coast.snapshot();coasting.actors[1].contactVelocity={.5f,.75f};coasting.actors[1].enemyMagnitude=std::hypot(.5f,.75f);need(coast.restoreSnapshot(coasting,e),e);coast.tick(.014f,{});
 need(coast.actors()[1].enemyMoveCommand==0&&std::abs(coast.actors()[1].contactVelocity.x-.485f)<1e-5f&&std::abs(coast.actors()[1].contactVelocity.z-.7275f)<1e-5f,"Roller neutral successful movement retains nonzero native inertia");continuation(coast,coastAuthored,l,15);
 // Actual nonzero speed with turning: no zero-speed fixture can detect a
 // retained-momentum regression or a body pose that never advances.
 for(const std::string mode:{"Seeker","Tracker"})for(const std::string base:{"Patrol","Random"}){
  auto scene=roller(base,mode);scene.objects[0].position={1500,1000};const auto authored=scene;GameWorld chase;need(chase.load(scene,l,e),e);const auto start=scene.objects[1].position;float peak=0;std::set<int> poses;
  for(int tick=0;tick<180;++tick){chase.tick(.017f,{});peak=std::max(peak,chase.actors()[1].enemyMagnitude);poses.insert(chase.actors()[1].enemyTurn->bodyPose);}
  need(peak>2&&nativeDistanceSquared(start,scene.objects[1].position)>1600,"Roller admitted pursuit accumulates original nonzero propulsion");need(poses.size()>=4,"live Roller steering visits intermediate authored direction banks");need(chase.actors()[1].route.empty()&&!chase.actors()[1].routeSearch,"Roller route ownership does not fall back to port A*");continuation(chase,authored,l,15);
  auto baseScene=roller(base,mode);baseScene.objects[0].position={5000,2500};const auto baseAuthored=baseScene;GameWorld action;need(action.load(baseScene,l,e),e);action.tick(.017f,{});need(action.actors()[1].ordinaryAI->base.clock.searching&&action.actors()[1].enemyMoveCommand==0,"Roller independent base begins search with authored Move-during-path-search0");continuation(action,baseAuthored,l,35);
 }
 // Existing checkpoints can lack Roller action state; migrate retained Patrol
 // progress without inventing lost graph identities or resetting momentum.
 auto oldScene=roller("Patrol");GameWorld old;need(old.load(oldScene,l,e),e);auto legacy=old.snapshot();legacy.actors[1].ordinaryAI.reset();legacy.actors[1].brainState.initialized=true;legacy.actors[1].brainState.point=2;legacy.actors[1].brainState.reverse=true;legacy.actors[1].contactVelocity={2,3};need(old.restoreSnapshot(legacy,e),e);need(old.actors()[1].ordinaryAI->patrol.point==2&&old.actors()[1].ordinaryAI->patrol.reverse,"Roller migration retains Patrol cursor/reverse");need(old.actors()[1].contactVelocity.x==2&&old.actors()[1].contactVelocity.z==3,"Roller migration preserves current momentum");old.damage(1,1e9f,0);need(!old.actors()[1].alive&&!old.actors()[1].ordinaryAI,"Roller terminal death releases native action ownership");need(old.restoreSnapshot(old.snapshot(),e)&&!old.actors()[1].ordinaryAI,"dead Roller restoration cannot recreate action state");
 for(const std::string mode:{"Seeker","Tracker"}){
  auto failScene=roller("Random",mode);const auto authored=failScene;GameWorld failure;need(failure.load(failScene,l,e),e);failure.tick(.017f,{});auto pending=failure.snapshot();need(pending.actors[1].ordinaryAI->targeting.search.has_value(),"Roller pending native action exists before failed wave");pending.actors[1].ordinaryAI->targeting.search->candidates.clear();pending.actors[1].ordinaryAI->targeting.saved.back().clock.scanRemainingMs=29;need(failure.restoreSnapshot(pending,e),e);failure.tick(.017f,{});need(failure.actors()[1].ordinaryAI->targeting.clock.scanRemainingMs==29,"Roller failed search restores signed saved scan clock");need(validMissileNativeNavigationSnapshot(failure.snapshot()),"Roller failed search retains valid graph ownership");continuation(failure,authored,l,15);
 }
 auto missing=roller("Patrol","Tracker");missing.objects[0].flags.invisible=true;missing.paths.clear();const auto missingAuthored=missing;GameWorld noPath;need(noPath.load(missing,l,e),e);for(int tick=0;tick<60;++tick)noPath.tick(.017f,{});need(!noPath.actors()[1].ordinaryAI->base.clock.active&&!noPath.actors()[1].ordinaryAI->base.clock.searching&&noPath.actors()[1].enemyMoveCommand==0,"missing Roller Patrol route stays absent without A* fallback");continuation(noPath,missingAuthored,l,15);
 auto fixed=roller("Fixed");fixed.classes[1].behaviours={"Fixed"};fixed.objects[1].behaviours={"Fixed"};GameWorld inert;need(inert.load(fixed,l,e)&&!inert.actors()[1].ordinaryAI,"non-targeting Fixed-only Roller remains excluded");

 std::cout<<"Roller native post-AI turn threshold/idle/powered continuation, pending/active/close reverse, nonzero coast/pursuit/turn, bases, memory/disk and legacy/death controls passed\n";
 return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
