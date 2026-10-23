#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string val){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(val)};return n;}
static TextNode block(std::string type,std::vector<TextNode> fields){TextNode n;n.kind=TextKind::Block;n.key="TYPE";n.arguments={std::move(type)};n.children=std::move(fields);return n;}
static TextNode group(std::string key,std::vector<TextNode> fields){auto n=block("",std::move(fields));n.key=std::move(key);n.arguments.clear();return n;}
static ContentPosition surface(float x,float z){const auto p=spiderNativeToSurface(x,z);return {p.x,p.y};}
namespace vyruz {
struct AmmoImpactNativeTestAccess {
 static void prepare(GameWorld& w,size_t incoming,bool owner,unsigned age,bool stale,bool ignore,bool bare=false){
  for(size_t n=0;n<w.actors_.size();++n){auto&a=w.actors_[n];a.ageMs=1000;a.contactVelocity={};a.nativeAmmoAbort=false;}
  auto&a=w.actors_[incoming];w.scene_->objects[incoming].position=surface(1500,500);
  a.nativeMotion=SpiderMotionHistory{{1500,500},surface(1490,500)};if(auto*h=a.nativeMotionHistory())*h=*a.nativeMotion;
  a.contactVelocity={20,0};a.enemyMoveCommand=200;
  auto&ammo=w.actors_[2];ammo.ageMs=age;ammo.ammoHidden=false;ammo.solid=false;
  w.scene_->objects[2].position=surface(1523,500);ammo.nativeMotion=SpiderMotionHistory{{1523,500},w.scene_->objects[2].position};
  w.scene_->objects[2].owner=owner?std::optional<ObjectId>{{uint32_t(incoming)}}:std::nullopt;
  ammo.nativeCreatorGeneration=owner?w.actors_[incoming].nativeGeneration+(stale?1u:0u):0;
  w.scene_->objects[2].properties.push_back(block("Ammo",{value("Ignore-owner",ignore?"1":"0")}));
  // The original attribute list uses its first Ammo; change that field directly.
  for(auto& node:w.scene_->objects[2].properties)if(node.key=="TYPE"&&!node.arguments.empty()&&node.arguments[0]=="Ammo")
   for(auto&field:node.children)if(field.key=="Ignore-owner")field.values={ignore?"1":"0"};
  if(bare){ammo.alive=false;Projectile p;p.classId={2};p.owner=owner?incoming:SIZE_MAX;p.position=surface(1523,500);p.radius=8;p.damage=10;p.damageRadius=1;p.ageMs=age;p.ignoreOwner=ignore;p.hitEffect="FlameSmoke";
   w.initializeBareAmmoBody(p);if(stale)++p.nativeOwnerGeneration;
   p.phaseInitialized=true;p.inactiveMs=-1;p.phaseHidden=false;w.projectiles_.push_back(p);w.rebuildBareAmmoIndex();w.synchronizeBareAmmoCell(w.projectiles_.back());}
  w.initializeActorCells();w.initializeSpiderNavigation(true);
 }
 static void line(GameWorld&w,float x,float z){auto* level=const_cast<Level*>(w.level_);level->nativeNavigationDecor={{x,z-100,x,z+100,0,0,1,false,false,false}};w.initializeSpiderNavigation(true);}
 static unsigned subtype(const GameWorld&w,size_t id){return w.nativeDamageSubtype(id);}
 static PlayerMotionConfig playerConfig(const GameWorld&w){return w.playerMotionConfig_;}
 static TerrainDisplacement step(GameWorld&w,size_t id,ContactVelocity step,float ms){w.currentTickMilliseconds_=ms;const auto d=nativeToSurface(step.x,step.z);return w.move(id,d.x,d.y,true,&step);}
 static void setupContinuation(GameWorld&w,size_t id,ContactVelocity step,unsigned behavior){
  const ContentPosition position{1600.125f,1000.375f};const auto n=spiderSurfaceToNative(position.x,position.y);
  auto& a=w.actors_[id];w.scene_->objects[id].position=position;
  a.nativeMotion=SpiderMotionHistory{{n.x,n.y},{-7,-9}};if(auto*h=a.nativeMotionHistory())*h=*a.nativeMotion;
  a.brain.seeker=(behavior&3)==1;a.brain.tracker=(behavior&3)==2;a.brain.base=(behavior&28)==12?BrainBase::Patrol:BrainBase::Fixed;
  a.contactVelocity={123,-456};a.enemyMagnitude=789;a.enemyMoveCommand=200;
  if(id==w.player_){w.motion_.velocityX=123;w.motion_.velocityZ=-456;w.motion_.magnitude=789;}
  const auto trial=spiderNativeToSurface(n.x+step.x,n.y+step.z);const auto next=spiderSurfaceToNative(trial.x,trial.y);
  const ContentPosition target=surface(next.x+3,next.y);w.scene_->objects[2].position=target;
  w.actors_[2].nativeMotion=SpiderMotionHistory{{next.x+3,next.y},target};w.actors_[2].solid=false;
  for(auto& node:w.scene_->objects[2].properties)if(node.key=="TYPE"&&!node.arguments.empty()&&node.arguments[0]=="Ammo")
   for(auto& field:node.children)if(field.key=="Hit-fx")field.values={"Product"};
  w.initializeActorCells();w.initializeSpiderNavigation(true);
 }
 static TerrainDisplacement move(GameWorld&w,size_t incoming){const ContactVelocity step{20,0};const auto d=nativeToSurface(step.x,step.z);return w.move(incoming,d.x,d.y,true,&step);}
 static void singular(GameWorld&w,size_t incoming){w.scene_->objects[2].position=surface(1522,500);w.actors_[2].nativeMotion=SpiderMotionHistory{{1522,500},w.scene_->objects[2].position};w.updateSpiderNavigation(2);(void)incoming;}
 static void impact(GameWorld&w,ContentPosition point,NativeNavPoint center,float radius){Projectile p;p.classId={2};p.owner=SIZE_MAX;p.position=point;p.damage=10;p.damageRadius=radius;p.hitEffect="FlameSmoke";w.applyAmmoImpact(p,1,true,center,16);}
 static void bodyPosition(GameWorld&w,size_t id,ContentPosition p){w.scene_->objects[id].position=p;const auto n=spiderSurfaceToNative(p.x,p.y);w.actors_[id].nativeMotion=SpiderMotionHistory{{n.x,n.y},p};w.updateSpiderNavigation(id);}
 static void playerTail(GameWorld&w){
  w.playerMotionConfig_.frictionX=w.playerMotionConfig_.frictionZ=1;w.playerMotionConfig_.thresholdX=w.playerMotionConfig_.thresholdZ=0;
  w.playerMotionConfig_.inertia=true;w.playerMotionConfig_.grip=1;w.playerMotionConfig_.deceleration=0;w.playerMotionConfig_.speedThreshold=1000;
  w.motion_.velocityX=20*(.014f/(1.f/60));w.motion_.velocityZ=0;w.motion_.thrust=0;w.motion_.magnitude=w.motion_.velocityX;w.motion_.direction=6;
  EquippedWeapon gun;gun.name="Test-Gun";gun.ammo="Contact-Ammo";gun.range=1000;w.actors_[0].weapons={gun};w.actors_[0].selectedWeapon=0;w.actors_[0].ammo[gun.ammo]={10,10};
  ActorCreate product;product.classId={4};w.actors_[0].creates={product};
 }
 static void passiveTail(GameWorld&w){auto&a=w.actors_[1];a.contactConfig.enabled=true;a.contactConfig.frictionX=a.contactConfig.frictionZ=1;a.contactConfig.grip=1;a.contactConfig.thresholdX=a.contactConfig.thresholdZ=0;a.contactConfig.inertia=true;a.contactMotion=true;ActorCreate product;product.classId={4};a.creates={product};}
};
}
struct Fixture {
 SceneContent scene;Level level;GameWorld world;
 Fixture(std::string family="Cop",std::optional<ContentPosition> duplicate={},bool wall=false){
  scene.mapPath="TEST/HANDOFF.3DG";level.mapW=level.mapH=64;
  if(wall)level.nativeNavigationDecor.push_back({1518,400,1518,600,0,0,1,false,false,false});
  for(unsigned n=0;n<5;++n){ObjectClass c;c.id={n};c.name=n==2?"Contact-Ammo":n==3?"FlameSmoke":n==4?"Product":n==0?"Player":family;
   c.properties={value("Sub-type",n==2?"Ammo":n==3?"FX":n==0?"Vyruz":n==4?"Thing":family),value("Bounding-box-size",n<3?"8":"0"),group("Energy",{value("Initial","1000"),value("Max","1000")})};
   if(n==0)c.behaviours={"Player"};else c.behaviours={"Fixed"};
   if(n==2)c.properties.push_back(block("Ammo",{value("Damage","10"),value("Damage-zone","1"),value("Hit-fx","FlameSmoke"),value("Ignore-owner","0"),value("Active-after","2000")}));
   if(n==3){c.properties.push_back(block("FX",{}));c.properties.push_back(value("Uniq-at-xy","1"));}
   scene.classes.push_back(c);
  }
  for(unsigned n=0;n<3;++n){ObjectPlacement o;o.id={n};o.classId={n};o.position={float(200+n*200),200};o.properties=scene.classes[n].properties;o.behaviours=scene.classes[n].behaviours;scene.objects.push_back(o);}
  if(duplicate){ObjectPlacement o;o.id={3};o.classId={3};o.position=*duplicate;o.properties=scene.classes[3].properties;o.behaviours=scene.classes[3].behaviours;scene.objects.push_back(o);}
  scene.player=ObjectId{0};std::string error;need(world.load(scene,level,error),error.c_str());world.setDebugEnemyAI(false);
 }
};

