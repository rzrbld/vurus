#include "game/world.h"
#include "game/save_state.h"
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
   // These controls isolate owner/shape/impact handoff on ACTIVE Ammo.
   // A visible inactive body has disabled bounds in4391d0(actor,0).
   p.phaseInitialized=true;p.inactiveMs=0;p.phaseHidden=false;w.projectiles_.push_back(p);w.rebuildBareAmmoIndex();w.synchronizeBareAmmoCell(w.projectiles_.back());}
  w.initializeActorCells();w.initializeSpiderNavigation(true);
 }
 static TerrainDisplacement move(GameWorld&w,size_t incoming){const ContactVelocity step{20,0};const auto d=nativeToSurface(step.x,step.z);return w.move(incoming,d.x,d.y,true,&step);}
 static void disableBare(GameWorld&w){auto&p=w.projectiles_[0];p.inactiveMs=-1;w.updateSpiderProjectileNavigation(p);}
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
   c.properties.push_back(value("Managed","1"));scene.classes.push_back(c);
  }
  for(unsigned n=0;n<3;++n){ObjectPlacement o;o.id={n};o.classId={n};o.position={float(200+n*200),200};o.properties=scene.classes[n].properties;o.behaviours=scene.classes[n].behaviours;scene.objects.push_back(o);}
  if(duplicate){ObjectPlacement o;o.id={3};o.classId={3};o.position=*duplicate;o.properties=scene.classes[3].properties;o.behaviours=scene.classes[3].behaviours;scene.objects.push_back(o);}
  scene.player=ObjectId{0};std::string error;need(world.load(scene,level,error),error.c_str());world.setDebugEnemyAI(false);
 }
};
int main(){try{unsigned cases=0;
 for(const auto* family:{"Cop","Spider","Walker","Roller"})for(bool bare:{false,true}){
  Fixture f(family);AmmoImpactNativeTestAccess::prepare(f.world,1,false,1000,false,false,bare);
  const auto moved=AmmoImpactNativeTestAccess::move(f.world,1);need(moved.ammoAbort&&f.world.actors()[1].nativeAmmoAbort,"selected Ammo must abort incoming actor");need(f.world.actors()[1].vitals.energy==990,"native direct actor-to-Ammo damage");
  need(bare?!f.world.projectiles()[0].alive:!f.world.actors()[2].alive,"remove1 consumes selected Ammo");
  need(f.world.actors()[1].nativeMotionHistory()->center.x==1500,"impact abort retains incoming native center");
  const auto proposed=surface(1520,500);need(std::abs(f.scene.objects[1].position.x-proposed.x)<.001f,"abort keeps proposed surface without physical rollback");++cases;
 }
 for(bool bare:{false,true})for(unsigned age:{0u,499u,500u})for(bool stale:{false,true})for(bool ignore:{false,true}){
  Fixture f;AmmoImpactNativeTestAccess::prepare(f.world,1,true,age,stale,ignore,bare);auto moved=AmmoImpactNativeTestAccess::move(f.world,1);
  const bool expected=stale||(!ignore&&age>=500);need(moved.ammoAbort==expected,"owner grace/Ignore-owner/generation admission");
  if(bare&&!expected)need(f.world.projectiles()[0].ageMs==age&&f.world.projectiles()[0].inactiveMs==0,"contact query cannot advance stored Ammo activation");++cases;
 }
 for(const auto*family:{"Cop","Spider","Walker","Roller"}){
  Fixture f(family);AmmoImpactNativeTestAccess::prepare(f.world,1,false,1000,false,false,true);AmmoImpactNativeTestAccess::disableBare(f.world);
  const auto moved=AmmoImpactNativeTestAccess::move(f.world,1);need(!moved.ammoAbort&&f.world.projectiles()[0].alive,"visible inactive compact Ammo has disabled bounds");
  need(f.world.actors()[1].vitals.energy==1000&&f.world.projectiles()[0].inactiveMs==-1,"inactive query neither damages actor nor advances activation");++cases;
 }
 {Fixture f("Cop",{},true);AmmoImpactNativeTestAccess::prepare(f.world,1,false,1000,false,false);auto moved=AmmoImpactNativeTestAccess::move(f.world,1);need(!moved.ammoAbort&&f.world.actors()[2].alive,"common terrain winner must block later Ammo proximity");need(f.world.actors()[1].vitals.energy==1000,"wall winner must not dispatch Ammo damage");++cases;}
 for(bool collision:{false,true}){
  Fixture f;AmmoImpactNativeTestAccess::prepare(f.world,0,false,1000,false,false);AmmoImpactNativeTestAccess::playerTail(f.world);
  if(!collision)AmmoImpactNativeTestAccess::bodyPosition(f.world,2,{2500,1500});
  f.world.tick(1.f/60,{{},true});const auto& player=f.world.actors()[0];
  need(player.nativeAmmoAbort==collision,"public player movement must expose original actor-tail abort");
  need(player.ammo.at("Contact-Ammo").count==(collision?10u:9u),"Ammo impact abort must skip held-fire tail");
  need(player.creates[0].done!=collision,"Ammo impact abort must skip ordinary CREATE tail");++cases;
 }
 for(bool collision:{false,true}){
  Fixture f;AmmoImpactNativeTestAccess::prepare(f.world,1,false,1000,false,false);AmmoImpactNativeTestAccess::passiveTail(f.world);
  if(!collision)AmmoImpactNativeTestAccess::bodyPosition(f.world,2,{2500,1500});
  f.world.tick(1.f/60,{});need(f.world.actors()[1].nativeAmmoAbort==collision,"passive body path must preserve actor abort");need(f.world.actors()[1].creates[0].done!=collision,"passive impact must skip CREATE after immediate FX allocation");++cases;
 }
 for(std::optional<ContentPosition> duplicate:{std::optional<ContentPosition>{},std::optional<ContentPosition>{{5,9}},std::optional<ContentPosition>{{70,40}}}){
  Fixture f("Cop",duplicate);AmmoImpactNativeTestAccess::prepare(f.world,1,false,1000,false,false);AmmoImpactNativeTestAccess::singular(f.world,1);auto moved=AmmoImpactNativeTestAccess::move(f.world,1);need(moved.ammoAbort,"original indefinite Ammo winner must still dispatch");
  const bool dup=duplicate&&duplicate->x<64&&duplicate->y<32;need(f.world.actors()[1].vitals.energy==(dup?1000:990),"NaN wildcard duplicate only in original contact cell0");
  for(const auto&o:f.scene.objects)need(std::isfinite(o.position.x)&&std::isfinite(o.position.y),"indefinite contact must not persist in objects");++cases;
 }
 const float nan=std::numeric_limits<float>::quiet_NaN();
 for(bool duplicate:{false,true})for(bool offscreen:{false,true}){
  Fixture f("Cop",duplicate?std::optional<ContentPosition>{{5,20}}:std::nullopt);AmmoImpactNativeTestAccess::bodyPosition(f.world,1,{10,20});
  const auto center=spiderSurfaceToNative(10,20);if(offscreen){NativeEffectCamera camera;camera.offsetX=10000;f.world.setEffectCamera(camera);}
  const auto before=f.scene.objects.size();AmmoImpactNativeTestAccess::impact(f.world,{nan,20},{center.x,center.y},64);
  need(f.world.actors()[1].vitals.energy==(duplicate?990:980),"NaN contact cell radial retained origin precedes duplicate/direct");
  if(!duplicate&&!offscreen){need(f.scene.objects.size()==before+1&&f.scene.objects.back().position.x==0&&f.scene.objects.back().position.y==20,"one indefinite component clamps independently for FX");}
  else need(f.scene.objects.size()==before,"duplicate/camera singular FX admission");++cases;
 }
 std::cout<<"PASS "<<cases<<" World actor-to-Ammo movement/owner/terrain/activation/singular damage-FX handoff controls\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
