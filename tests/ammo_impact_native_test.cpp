// World adapter checks against unchanged-original impact-probe records.
// The friend invokes only the production handoff; public snapshots and tick
// exercise pending death, persistence and the later actor visit.
#include "game/world.h"
#include "snapshot_fixture.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/properties.h"
#include "game/spider_contact_native.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace vyruz {
struct AmmoImpactNativeTestAccess {
 static void impact(GameWorld& w,Projectile p,size_t target,NativeNavPoint center){p.nativeOwnerGeneration=w.actors()[p.owner].nativeGeneration;w.applyAmmoImpact(p,target,true,center,16);}
};
}
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string val){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={val};return n;}
static TextNode block(std::string key,std::vector<TextNode> fields,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=std::move(fields);if(!type.empty())n.arguments={type};return n;}
static ContentPosition surface(float x,float z){auto p=spiderNativeToSurface(x,z);return {p.x,p.y};}
struct Expected {bool tank;float shift,distance,energy,coefficient,radius,impactEnergy;unsigned kills,fast;float laterEnergy,laterShield;bool laterBase;};
// Generated from original whole43d7d0+43cb20+43d350+43d780+43ca30 records:
// work/quirk-audit/n3-radial-damage/impact-probe/result{,-direct-only}.json.
static constexpr Expected original[]={
 {false,0.0f,10.0f,50.0f,0.20000000298023224f,64.f,-993.75f,1,1,80.0f,200.0f,true},
 {false,0.0f,10.0f,5000.0f,0.20000000298023224f,64.f,3956.25f,0,0,3956.25f,0.0f,false},
 {false,0.0f,10.0f,50.0f,0.5f,64.f,-1293.75f,1,1,80.0f,200.0f,true},
 {false,0.0f,10.0f,5000.0f,0.5f,64.f,3656.25f,0,0,3656.25f,0.0f,false},
 {false,0.0f,50.0f,50.0f,0.20000000298023224f,64.f,-368.75f,1,1,80.0f,200.0f,true},
 {false,0.0f,50.0f,5000.0f,0.20000000298023224f,64.f,4581.25f,0,0,4581.25f,0.0f,false},
 {false,0.0f,50.0f,50.0f,0.5f,64.f,-668.75f,1,1,80.0f,200.0f,true},
 {false,0.0f,50.0f,5000.0f,0.5f,64.f,4281.25f,0,0,4281.25f,0.0f,false},
 {false,25.0f,10.0f,50.0f,0.20000000298023224f,64.f,-915.625f,1,1,80.0f,200.0f,true},
 {false,25.0f,10.0f,5000.0f,0.20000000298023224f,64.f,4034.375f,0,0,4034.375f,0.0f,false},
 {false,25.0f,10.0f,50.0f,0.5f,64.f,-1215.625f,1,1,80.0f,200.0f,true},
 {false,25.0f,10.0f,5000.0f,0.5f,64.f,3734.375f,0,0,3734.375f,0.0f,false},
 {false,25.0f,50.0f,50.0f,0.20000000298023224f,64.f,-759.375f,1,1,80.0f,200.0f,true},
 {false,25.0f,50.0f,5000.0f,0.20000000298023224f,64.f,4190.625f,0,0,4190.625f,0.0f,false},
 {false,25.0f,50.0f,50.0f,0.5f,64.f,-1059.375f,1,1,80.0f,200.0f,true},
 {false,25.0f,50.0f,5000.0f,0.5f,64.f,3890.625f,0,0,3890.625f,0.0f,false},
 {true,0.0f,10.0f,50.0f,0.20000000298023224f,64.f,-993.75f,1,1,80.0f,200.0f,true},
 {true,0.0f,10.0f,5000.0f,0.20000000298023224f,64.f,3956.25f,0,0,3956.25f,0.0f,false},
 {true,0.0f,10.0f,50.0f,0.5f,64.f,-1293.75f,1,1,80.0f,200.0f,true},
 {true,0.0f,10.0f,5000.0f,0.5f,64.f,3656.25f,0,0,3656.25f,0.0f,false},
 {true,0.0f,50.0f,50.0f,0.20000000298023224f,64.f,-368.75f,1,1,80.0f,200.0f,true},
 {true,0.0f,50.0f,5000.0f,0.20000000298023224f,64.f,4581.25f,0,0,4581.25f,0.0f,false},
 {true,0.0f,50.0f,50.0f,0.5f,64.f,-668.75f,1,1,80.0f,200.0f,true},
 {true,0.0f,50.0f,5000.0f,0.5f,64.f,4281.25f,0,0,4281.25f,0.0f,false},
 {true,25.0f,10.0f,50.0f,0.20000000298023224f,64.f,-915.625f,1,1,80.0f,200.0f,true},
 {true,25.0f,10.0f,5000.0f,0.20000000298023224f,64.f,4034.375f,0,0,4034.375f,0.0f,false},
 {true,25.0f,10.0f,50.0f,0.5f,64.f,-1215.625f,1,1,80.0f,200.0f,true},
 {true,25.0f,10.0f,5000.0f,0.5f,64.f,3734.375f,0,0,3734.375f,0.0f,false},
 {true,25.0f,50.0f,50.0f,0.20000000298023224f,64.f,-759.375f,1,1,80.0f,200.0f,true},
 {true,25.0f,50.0f,5000.0f,0.20000000298023224f,64.f,4190.625f,0,0,4190.625f,0.0f,false},
 {true,25.0f,50.0f,50.0f,0.5f,64.f,-1059.375f,1,1,80.0f,200.0f,true},
 {true,25.0f,50.0f,5000.0f,0.5f,64.f,3890.625f,0,0,3890.625f,0.0f,false},
 {false,0.0f,10.0f,50.0f,0.20000000298023224f,1.f,-150.0f,1,0,80.0f,200.0f,true},
 {false,0.0f,10.0f,5000.0f,0.20000000298023224f,1.f,4800.0f,0,0,4800.0f,0.0f,false},
 {false,0.0f,10.0f,50.0f,0.5f,1.f,-450.0f,1,0,80.0f,200.0f,true},
 {false,0.0f,10.0f,5000.0f,0.5f,1.f,4500.0f,0,0,4500.0f,0.0f,false},
 {false,0.0f,50.0f,50.0f,0.20000000298023224f,1.f,-150.0f,1,0,80.0f,200.0f,true},
 {false,0.0f,50.0f,5000.0f,0.20000000298023224f,1.f,4800.0f,0,0,4800.0f,0.0f,false},
 {false,0.0f,50.0f,50.0f,0.5f,1.f,-450.0f,1,0,80.0f,200.0f,true},
 {false,0.0f,50.0f,5000.0f,0.5f,1.f,4500.0f,0,0,4500.0f,0.0f,false},
 {false,25.0f,10.0f,50.0f,0.20000000298023224f,1.f,-150.0f,1,0,80.0f,200.0f,true},
 {false,25.0f,10.0f,5000.0f,0.20000000298023224f,1.f,4800.0f,0,0,4800.0f,0.0f,false},
 {false,25.0f,10.0f,50.0f,0.5f,1.f,-450.0f,1,0,80.0f,200.0f,true},
 {false,25.0f,10.0f,5000.0f,0.5f,1.f,4500.0f,0,0,4500.0f,0.0f,false},
 {false,25.0f,50.0f,50.0f,0.20000000298023224f,1.f,-150.0f,1,0,80.0f,200.0f,true},
 {false,25.0f,50.0f,5000.0f,0.20000000298023224f,1.f,4800.0f,0,0,4800.0f,0.0f,false},
 {false,25.0f,50.0f,50.0f,0.5f,1.f,-450.0f,1,0,80.0f,200.0f,true},
 {false,25.0f,50.0f,5000.0f,0.5f,1.f,4500.0f,0,0,4500.0f,0.0f,false},
 {true,0.0f,10.0f,50.0f,0.20000000298023224f,1.f,-150.0f,1,0,80.0f,200.0f,true},
 {true,0.0f,10.0f,5000.0f,0.20000000298023224f,1.f,4800.0f,0,0,4800.0f,0.0f,false},
 {true,0.0f,10.0f,50.0f,0.5f,1.f,-450.0f,1,0,80.0f,200.0f,true},
 {true,0.0f,10.0f,5000.0f,0.5f,1.f,4500.0f,0,0,4500.0f,0.0f,false},
 {true,0.0f,50.0f,50.0f,0.20000000298023224f,1.f,-150.0f,1,0,80.0f,200.0f,true},
 {true,0.0f,50.0f,5000.0f,0.20000000298023224f,1.f,4800.0f,0,0,4800.0f,0.0f,false},
 {true,0.0f,50.0f,50.0f,0.5f,1.f,-450.0f,1,0,80.0f,200.0f,true},
 {true,0.0f,50.0f,5000.0f,0.5f,1.f,4500.0f,0,0,4500.0f,0.0f,false},
 {true,25.0f,10.0f,50.0f,0.20000000298023224f,1.f,-150.0f,1,0,80.0f,200.0f,true},
 {true,25.0f,10.0f,5000.0f,0.20000000298023224f,1.f,4800.0f,0,0,4800.0f,0.0f,false},
 {true,25.0f,10.0f,50.0f,0.5f,1.f,-450.0f,1,0,80.0f,200.0f,true},
 {true,25.0f,10.0f,5000.0f,0.5f,1.f,4500.0f,0,0,4500.0f,0.0f,false},
 {true,25.0f,50.0f,50.0f,0.20000000298023224f,1.f,-150.0f,1,0,80.0f,200.0f,true},
 {true,25.0f,50.0f,5000.0f,0.20000000298023224f,1.f,4800.0f,0,0,4800.0f,0.0f,false},
 {true,25.0f,50.0f,50.0f,0.5f,1.f,-450.0f,1,0,80.0f,200.0f,true},
 {true,25.0f,50.0f,5000.0f,0.5f,1.f,4500.0f,0,0,4500.0f,0.0f,false},
};
struct Fixture {
 SceneContent scene,blueprint;Level level;GameWorld world;std::string error;
 Fixture(const Expected& e,bool flame=false){
  scene.mapPath="TEST/IMPACT.3DG";level.mapW=level.mapH=32;level.hasTerrainCollision=true;
  ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Sub-type","Vyruz"),block("Energy",{value("Initial","100000"),value("Max","100000")})};
  ObjectClass target;target.id={1};target.name=e.tank?"Tank":"Turret";target.behaviours={"Fixed"};target.properties={value("Sub-type",e.tank?"Tank":"Turret"),value("Bounding-box-size","16"),value("Two-pieces","1"),block("Energy",{value("Initial","80"),value("Max","5000")}),block("Shield",{value("Initial","200"),value("Max","200"),value("Up-step","3")}),block("TYPE",{value("Class-name","Test-Ammo"),value("Coeff",std::to_string(e.coefficient))},"Absorption")};
  ObjectClass ammo;ammo.id={2};ammo.name="Test-Ammo";ammo.properties={value("Sub-type","Ammo"),block("TYPE",{value("Damage","1000"),value("Damage-zone",std::to_string(e.radius))},"Ammo")};
  ObjectClass smoke;smoke.id={3};smoke.name="FlameSmoke";smoke.behaviours={"Fixed"};smoke.properties={value("Sub-type","FX"),value("Uniq-at-xy","1"),block("TYPE",{},"FX")};
  for(auto* cls:{&player,&target,&ammo,&smoke})cls->properties.push_back(value("Managed","1"));
  scene.classes={player,target,ammo,smoke};
  const auto place=[&](size_t cls,ContentPosition p){ObjectPlacement o;o.id={uint32_t(scene.objects.size())};o.classId={uint32_t(cls)};o.position=p;o.properties=scene.classes[cls].properties;o.behaviours=scene.classes[cls].behaviours;scene.objects.push_back(o);};
  place(0,{100,100});place(1,surface(1500+e.distance,500));if(flame)place(3,surface(1500,500));scene.player=ObjectId{0};blueprint=scene;
  need(world.load(scene,level,error),error);world.setDebugEnemyAI(false);auto s=world.snapshot();s.actors[1].vitals.energy=e.energy;s.actors[1].vitals.shield=0;s.actors[1].ageMs=2000;s.actors[1].hudEnemy=true;s.actors[1].nativeMotion=SpiderMotionHistory{{1500+e.distance,500},s.objects[1].position};s.actors[1].shieldUpStep=3;s.spiderNavigation.reset();
  need(world.restoreSnapshot(s,error),error);world.setDebugEnemyAI(false);
 }
 Projectile shot(const Expected&e,bool flame=false)const {Projectile p;p.classId={2};p.owner=0;p.position=surface(1500,500);p.damage=1000;p.damageRadius=e.radius;p.ageMs=1000;p.directionX=1;p.directionZ=0;if(flame)p.hitEffect="FlameSmoke";return p;}
 void impact(const Expected&e,bool flame=false){auto p=shot(e,flame);AmmoImpactNativeTestAccess::impact(world,p,1,{1500+e.shift,500});}
};
static WorldSnapshot disk(WorldSnapshot s){SaveGame save,read;save.currentLevel="TEST/IMPACT.3DG";save.levels[save.currentLevel].world=std::move(s);std::vector<uint8_t> bytes;std::string error;need(encodeSave(save,bytes,error),error);need(decodeSave(bytes,read,error),error);return read.levels.at(save.currentLevel).world;}
int main(){try{
 for(size_t n=0;n<std::size(original);++n){const auto&e=original[n];Fixture f(e);f.impact(e);auto s=f.world.snapshot();const auto&a=s.actors[1];
  need(a.vitals.energy==e.impactEnergy,"original impact Energy case"+std::to_string(n));need(a.vitals.shield==0,"original impact Shield");need(a.alive&&!a.turretOnlyBase,"upper piece must survive until later actor visit");need(a.nativeDamagePending==(e.impactEnergy<=0),"deferred pending death");need(s.playerKills==e.kills&&s.fastKillCount==e.fast,"original direct/radial owner and Fast Kill counts");need(s.ammoHits==1&&s.ammoMisses==0,"impact stats precede direct phase");
  if(e.impactEnergy<=0){auto decoded=disk(s);auto copy=f.blueprint;GameWorld resumed;need(resumed.load(copy,f.level,f.error),f.error);need(resumed.restoreSnapshot(decoded,f.error),f.error);resumed.setDebugEnemyAI(false);f.world.tick(.016f,{});resumed.tick(.016f,{});const auto&live=f.world.actors()[1];const auto&saved=resumed.actors()[1];need(live.turretOnlyBase==e.laterBase&&live.vitals.energy==e.laterEnergy&&live.vitals.shield==e.laterShield,"original later upper-piece phase");need(live.alive&&!live.nativeDamagePending&&live.shieldUpStep==0,"later phase retained base and disabled regen");need(saved.alive==live.alive&&saved.turretOnlyBase==live.turretOnlyBase&&saved.vitals.energy==live.vitals.energy&&saved.vitals.shield==live.vitals.shield,"pending upper-piece disk continuation");}
 }
 // FlameSmoke uniqueness aborts direct damage, but radial and contact stats ran.
 {Fixture offscreen(original[0]);offscreen.impact(original[0]);NativeEffectCamera camera;camera.offsetX=-100000;offscreen.world.setEffectCamera(camera);offscreen.world.tick(.016f,{});need(offscreen.world.actors()[1].turretOnlyBase,"pending death precedes actor camera admission");}
 {Fixture seed(original[0]);auto scene=seed.blueprint;
  auto& p=scene.classes[1].properties;p.push_back(value("Sub-type","StaticGen"));p.push_back(value("Two-pieces","0"));
  p.push_back(block("TYPE",{value("Product","Test-Ammo"),value("Max-births","1"),value("Birth-delay","1000"),value("Birth-duration","10"),value("Active","1")},"Generator"));scene.objects[1].properties=p;
  GameWorld w;std::string error;need(w.load(scene,seed.level,error),error);auto state=w.snapshot();state.actors[1].vitals.energy=50;state.actors[1].vitals.shield=0;need(w.restoreSnapshot(state,error),error);w.setDebugEnemyAI(false);
  auto shot=seed.shot(original[0]);shot.damageRadius=1;AmmoImpactNativeTestAccess::impact(w,shot,1,{1500,500});need(w.actors()[1].nativeDamagePending,"generator has deferred lethal hit");const auto before=scene.objects.size();w.tick(.016f,{});need(!w.actors()[1].alive && scene.objects.size()==before,"pending generator dies before producing another enemy");}
 Expected flame=original[1];Fixture duplicate(flame,true);duplicate.impact(flame,true);auto d=duplicate.world.snapshot();need(d.actors[1].vitals.energy==4156.25f,"duplicate FlameSmoke suppresses direct only after radial");need(d.ammoHits==1,"duplicate FlameSmoke still counts native contact");need(duplicate.scene.objects.size()==3,"duplicate does not construct second FlameSmoke");
 // Signed direct damage can repair a radial-lethal target before its next visit.
 Expected healing=original[0];healing.coefficient=-2;Fixture repair(healing);repair.impact(healing);auto healed=repair.world.snapshot();need(healed.actors[1].vitals.energy==1206.25f&&!healed.actors[1].nativeDamagePending,"negative direct damage clears pending lethal state");need(repair.world.restoreSnapshot(healed,repair.error),"healed checkpoint is valid");repair.world.setDebugEnemyAI(false);repair.world.tick(.016f,{});need(!repair.world.actors()[1].turretOnlyBase,"healed actor is not converted next visit");
 // Preserve cell head/next visitation order across memory and disk restore.
 Fixture cells(original[1]);const auto targetPos=cells.scene.objects[1].position;const auto second=cells.world.spawn(ClassId{1},targetPos);const auto third=cells.world.spawn(ClassId{1},targetPos);auto before=cells.world.snapshot();need(before.actorCells.has_value(),"persistent cell producer exists");const auto cell=before.actorCells->coordinate(targetPos.x,targetPos.y);need(before.actorCells->heads[cell]==third&&before.actorCells->next[third]==second&&before.actorCells->next[second]==1,"native prepend visitation order");auto decoded=disk(before);need(decoded.actorCells->heads==before.actorCells->heads&&decoded.actorCells->next==before.actorCells->next&&decoded.actorCells->cell==before.actorCells->cell,"v31 exact producer disk order");need(cells.world.restoreSnapshot(decoded,cells.error),cells.error);
 auto bad=decoded;bad.actorCells->next[third]=third;need(!cells.world.restoreSnapshot(bad,cells.error),"reject cell cycle");bad=decoded;bad.actorCells->heads[cell]=bad.actors.size();need(!cells.world.restoreSnapshot(bad,cells.error),"reject invalid cell actor reference");bad=decoded;bad.actorCells->relocate(1,0);need(!cells.world.restoreSnapshot(bad,cells.error),"reject structurally valid but misplaced cell membership");bad=decoded;bad.actors[1].nativeDamagePending=true;need(!cells.world.restoreSnapshot(bad,cells.error),"reject positive-Energy pending death");
 // A persisted linked list must feed every nonselected radial recipient.
 auto recipients=decoded;for(const auto id:{size_t(1),second,third}){recipients.actors[id].vitals.energy=5000;recipients.actors[id].vitals.shield=0;recipients.actors[id].nativeMotion=SpiderMotionHistory{{1510,500},targetPos};}
 need(cells.world.restoreSnapshot(recipients,cells.error),cells.error);cells.impact(original[1]);auto visited=cells.world.snapshot();need(visited.actors[1].vitals.energy==3956.25f,"selected cell member receives radial then direct");need(visited.actors[second].vitals.energy==4156.25f&&visited.actors[third].vitals.energy==4156.25f,"every nonselected persisted cell member receives original radial damage");
 // 440ace..440bb7 runs Ammo off/on clocks before440836 consumes lethal
 // Energy. A live carrier's pending death belongs to its own projectile visit,
 // even if an earlier projectile in that same pass just killed it. Descending
 // slope and a due CREATE deliberately expose premature ground/motion work.
 for(bool killedDuringProjectilePass:{false,true}){
  SceneContent scene;scene.mapPath="TEST/IMPACT.3DG";
  ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Sub-type","Vyruz"),block("Energy",{value("Initial","10000"),value("Max","10000")})};
  ObjectClass carrier;carrier.id={1};carrier.name="Off-Phase-Ammo";carrier.behaviours={"Straight"};
  carrier.properties={value("Sub-type","Ammo"),value("Speed-max","300"),value("Bounding-box-size","1"),value("Altitude","10"),block("Energy",{value("Initial","50"),value("Max","50")}),block("TYPE",{value("Damage","1"),value("On-hit","1")},"Ammo"),block("CREATE",{value("Name","Premature-FX"),value("Delay","0"),value("Repeat-timeout","1"),value("At-current-position","1")})};
  ObjectClass attacker;attacker.id={2};attacker.name="Radial-Attacker";attacker.properties={value("Sub-type","Ammo"),block("TYPE",{value("Damage","1000"),value("Damage-zone","64")},"Ammo")};
  ObjectClass fx;fx.id={3};fx.name="Premature-FX";fx.behaviours={"Fixed"};fx.properties={value("Sub-type","FX"),value("Life-time","1000"),block("TYPE",{},"FX")};
  for(auto* cls:{&player,&carrier,&attacker,&fx})cls->properties.push_back(value("Managed","1"));
  scene.classes={player,carrier,attacker,fx};
  for(unsigned id=0;id<2;++id){ObjectPlacement o;o.id={id};o.classId={id};o.position=id?surface(1500,500):ContentPosition{100,100};o.properties=scene.classes[id].properties;o.behaviours=scene.classes[id].behaviours;if(id)o.owner=ObjectId{0};scene.objects.push_back(o);}scene.player=ObjectId{0};const auto blueprint=scene;
  Level level;level.mapW=level.mapH=32;level.hasTerrainCollision=true;GameWorld world;std::string error;need(world.load(scene,level,error),error);world.setDebugEnemyAI(false);
  Projectile off;off.classId={1};off.visual=1;off.owner=0;off.position=surface(1500,500);off.ageMs=2000;off.radius=1;
  off.phaseInitialized=off.phaseConfigReady=true;off.phaseHidden=true;off.inactiveMs=100;
  off.slopeEnabled=true;off.slopeRadians=-std::acos(-1.f)*.5f;off.slopeHeight=off.slopeAltitude=10;
  off.motionConfig.speedMax=300;off.motionConfig.inertia=true;off.motionConfig.grip=off.motionConfig.frictionX=off.motionConfig.frictionZ=1;off.motionConfig.thresholdX=off.motionConfig.thresholdZ=0;
  off.motion.velocityX=45;off.motion.velocityZ=-45;off.directionX=.7071067811865475f;off.directionZ=-off.directionX;
  Projectile damage;damage.classId={2};damage.owner=0;damage.position=off.position;damage.ageMs=1000;damage.damage=1000;damage.damageRadius=64;damage.phaseInitialized=true;damage.fuseMs=1;damage.damageRemainingMs=0;
  need(scene.objects.size()==3 && scene.objects[2].classId.value==3,"fresh Ammo constructor already emits zero-delay FX before off-phase control");
  const auto constructorObjects=scene.objects.size();
  auto state=world.snapshot();state.actors[1].ageMs=2000;state.actors[1].nativeMotion=SpiderMotionHistory{{1500,500},off.position};
  state.projectiles.clear();
  if(killedDuringProjectilePass)vyruz_test::appendProjectile(state,scene,damage);
  vyruz_test::appendProjectile(state,scene,off);state.spiderNavigation.reset();
  // The native mixed manager, not Projectile vector order, defines the
  // controlled earlier-Ammo/later-carrier visit in this regression.
  if(killedDuringProjectilePass){auto& awake=*state.nativeAwake;awake.manager.entries={0,NativeAwakeAmmoBit|state.projectiles[0].nativeBodyIdentity,1,2};awake.actors[0]=0;awake.actors[1]=2;awake.actors[2]=3;awake.ammo[state.projectiles[0].nativeBodyIdentity]=1;state.projectiles[0].nativeBareBody->awakeIndex=1;}
  need(world.restoreSnapshot(state,error),error);world.setDebugEnemyAI(false);
  if(!killedDuringProjectilePass)AmmoImpactNativeTestAccess::impact(world,damage,1,{1500,500});
  const auto origin=scene.objects[1].position;std::optional<SceneContent> resumedScene;GameWorld resumed;
  for(unsigned tick=0;tick<4;++tick){world.tick(.02f,{});
   need(world.actors()[1].alive&&world.actors()[1].nativeDamagePending&&world.actors()[1].vitals.energy<=0,"off-phase lethal Ammo stays alive until its activation");
   need(world.projectiles().size()==1&&world.projectiles()[0].inactiveMs==100-20.f*(tick+1),"lethal Ammo off clock continues on its own projectile visit");
   need(world.projectiles()[0].position.x==origin.x&&world.projectiles()[0].position.y==origin.y&&world.projectiles()[0].slopeHeight==10&&world.projectiles()[0].slopeAltitude==10,"off-phase lethal Ammo does not move or descend");
   need(scene.objects.size()==constructorObjects,"off-phase lethal Ammo emits no additional due CREATE or impact FX after constructor birth");
   if(tick==1){resumedScene=blueprint;need(resumed.load(*resumedScene,level,error),error);need(resumed.restoreSnapshot(disk(world.snapshot()),error),error);resumed.setDebugEnemyAI(false);}
   else if(tick>1){resumed.tick(.02f,{});need(resumed.actors()[1].alive&&resumed.actors()[1].nativeDamagePending&&resumed.projectiles()[0].inactiveMs==world.projectiles()[0].inactiveMs,"off-phase pending death resumes its exact disk clock");}
  }
  world.tick(.02f,{});resumed.tick(.02f,{});
  need(!world.actors()[1].alive&&!world.actors()[1].nativeDamagePending&&world.projectiles().empty(),"activation consumes lethal Ammo before slope/propulsion");
  need(!resumed.actors()[1].alive&&resumed.projectiles().empty(),"restored off-phase pending Ammo dies at same activation");
  need(scene.objects.size()==constructorObjects&&resumedScene->objects.size()==constructorObjects&&scene.objects[1].position.x==origin.x&&scene.objects[1].position.y==origin.y,"activation death creates no terminal impact or due CREATE and preserves position");
 }
 std::cout<<"PASS 64 original impact states, deferred upper-piece/disk continuation, retained origin, duplicate FlameSmoke, signed health repair, cell visitation/order and corrupt restore\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
