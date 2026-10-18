#define main retainedWalkerControllerTests
#include "ordinary_enemy_ai_world_test.cpp"
#undef main
#include "game/spider_contact_native.h"
#include "game/enemy_idle_native.h"
#include <tuple>
#include <set>
namespace {
void replace(auto& p,const std::string& key,const std::string& text){for(auto& n:p)if(n.key==key){n.values={text};return;}p.push_back(value(key,text));}
SceneContent ordinaryFixture(std::string family,std::string base="Fixed",std::string action="Seeker",bool guardian=false){
 auto s=fixture(base);auto& c=s.classes[1];c.name=family;c.behaviours={base,action};if(guardian)c.behaviours.push_back("Guardian");replace(c.properties,"Sub-type",family);replace(c.properties,"Move-during-path-search","0");replace(c.properties,"Bounding-box-size","25");replace(c.properties,"Speed-max","0");replace(c.properties,"Speed-min","0");replace(c.properties,"Two-pieces",family=="Turret"||family=="Tank"?"1":"0");replace(c.properties,"Turret-turn-timeout","75");
 for(auto& n:c.properties)if(n.kind==TextKind::Block&&n.key=="TYPE"&&!n.arguments.empty()&&n.arguments[0]=="Seeker"){
  n.arguments[0]=action;replace(n.children,"Sensor-Range",family=="Heli"||family=="Turret"?"500":"600");replace(n.children,"Scan-Delay",family=="Jeep"||family=="Roller"?"250":family=="Heli"?"400":"500");replace(n.children,"Min-distance",family=="Jeep"?"350":family=="Roller"?"300":family=="Heli"?"250":"400");
 }
 auto pair=[](std::string key){auto n=value(key,"-1");n.values.push_back("-1");return n;};if(guardian)c.properties.push_back(block("TYPE",{pair("Location"),pair("Center"),value("Range","300")},"Guardian"));
 s.objects[1].properties=c.properties;s.objects[1].behaviours=c.behaviours;return s;
}
void moveObject(WorldSnapshot& snapshot,size_t id,ContentPosition position){snapshot.objects[id].position=position;const auto n=spiderSurfaceToNative(position.x,position.y);snapshot.actors[id].nativeMotion=SpiderMotionHistory{{n.x,n.y},position};if(snapshot.actorCells)snapshot.actorCells->relocate(id,snapshot.actorCells->coordinate(position.x,position.y),snapshot.nextNativeCellOrder++);}
}
auto turnFields(const EnemyTurnState&t){return std::tuple{t.bodyPose,t.bodyDesiredPose,t.bodyPreviousPose,t.gunPose,t.gunDesiredPose,t.gunPreviousPose,t.bodyPeriodMs,t.gunPeriodMs,t.bodyRemainingMs,t.gunRemainingMs,t.logicalHeading,t.gunLogicalHeading,t.turnCommand,t.gunCommand,t.gunPreviousCommand,t.gunInRange,t.gunStateKnown,t.fastTurn};}
int main(){try{Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);std::string e;unsigned actions=0;
 for(const std::string family:{"Jeep","Roller","Heli","Turret","Tank"})for(const std::string mode:{"Seeker","Tracker"})for(const std::string base:{"Fixed","Patrol","Random"}){
  auto scene=ordinaryFixture(family,base,mode);const auto original=scene;GameWorld w;need(w.load(scene,l,e),e);need(w.actors()[1].ordinaryAIConfig.enabled&&w.actors()[1].ordinaryAIConfig.patrolArrival==64,"remaining family selects native coupled actions and ordinary arrival radius");
  w.tick(.017f,{});const auto pending=w.snapshot();need(pending.actors[1].ordinaryAI->targeting.clock.searching&&pending.actors[1].ordinaryAI->targeting.search,"targeting starts native pending action");need(pending.actors[1].route.empty()&&!pending.actors[1].routeSearch,"no surface A* state remains on migrated family");continuation(w,original,l,3);need(w.actors()[1].ordinaryAI->targeting.clock.active,"native direct target route accepted");continuation(w,original,l,45);
  auto failed=pending;failed.actors[1].ordinaryAI->targeting.search->candidates.clear();failed.actors[1].ordinaryAI->targeting.saved.back().clock.scanRemainingMs=29;auto fs=original;GameWorld failure;need(failure.load(fs,l,e)&&failure.restoreSnapshot(failed,e),e);failure.tick(.017f,{});need(failure.actors()[1].ordinaryAI->targeting.clock.scanRemainingMs==29,"failed wave rolls back independent signed action clock");need(validMissileNativeNavigationSnapshot(failure.snapshot()),"remaining family graph ownership is valid after failure");continuation(failure,original,l,3);++actions;
 }
 for(const std::string family:{"Jeep","Roller","Heli","Turret","Tank"}){
  auto scene=ordinaryFixture(family,"Patrol");scene.objects[0].flags.invisible=true;scene.paths.clear();GameWorld w;need(w.load(scene,l,e),e);for(int tick=0;tick<45;++tick)w.tick(.017f,{});need(!w.actors()[1].ordinaryAI->base.clock.searching&&!w.actors()[1].ordinaryAI->base.clock.active&&w.actors()[1].enemyMoveCommand==0,"missing authored Patrol path stays missing without an invented route");
 }
 // Native444a40/444b80 controls include negative countdown/body carry and
 // independently retained logical gun ring. The helpers are already compared
 // against16000 unchanged-PE multipart records; these controls prove admission
 // and continuation through the migrated live World path.
 for(const std::string family:{"Turret","Tank"})for(bool range:{false,true})for(int seed=0;seed<4;++seed){
  auto scene=ordinaryFixture(family);scene.objects[0].flags.invisible=true;const auto original=scene;GameWorld w;need(w.load(scene,l,e),e);auto start=w.snapshot();auto&actor=start.actors[1];const auto p=start.objects[1].position;const auto n=spiderSurfaceToNative(p.x,p.y);actor.nativeMotion=SpiderMotionHistory{{n.x,n.y},p};auto&t=*actor.enemyTurn;t.gunInRange=range;
  if(seed==0){t.gunPose=t.gunPreviousPose=t.gunDesiredPose=1;t.gunRemainingMs=50;}
  if(seed==1){t.gunPose=t.gunPreviousPose=2;t.gunDesiredPose=14;t.gunRemainingMs=1;}
  if(seed==2){t.bodyPose=t.bodyPreviousPose=1;t.bodyDesiredPose=13;t.gunPose=t.gunPreviousPose=2;t.gunDesiredPose=14;t.gunRemainingMs=-1;}
  if(seed==3){t.gunPose=t.gunPreviousPose=24;t.gunDesiredPose=14;t.gunLogicalHeading=6;t.gunPreviousCommand=203;t.gunRemainingMs=-50;}
  start.nativeWorldRandom={1,2118850534u};auto expected=t;auto random=start.nativeWorldRandom;
  //440fe1 clears both current commands before dispatch, but not previous gun
  // command or range latch. No acquired target means no42d220 synchronization.
  expected.turnCommand=expected.gunCommand=0;advanceMultipartEnemyIdleTurn(actor.enemyTurnConfig,expected,17,[&](int low,int high){return random.inclusive(low,high);});need(w.restoreSnapshot(start,e),e);w.tick(.017f,{});need(turnFields(*w.actors()[1].enemyTurn)==turnFields(expected),"coupled stationary multipart pass preserves original gun idle controller");need(w.snapshot().nativeWorldRandom.seed==random.seed&&w.snapshot().nativeWorldRandom.counter==random.counter,"multipart controller retains accepted idle RNG draw order");
  if(seed==1)need(w.actors()[1].enemyTurn->gunRemainingMs==-16,"positive countdown subtracts and returns negative without catch-up");
  if(seed==2)need(w.actors()[1].enemyTurn->bodyPose!=1,"negative gun countdown pre-steps BODY with gun carry");
  continuation(w,original,l,30);
 }
 for(const std::string family:{"Jeep","Heli"})for(bool negativeBirth:{false,true}){
  auto scene=ordinaryFixture(family,"Fixed","Seeker",true);scene.objects[1].position=negativeBirth?ContentPosition{2000,500}:ContentPosition{1000,1000};scene.objects[0].position=negativeBirth?ContentPosition{2100,500}:ContentPosition{1500,1000};const auto original=scene;GameWorld w;need(w.load(scene,l,e),e);const auto config=w.actors()[1].ordinaryAIConfig;auto moved=w.snapshot();moveObject(moved,1,negativeBirth?ContentPosition{2400,500}:ContentPosition{1400,1000});moveObject(moved,0,negativeBirth?ContentPosition{2500,500}:ContentPosition{1500,1000});need(w.restoreSnapshot(moved,e),e);w.tick(.017f,{});const auto&route=w.actors()[1].ordinaryAI->targeting;need(route.clock.searching&&route.search,"remaining Guardian creates native action");if(negativeBirth)need(route.target==0,"negative native Guardian center follows current actor");else{need(!route.target,"positive native Guardian center remains fixed");const auto goal=nativeEnemyGuardianGoal(config.guardianHome);need(route.search->seen.front()==goal.destination&&route.sampledTarget.x==goal.sample.x&&route.sampledTarget.y==goal.sample.y,"remaining Guardian uses original fallback double transform");}continuation(w,original,l,30);
 }
 //Powered Tank controls use the original COMMON/OBJECT.3DC propulsion values;
 //zero-speed controller fixtures above cannot detect lost motion or hull turns.
 for(const std::string mode:{"Seeker","Tracker"}){
  auto scene=ordinaryFixture("Tank","Patrol",mode);auto&c=scene.classes[1];
  for(const auto&[key,text]:{std::pair{"Speed-max","35"},{"Speed-min","-35"},{"Turn-timeout","150"},{"Turret-turn-timeout","50"},{"Use-inertia","1"},{"Grip","1"},{"Friction-factor","0.97"},{"Friction-threshold","0.01"},{"Acceleration-step","5"},{"Decceleration-step","5"}})replace(c.properties,key,text);
  c.properties.push_back(block("Weight",{value("Initial","100")}));
  c.properties.push_back(block("Thrust",{value("Initial","500"),value("Min","-500"),value("Max","500")}));
  scene.objects[1].properties=c.properties;scene.objects[0].position={1700,1000};const auto original=scene;
  GameWorld w;need(w.load(scene,l,e),e);const auto before=scene.objects[1].position;float peak=0;std::set<int>poses;
  for(int tick=0;tick<240;++tick){w.tick(.017f,{});peak=std::max(peak,w.actors()[1].enemyMagnitude);poses.insert(w.actors()[1].enemyTurn->bodyPose);}
  std::cerr<<"Tank "<<mode<<" peak="<<peak<<" displacement2="<<nativeDistanceSquared(before,scene.objects[1].position)<<" poses="<<poses.size()<<" thrust="<<w.actors()[1].enemyThrust<<" move="<<w.actors()[1].enemyMoveCommand<<" target="<<w.actors()[1].ordinaryAI->targeting.target.has_value()<<" active="<<w.actors()[1].ordinaryAI->targeting.clock.active<<"\n";
  //Tank clears retained velocity after each unconstrained step. At full
  //authored thrust500/mass100, native heading length.1 and friction.97
  //yield a per-step magnitude.485; Heli's accumulated-speed bound is invalid.
  need(peak>.48f&&peak<.49f&&nativeDistanceSquared(before,scene.objects[1].position)>1600,"powered Tank native action accumulates propulsion and displacement");
  need(poses.size()>1,"powered Tank hull follows native route turning");
  need(!w.actors()[1].routeSearch&&w.actors()[1].route.empty(),"powered Tank never allocates port A*");continuation(w,original,l,30);
 }
 // Upper-piece destruction deletes native action storage; retained lower
 // pieces coast through the accepted common motion path without recreating AI.
 for(const std::string family:{"Turret","Tank"}){
  auto scene=ordinaryFixture(family);const auto original=scene;GameWorld w;need(w.load(scene,l,e),e);w.tick(.017f,{});auto moving=w.snapshot();auto&a=moving.actors[1];a.contactMotion=true;a.contactVelocity={2,1};a.enemyMagnitude=std::hypot(2.f,1.f);need(w.restoreSnapshot(moving,e),e);w.damage(1,1e9f,0);need(w.actors()[1].alive&&w.actors()[1].turretOnlyBase&&!w.actors()[1].ordinaryAI&&!w.actors()[1].ordinaryAIConfig.enabled,"retained lower piece has no recreated native action/cache admission");const auto before=scene.objects[1].position;w.tick(.017f,{});need(scene.objects[1].position.x!=before.x||scene.objects[1].position.y!=before.y,"retained lower piece preserves momentum through first post-destruction update");need(!w.actors()[1].ordinaryAI,"coasting lower piece does not allocate replacement AI");continuation(w,original,l,30);w.damage(1,1e9f,0);need(!w.actors()[1].alive&&!w.actors()[1].ordinaryAI,"second destruction disposes retained lower piece");
 }
 std::cout<<"PASS "<<actions<<" remaining ordinary coupled modes, missing paths, multipart idle, Guardian, retained-base coast and disk continuations\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