static std::vector<uint8_t> diskBytes(const GameWorld&w){SaveGame s;s.currentLevel="TEST/HANDOFF.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string e;need(encodeSave(s,b,e),e.c_str());return b;}
static void same(ContactVelocity a,ContactVelocity b,const char* why){need(a.x==b.x&&a.z==b.z,why);}
static ContactVelocity velocity(const GameWorld&w,size_t id){const auto s=w.snapshot();return id==w.playerIndex()?ContactVelocity{s.motion.velocityX,s.motion.velocityZ}:s.actors[id].contactVelocity;}
int main(){try{unsigned cases=0,resumes=0;
 {Fixture f;auto state=f.world.snapshot();std::string error;
  const auto before=diskBytes(f.world);
  for(auto& a:state.actors)a.nativeSubtype=0xffffffffu;
  need(f.world.restoreSnapshot(state,error),error.c_str());
  need(AmmoImpactNativeTestAccess::subtype(f.world,0)==0x18&&
       AmmoImpactNativeTestAccess::subtype(f.world,1)==0x16&&
       AmmoImpactNativeTestAccess::subtype(f.world,2)==0x10,
       "live restore rebuilds subtype cache instead of trusting transient input");
  need(diskBytes(f.world)==before,"derived subtype never changes the portable checkpoint");
 }
 for(const auto* family:{"Thing","Cop","Walker","Burner","Jeep","Roller","Heli","Turret","Spider","Tank"})
 for(unsigned behavior:{0u,1u,2u,3u,12u,13u,14u})for(float ms:{1000.f/60,1000.f/90}){
  const bool player=std::string(family)=="Cop" && behavior==3;const size_t id=player?0:1;
  Fixture f(family);f.level.hasTerrainCollision=true;const auto blueprint=f.scene;
  const ContactVelocity raw{80.125f,-7.375f};AmmoImpactNativeTestAccess::setupContinuation(f.world,id,raw,behavior);
  const auto before=f.world.snapshot();const auto old=before.actors[id].nativeMotionHistory()->center;
  const auto oldSurface=spiderNativeToSurface(old.x,old.y);
  const auto trial=spiderNativeToSurface(old.x+raw.x,old.y+raw.z);const auto proposed=spiderSurfaceToNative(trial.x,trial.y);
  const auto moved=AmmoImpactNativeTestAccess::step(f.world,id,raw,ms);
  need(moved.ammoAbort,"original actor-to-Ammo abort reached");const auto& a=f.world.actors()[id];const auto* h=a.nativeMotionHistory();
  need(a.nativeAmmoContinuation&&h->center==old,"abort retains native center and pending continuation");
  need(h->previousSurface.x==oldSurface.x&&h->previousSurface.y==oldSurface.y,"abort replaces previous surface with old center projection");
  const unsigned effectiveBehavior=(player?3u:(behavior&3)==2?2u:(behavior&3)==1?1u:0u)|((behavior&28)==12?12u:0u);
  const auto expected=nativeActorAmmoAbortVelocity(AmmoImpactNativeTestAccess::subtype(f.world,id),effectiveBehavior,{old.x,old.y},{proposed.x,proposed.y},raw,14.f/ms);
  same(velocity(f.world,id),expected,"World installs original abort velocity before a non-damaging direct-phase rejection");
  need(a.vitals.energy==1000&&a.enemyMoveCommand==200,"rejected direct FX preserves health/commands");
  need((player?f.world.snapshot().motion.magnitude:a.enemyMagnitude)==789,"abort does not refresh cached magnitude");
  need(!f.world.actors()[2].alive,"selected Ammo still physically removed");
  const auto p=f.scene.objects[id].position;need(p.x==trial.x&&p.y==trial.y,"abort keeps original proposed surface");
  const auto cell=f.world.snapshot().actorCells->cell[id];need(cell!=before.actorCells->cell[id],"fixture crosses cell");
  const auto peer=f.world.spawn({4},p);need(f.world.snapshot().actorCells->heads[cell]==peer,"peer prepended before second visit");
  const auto rested=AmmoImpactNativeTestAccess::step(f.world,id,{},ms);need(!rested.ammoAbort,"rest has no impact");
  need(f.world.actors()[id].nativeAmmoContinuation&&f.world.actors()[id].nativeMotionHistory()->center==old,"rest does not commit retained bounds");
  same(velocity(f.world,id),{},"rest clears velocity");need(f.world.actors()[id].nativeMotionHistory()->previousSurface.x==p.x,"rest copies current surface into previous");
  SaveGame saved;std::string error;const auto bytes=diskBytes(f.world);need(bytes[8]==36&&decodeSave(bytes,saved,error),"Current writer retains v33 pending-movement checkpoint");
  auto content=blueprint;GameWorld resumed;need(resumed.load(content,f.level,error)&&resumed.restoreSnapshot(saved.levels.begin()->second.world,error),error.c_str());
  need(resumed.actors()[id].nativeAmmoContinuation&&diskBytes(resumed)==bytes,"disk retains continuation and exact cells/history");
  // A malformed saved pending body must fail transactionally.
  auto bad=resumed.snapshot();if(bad.actors[id].spiderAI)bad.actors[id].spiderAI->motion.reset();bad.actors[id].nativeMotion.reset();
  need(!resumed.restoreSnapshot(bad,error)&&diskBytes(resumed)==bytes,"pending continuation requires valid history, without mutating world");
  for(auto* w:{&f.world,&resumed}){
   const auto clear=AmmoImpactNativeTestAccess::step(*w,id,raw,ms);need(!clear.ammoAbort&&!w->actors()[id].nativeAmmoContinuation,"next clear visit commits continuation once");
   const auto* next=w->actors()[id].nativeMotionHistory();need(next->center.x==proposed.x&&next->center.y==proposed.y,"next visit advances retained old center, not prior surface");
   need(next->previousSurface.x==oldSurface.x&&next->previousSurface.y==oldSurface.y,"next clear history is old native center projection");
   const auto subtype=AmmoImpactNativeTestAccess::subtype(*w,id);
   const auto clearVelocity=subtype==0x19||subtype==0x1f||subtype==0x20?ContactVelocity{}:ContactVelocity{float((long double)(14.f/ms)*raw.x),float((long double)(14.f/ms)*raw.z)};
   same(velocity(*w,id),clearVelocity,"next clear visit uses original velocity conversion");
   const auto cells=w->snapshot().actorCells;need(cells->heads[cell]==id&&cells->next[id]==peer,"intermediate old-cell reentry preserves native prepend order");
  }
  need(diskBytes(f.world)==diskBytes(resumed),"live/disk continuation remains byte-identical");++resumes;++cases;
 }
 unsigned separatingCases=0;
 for(bool player:{false,true}){
  Fixture f(player?"Cop":"Thing");f.level.hasTerrainCollision=true;const size_t id=player?0:1;
  AmmoImpactNativeTestAccess::setupContinuation(f.world,id,{80.125f,-7.375f},player?3:0);
  const auto old=f.world.actors()[id].nativeMotionHistory()->center;
  need(AmmoImpactNativeTestAccess::step(f.world,id,{80.125f,-7.375f},14).ammoAbort,"separating sequence starts at impact");
  AmmoImpactNativeTestAccess::line(f.world,old.x+4,old.y);
  AmmoImpactNativeTestAccess::step(f.world,id,{-2,0},14);
  const auto separated=f.world.snapshot();const auto* h=separated.actors[id].nativeMotionHistory();
  const auto originalSurface=spiderNativeToSurface(old.x,old.y);
  need(separated.actors[id].nativeAmmoContinuation&&h->center.x<old.x,"separating return2 retains pending divergent bounds");
  need(separated.objects[id].position.x==originalSurface.x&&separated.objects[id].position.y==originalSurface.y,"separating return2 rolls back only surface");
  const auto next=spiderNativeToSurface(h->center.x-20,h->center.y);const auto nextCenter=spiderSurfaceToNative(next.x,next.y);
  AmmoImpactNativeTestAccess::step(f.world,id,{-20,0},14);
  need(!f.world.actors()[id].nativeAmmoContinuation&&f.world.actors()[id].nativeMotionHistory()->center.x==nextCenter.x,"following clear step resumes separating retained center");++separatingCases;
 }
 unsigned publicTicks=0;
 for(bool player:{false,true})for(float ms:{1000.f/60,1000.f/90}){
  Fixture f(player?"Cop":"Thing");f.level.hasTerrainCollision=true;const auto blueprint=f.scene;const size_t id=player?0:1;
  const ContactVelocity raw{80.125f,-7.375f};AmmoImpactNativeTestAccess::setupContinuation(f.world,id,raw,player?3:0);
  need(AmmoImpactNativeTestAccess::step(f.world,id,raw,ms).ammoAbort,"public continuation starts after real impact");
  const auto checkpoint=f.world.snapshot();const auto old=checkpoint.actors[id].nativeMotionHistory()->center;
  const float dt=ms/1000.f,scale=dt/.014f;ContactVelocity nextVelocity;
  if(player){auto motion=checkpoint.motion;PlayerMotionConfig config;
   // Exact resolved fixture player configuration is exposed only to this test.
   config=AmmoImpactNativeTestAccess::playerConfig(f.world);advancePlayerMotion(motion,{},dt,config);nextVelocity={motion.velocityX,motion.velocityZ};
  }else{const auto& c=checkpoint.actors[id].contactConfig;nextVelocity=checkpoint.actors[id].contactVelocity;
   nextVelocity.x*=c.grip;nextVelocity.z*=c.grip;nextVelocity.x*=c.frictionX;nextVelocity.z*=c.frictionZ;
  }
  const auto nextSurface=spiderNativeToSurface(old.x+nextVelocity.x*scale,old.y+nextVelocity.z*scale);
  const auto nextCenter=spiderSurfaceToNative(nextSurface.x,nextSurface.y);
  SaveGame saved;std::string error;need(decodeSave(diskBytes(f.world),saved,error),error.c_str());auto content=blueprint;GameWorld resumed;
  need(resumed.load(content,f.level,error)&&resumed.restoreSnapshot(saved.levels.begin()->second.world,error),error.c_str());resumed.setDebugEnemyAI(false);
  f.world.tick(dt,{});resumed.tick(dt,{});
  const auto after=f.world.snapshot();const auto* h=after.actors[id].nativeMotionHistory();
  need(!after.actors[id].nativeAmmoContinuation&&h->center.x==nextCenter.x&&h->center.y==nextCenter.y,"public next solver uses saved abort velocity and retained center");
  need(after.objects[id].position.x==nextSurface.x&&after.objects[id].position.y==nextSurface.y,"public continuation original projected surface");
  need(diskBytes(f.world)==diskBytes(resumed),"public next tick disk continuation exact");
  for(unsigned tick=0;tick<3;++tick){f.world.tick(dt,{});resumed.tick(dt,{});need(diskBytes(f.world)==diskBytes(resumed),"later public ticks remain exact after continuation commits");}
  ++publicTicks;
 }
 std::cout<<"PASS "<<cases<<" World abort velocity/history/rest/cell controls and "<<resumes<<" exact disk continuations and "<<publicTicks<<" public solver/resume controls and "<<separatingCases<<" separating chains\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
