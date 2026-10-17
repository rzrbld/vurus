#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/combat.h"
#include "game/enemy_idle_native.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={v};return n;}
static TextNode block(std::string key,std::vector<TextNode> ns,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=std::move(ns);if(!type.empty())n.arguments={type};return n;}
static ContentPosition targetPosition(int heading,float distance){const auto h=nativeHeading(heading);const float norm=std::hypot(h.x,h.y);const auto s=nativeToSurface(h.x*distance/norm,h.y*distance/norm);return {1000+s.x,1000+s.y};}
// These actively simulated classes mirror authored Managed=1; omission means
// native unmanaged scenery and must not rely on the former all-actors loop.
static SceneContent fixture(std::string subtype,bool two,float minimum=300,bool guided=false){
 SceneContent s;s.mapPath="TEST/TURN.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Managed","1"),block("Energy",{value("Initial","100000"),value("Max","100000")})};
 ObjectClass actor;actor.id={1};actor.name="Enemy";actor.behaviours={"Fixed","Seeker"};actor.properties={value("Managed","1"),value("Sub-type",subtype),value("Two-pieces",two?"1":"0"),value("Can-turn-on-place","1"),value("Turn-timeout","35"),value("Turret-turn-timeout","50"),value("Speed-max","0"),value("Bounding-box-size","0"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("TYPE",{value("Target","Player"),value("Sensor-Range","1000"),value("Min-distance",std::to_string(minimum)),value("Scan-Delay","1"),value("Vision-Angle","360")},"Seeker"),block("ADD",{value("Name","Gun")},"Weapon"),block("ADD",{value("Name","Bullet"),value("Count","100"),value("Max","100")},"Ammo")};
 ObjectClass gun;gun.id={2};gun.name="Gun";gun.properties={block("TYPE",{value("Ammo","Bullet"),value("Delay","1000"),value("Fire-Range","1000"),value("Start-distance","30")},"Weapon")};
 ObjectClass bullet;bullet.id={3};bullet.name="Bullet";bullet.behaviours=guided?std::vector<std::string>{"Straight","Tracker"}:std::vector<std::string>{"Straight"};bullet.properties={value("Managed","1"),value("Speed-max","0"),value("Life-time","10000"),block("TYPE",{value("damage","0"),value("Ignore-owner","1"),value("On-hit","1")},"Ammo")};
 if(guided)bullet.properties.push_back(block("TYPE",{value("Scan-Delay","1000"),value("Sensor-Range","2000"),value("Turn-timeout","1000")},"Tracker"));
 s.classes={player,actor,gun,bullet};
 for(unsigned i=0;i<2;++i){ObjectPlacement o;o.id={i};o.classId={i};o.position=i?ContentPosition{1000,1000}:targetPosition(4,225);o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;s.objects.push_back(o);}s.player=ObjectId{0};return s;
}
static void tickToFirstSteer(GameWorld& w,float dt,std::optional<std::pair<int,int>> facing={}){
 if(!w.actors()[1].ordinaryAIConfig.enabled){w.tick(dt,{});return;}
 for(int tick=0;tick<60;++tick){const auto& a=w.actors()[1].ordinaryAI->targeting.clock;const bool steering=a.active&&a.scanRemainingMs>0;
  // Native begin/select visits also admit multipart idle. Seed the isolated
  // turn input immediately before steering, after those real action visits.
  if(steering&&facing){auto s=w.snapshot();auto& actor=s.actors[1];actor.direction=facing->first;actor.enemyTurn=initializeEnemyTurn(actor.enemyTurnConfig,facing->first,facing->second);std::string e;need(w.restoreSnapshot(s,e),e);}
  w.tick(dt,{});if(steering)return;}
 throw std::runtime_error("native targeting never reaches its first admitted steering pass");
}
static Level level(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;return l;}
static bool same(const EnemyTurnState&a,const EnemyTurnState&b){return a.bodyPose==b.bodyPose&&a.bodyDesiredPose==b.bodyDesiredPose&&a.bodyPreviousPose==b.bodyPreviousPose&&a.gunPose==b.gunPose&&a.gunDesiredPose==b.gunDesiredPose&&a.gunPreviousPose==b.gunPreviousPose&&a.bodyPeriodMs==b.bodyPeriodMs&&a.gunPeriodMs==b.gunPeriodMs&&a.bodyRemainingMs==b.bodyRemainingMs&&a.gunRemainingMs==b.gunRemainingMs&&a.logicalHeading==b.logicalHeading&&a.orthogonalHeading==b.orthogonalHeading&&a.turnCommand==b.turnCommand&&a.lean==b.lean&&a.fastTurn==b.fastTurn&&a.gunLogicalHeading==b.gunLogicalHeading&&a.gunCommand==b.gunCommand&&a.gunPreviousCommand==b.gunPreviousCommand&&a.gunInRange==b.gunInRange&&a.gunStateKnown==b.gunStateKnown;}
static void seedFacing(GameWorld&w,int body,int gun){std::string error;auto snap=w.snapshot();auto&a=snap.actors[1];a.direction=body;a.enemyTurn=initializeEnemyTurn(a.enemyTurnConfig,body,gun);need(w.restoreSnapshot(snap,error),error);}
int main(){try{
 const auto l=level();std::string error;
 {auto s=fixture("Cop",false);GameWorld w;need(w.load(s,l,error),error);need(!w.player().enemyTurn,"player keeps separate accepted controller");
  auto expected=*w.actors()[1].enemyTurn;const auto config=w.actors()[1].enemyTurnConfig;auto random=w.snapshot().nativeWorldRandom;
  bool sawHalfPose=false;
  for(int n=0;n<24;++n){const auto before=w.snapshot();const auto& clock=before.actors[1].ordinaryAI->targeting.clock;const bool steering=clock.active&&clock.scanRemainingMs>0;w.tick(1.f/60,{});const int ms=int(std::floor(w.snapshot().elapsedMs)-std::floor(before.elapsedMs));expected.turnCommand=0;
   if(steering){advanceEnemyBodyTurn(config,expected,4,true,ms,0,0);advanceEnemyGunTurn(config,expected,4,true,ms);}

   // This stationary Fixed Seeker can admit idle after a not-due route pass;
   // original4412fe then decrements the SAME body countdown a second time.
   if(expected.turnCommand==0)advanceOnePieceEnemyIdleTurn(config,expected,ms,[&](int low,int high){return random.inclusive(low,high);});
   need(same(*w.actors()[1].enemyTurn,expected),"Cop world route/idle share native integer countdown and intermediate poses");need(w.snapshot().nativeWorldRandom.counter==random.counter,"Cop shared route/idle preserves RNG draw count");if(n<2)need(!steering,"native begin/search visits do not run route steering");if(steering&&!sawHalfPose){need(w.actors()[1].direction==0&&w.actors()[1].enemyTurn->bodyPose==2,"first admitted Cop steering remains logical heading0 at half pose2");sawHalfPose=true;}}
  need(sawHalfPose,"Cop regression observes an actual admitted half pose");
 }
 for(const std::string family:{"Tank","Turret"}){
  auto s=fixture(family,true);GameWorld w;need(w.load(s,l,error),error);
  tickToFirstSteer(w,.016f,std::pair{18,0});const auto&first=*w.actors()[1].enemyTurn;
  need(first.bodyPose==19&&first.bodyRemainingMs==0&&first.gunPose==2&&w.actors()[1].direction==18,"terminal target-facing holds hull and advances gun separately");
  need(first.turnCommand==0,"held hull cannot leak stale turn command into firing/lean");
  SaveGame save;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=w.snapshot();std::vector<uint8_t> bytes;SaveGame disk;
  need(encodeSave(save,bytes,error)&&decodeSave(bytes,disk,error),error);auto clone=fixture(family,true);GameWorld resumed;
  need(resumed.load(clone,l,error)&&resumed.restoreSnapshot(disk.levels.at(s.mapPath).world,error),error);
  need(same(first,*resumed.actors()[1].enemyTurn)&&resumed.actors()[1].enemyTurnConfig.twoPieces,"multipart disk restore retains gun clock and rebuilds class config");
  for(int n=0;n<40 && w.projectiles().empty();++n){w.tick(.016f,{});resumed.tick(.016f,{});need(same(*w.actors()[1].enemyTurn,*resumed.actors()[1].enemyTurn)&&w.projectiles().size()==resumed.projectiles().size(),"multipart gun save continuation matches poses/timers/firing");}
  need(w.projectiles().size()==1,"aligned independent gun emits projectile");
  need(w.actors()[1].direction==18&&w.actors()[1].weaponHeading()==4,"weapon heading is gun rather than hull");
  const auto&shot=w.projectiles()[0];const auto h=nativeHeading(4);const auto norm=std::hypot(h.x,h.y);
  need(std::abs(shot.directionX-h.x/norm)<.000001f&&std::abs(shot.directionZ-h.y/norm)<.000001f,"emitted projectile uses independent gun heading");
  const auto start=projectileStartOffset(4,30);need(std::abs(shot.position.x-(1000+start.x))<.001f&&std::abs(shot.position.y-(1000+start.y))<.001f,"muzzle origin uses independent gun heading");
 }
 {auto s=fixture("Tank",true,100);GameWorld w;need(w.load(s,l,error),error);tickToFirstSteer(w,.016f,std::pair{0,8});const auto&a=*w.actors()[1].enemyTurn;
  need(a.bodyPose==2&&a.gunPose==10&&a.gunPreviousPose==10&&a.gunDesiredPose==9,"route-facing hull turn carries relative gun orientation without changing gun goal");need(w.projectiles().empty(),"outside native attack window remains nonfiring");
 }
 {auto s=fixture("Turret",true,300,true);GameWorld w;need(w.load(s,l,error),error);seedFacing(w,18,0);for(int n=0;n<50&&w.projectiles().empty();++n)w.tick(.016f,{});
  need(w.projectiles().size()==1&&w.projectiles()[0].guidanceClass&&w.projectiles()[0].guidanceTarget==0,"Tracker newborn acquires player using gun ring");
  need(w.projectiles()[0].guidance.pose48==9,"Tracker newborn pose matches gunheading4 rather than hull18");
 }
 {auto original=fixture("Cop",false);auto s=original;GameWorld w;need(w.load(s,l,error),error);tickToFirstSteer(w,.016f);
  need(w.actors()[1].enemyTurn->bodyPose%2==0,"checkpoint covers intermediate body pose");SaveGame save;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=w.snapshot();std::vector<uint8_t> bytes;SaveGame disk;need(encodeSave(save,bytes,error)&&decodeSave(bytes,disk,error),error);
  GameWorld resumed;need(resumed.load(original,l,error)&&resumed.restoreSnapshot(disk.levels.at(s.mapPath).world,error),error);need(same(*w.actors()[1].enemyTurn,*resumed.actors()[1].enemyTurn),"v30 disk restore retains exact intermediate turn state");
  for(int n=0;n<120;++n){w.tick(1.f/60,{});resumed.tick(1.f/60,{});need(same(*w.actors()[1].enemyTurn,*resumed.actors()[1].enemyTurn)&&w.actors()[1].direction==resumed.actors()[1].direction,"save continuation retains turn clocks and poses");need(w.projectiles().size()==resumed.projectiles().size(),"save continuation retains firing cadence");}
  auto legacy=w.snapshot();legacy.actors[1].enemyTurn.reset();auto legacyScene=fixture("Cop",false);GameWorld migrated;
  need(migrated.load(legacyScene,l,error)&&migrated.restoreSnapshot(legacy,error),error);need(migrated.actors()[1].enemyTurn&&migrated.actors()[1].enemyTurn->bodyPose==migrated.actors()[1].direction*2+1,"legacy missing half-pose state migrates to retained logical heading");
  auto bad=w.snapshot();bad.actors[1].enemyTurn->bodyPose=49;need(!w.restoreSnapshot(bad,error),"reject invalid ordinary body pose");bad=w.snapshot();bad.actors[1].enemyTurn->logicalHeading=(bad.actors[1].direction+1)%24;need(!w.restoreSnapshot(bad,error),"reject logical heading inconsistent with actor");
 }
 std::cout<<"ordinary enemy World half-poses/countdowns, independent hull/gun firing/guidance, and v30 continuation passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
