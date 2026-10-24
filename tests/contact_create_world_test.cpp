#include "game/world.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <cmath>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const std::string& why){if(!b)throw std::runtime_error(why);}
static TextNode v(std::string k,std::string s){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={s};return n;}
static TextNode b(std::string k,std::vector<TextNode> c,std::string t=""){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=c;if(!t.empty())n.arguments={t};return n;}
static ContentPosition surface(float x,float z){const auto p=spiderNativeToSurface(x,z);return {p.x,p.y};}
static bool near(ContentPosition a,ContentPosition c){return std::abs(a.x-c.x)<.001f&&std::abs(a.y-c.y)<.001f;}
namespace vyruz {
struct AmmoImpactNativeTestAccess {
 static size_t prepare(GameWorld&w,int mode,bool current,bool due,bool repeating,bool moving,bool radial,bool stale=false){
  for(auto&a:w.actors_){a.ageMs=1000;a.contactVelocity={};a.nativeAmmoAbort=false;}
  const auto incoming=spiderSurfaceToNative(w.scene_->objects[1].position.x,w.scene_->objects[1].position.y);w.actors_[1].nativeMotion=SpiderMotionHistory{{incoming.x,incoming.y},surface(1490,500)};w.actors_[1].contactVelocity={20,0};w.actors_[1].enemyMoveCommand=200;
  const auto start=surface(moving?1500:1523,500),previous=surface(1483,480);
  const size_t visual=mode?w.spawn({2},start):SIZE_MAX;
  if(mode){auto&a=w.actors_[visual];a.ageMs=1000;a.ammoHidden=false;a.ammoContactDisabled=false;a.solid=false;
   a.nativeMotion=SpiderMotionHistory{{moving?1500.f:1523.f,500},previous};a.projectileAltitude=7;
   w.assignNativeCreator(visual,0);if(stale)++a.nativeCreatorGeneration;
   need(a.creates.size()==1,"authored CREATE admitted");a.creates[0].nextMs=due?1000:2000;a.creates[0].repeatMs=repeating?100:0;a.creates[0].currentPosition=current;
  }
  if(mode!=2){Projectile p;p.classId={2};p.owner=0;p.position=start;p.radius=8;p.damage=radial?100:10;p.damageRadius=radial?128:0;
   p.ageMs=1000;p.lifetimeMs=10000;p.phaseInitialized=true;p.inactiveMs=0;p.phaseHidden=false;p.hitEffect="HitFx";
   p.motionConfig.inertia=true;p.motionConfig.launched=true;p.motionConfig.frictionX=p.motionConfig.frictionZ=1;p.motionConfig.grip=1;
   p.motion.velocityX=moving?20:0;p.motion.magnitude=moving?20:0;p.directionX=1;p.directionZ=0;
   if(mode){p.visual=visual;p.nativeGeneration=w.actors_[visual].nativeGeneration;p.nativeOwnerGeneration=w.actors_[visual].nativeCreatorGeneration;w.configureProjectileCreates(p,true);need(w.actors_[visual].creates.empty(),"emitted schedule transferred");p.creates[0].nextMs=due?1000:2000;p.creates[0].repeatMs=repeating?100:0;}
   else {w.initializeBareAmmoBody(p);w.enrollNativeAmmo(p);p.nativeBareBody->motion.previousSurface=previous;
    ProjectileCreate c;c.classId={3};c.nextMs=due?1000:2000;c.repeatMs=repeating?100:0;c.currentPosition=current;p.creates={c};if(stale)++p.nativeOwnerGeneration;}
   w.projectiles_.push_back(p);w.rebuildBareAmmoIndex();w.synchronizeBareAmmoCell(w.projectiles_.back());
  }
  // Both vectors must reallocate on the next child; source/caller IDs survive.
  w.actors_.shrink_to_fit();w.scene_->objects.shrink_to_fit();
  w.initializeActorCells();for(auto&p:w.projectiles_)w.synchronizeBareAmmoCell(p);w.initializeSpiderNavigation(true);
  return visual;
 }
 static void actorContact(GameWorld&w){w.currentTickMilliseconds_=14;const ContactVelocity step{20,0};const auto d=nativeToSurface(step.x,step.z);w.move(1,d.x,d.y,true,&step);}
 static void projectileStep(GameWorld&w){w.currentTickMilliseconds_=14;w.updateProjectile(0,.014f,true);}
 static void timed(GameWorld&w,bool ground){auto&p=w.projectiles_[0];p.motion.velocityX=p.motion.magnitude=0;
  if(ground){p.slopeEnabled=true;p.slopeHeight=0;p.slopeAltitude=0;}else{p.fuseMs=1;p.damageRemainingMs=0;}}
 static void producer(GameWorld&w){auto&p=w.projectiles_[0];w.updateProjectileCreates(p,p.position);}
 static void failProduct(GameWorld&w,bool fail){w.projectiles_[0].creates[0].classId={fail?999u:3u};}
 static void checkTimer(GameWorld&w,bool done,float next){const auto&c=w.projectiles_[0].creates[0];need(c.done==done&&c.nextMs==next,"CREATE timer mutation");}
 static void rejectActorCreate(GameWorld&w,size_t id){w.actors_[id].creates[0].offset={1000000,1000000};}
 static void actorTimerUnconsumed(GameWorld&w,size_t id){const auto&c=w.actors_[id].creates[0];need(!c.done&&c.nextMs==1000,"failed authored contact CREATE retains timer");}
 static void explicitActorProducer(GameWorld&w,size_t id){w.updateActorCreates(id,w.scene_->objects[id].position);}
 static void noShot(GameWorld&w){need(w.projectiles_.empty()||!w.projectiles_[0].alive,"source consumed");}
};
}
struct Fixture {
 SceneContent scene;Level level;GameWorld world;
 Fixture(bool current=false,bool radial=false,bool moving=false,bool wall=false){
  scene.mapPath="TEST/CONTACT-CREATE.3DG";level.mapW=level.mapH=100;level.hasTerrainCollision=true;
  for(unsigned n=0;n<5;++n){ObjectClass c;c.id={n};c.name=n==0?"Player":n==1?"Target":n==2?"SourceAmmo":n==3?"Product":"HitFx";
   c.behaviours={n==0?"Player":"Fixed"};
   c.properties={v("Managed","1"),v("Sub-type",n==0?"Vyruz":n==1?"Cop":n==2?"Ammo":n==3?"Thing":"FX"),v("Bounding-box-size",n<3?"8":"0"),b("Energy",{v("Initial","1000"),v("Max","1000")})};
   if(n==2){c.properties.push_back(v("Altitude","7"));c.properties.push_back(b("TYPE",{v("Damage",radial?"100":"10"),v("Damage-zone",radial?"128":"0"),v("Hit-fx","HitFx"),v("Ignore-owner","0")},"Ammo"));
    c.properties.push_back(b("CREATE",{v("Name","Product"),v("Delay","1000"),v("At-current-position",current?"1":"0")}));}
   if(n==4)c.properties.push_back(b("TYPE",{},"FX"));scene.classes.push_back(c);
  }
  for(unsigned n=0;n<2;++n){ObjectPlacement o;o.id={n};o.classId={n};o.position=surface(n?(moving?1523:1500):500,500);o.properties=scene.classes[n].properties;o.behaviours=scene.classes[n].behaviours;scene.objects.push_back(o);}
  if(wall){scene.objects[1].position=surface(2000,500);level.nativeNavigationDecor.push_back({1518,400,1518,600,0,0,1,false,false,false});}
  scene.player=ObjectId{0};std::string error;need(world.load(scene,level,error),error);world.setDebugEnemyAI(false);
 }
 size_t count(unsigned cls)const{size_t n=0;for(const auto&o:scene.objects)if(o.classId.value==cls)++n;return n;}
 size_t product()const{for(size_t i=0;i<scene.objects.size();++i)if(scene.objects[i].classId.value==3)return i;throw std::runtime_error("missing product");}
 void checkProduct(ContentPosition expected,bool radial=false,float altitude=7){need(count(3)==1,"exactly one due CREATE product");const auto id=product();need(near(scene.objects[id].position,expected),"CREATE origin");
  need(!scene.objects[id].owner&&world.actors()[id].nativeCreatorGeneration==0,"CREATE has no creator");need(world.actors()[id].projectileAltitude==altitude,"CREATE altitude inherited got="+std::to_string(world.actors()[id].projectileAltitude.value_or(-999)));
  if(radial)need(world.actors()[id].vitals.energy<1000,"contact CREATE precedes radial damage");
  for(size_t i=0;i<scene.objects.size();++i)if(scene.objects[i].classId.value==4)need(id<i,"CREATE birth precedes Hit-fx");
 }
};
static std::vector<uint8_t> disk(const GameWorld&w,const std::string&map){SaveGame s;s.currentLevel=map;s.levels[map].world=w.snapshot();std::string e;std::vector<uint8_t> bytes;need(encodeSave(s,bytes,e),e);return bytes;}
static void actorCases(){for(int mode:{0,1,2})for(bool current:{false,true})for(bool due:{false,true})for(bool repeat:{false,true})for(bool stale:{false,true}){
 Fixture f(current);const auto source=AmmoImpactNativeTestAccess::prepare(f.world,mode,current,due,repeat,false,false,stale);
 AmmoImpactNativeTestAccess::actorContact(f.world);need(f.world.ammoHits()+f.world.ammoMisses()==(stale?0u:1u),"contact counters mode="+std::to_string(mode)+" current="+std::to_string(current)+" stale="+std::to_string(stale)+" got="+std::to_string(f.world.ammoHits()+f.world.ammoMisses()));need(f.count(3)==(due?1u:0u),"actor consumes Ammo: due producer serviced");
 if(due)f.checkProduct(current?surface(1523,500):surface(1483,480));
 if(mode)need(!f.world.actors()[source].alive,"visual/authored source destroyed");else AmmoImpactNativeTestAccess::noShot(f.world);
 }
 // A constructor/ordinary visit which already consumed a one-shot schedule
 // must not get a second birth when another actor contacts it.
 for(int mode:{1,2}){Fixture f(true);const auto id=AmmoImpactNativeTestAccess::prepare(f.world,mode,true,true,false,false,false);
  if(mode==1)AmmoImpactNativeTestAccess::producer(f.world);else AmmoImpactNativeTestAccess::explicitActorProducer(f.world,id);
  AmmoImpactNativeTestAccess::actorContact(f.world);need(f.count(3)==1,"one-shot not double-serviced");}
}
static void movingCases(){for(int mode:{0,1})for(bool current:{false,true})for(bool wall:{false,true}){
 Fixture f(current,true,true,wall);AmmoImpactNativeTestAccess::prepare(f.world,mode,current,true,false,true,true);
 AmmoImpactNativeTestAccess::projectileStep(f.world);AmmoImpactNativeTestAccess::noShot(f.world);
 f.checkProduct(current?surface(1520,500):surface(1500,500),true);
 }
}
static void timedCases(){for(int mode:{0,1})for(bool current:{true,false})for(bool ground:{false,true}){
 Fixture f(current,true);AmmoImpactNativeTestAccess::prepare(f.world,mode,current,true,false,false,true);AmmoImpactNativeTestAccess::timed(f.world,ground);
 AmmoImpactNativeTestAccess::projectileStep(f.world);AmmoImpactNativeTestAccess::noShot(f.world);f.checkProduct(current?surface(1523,500):surface(1483,480),false,mode&&ground?0:7);
 need(f.world.actors()[f.product()].vitals.energy==1000,"fuse/ground radial precedes CREATE");need(f.world.ammoHits()+f.world.ammoMisses()==0,"timed detonation is not contact");
 }
}
static void failureCase(){Fixture f(true);AmmoImpactNativeTestAccess::prepare(f.world,0,true,true,true,false,false);AmmoImpactNativeTestAccess::failProduct(f.world,true);
 AmmoImpactNativeTestAccess::producer(f.world);AmmoImpactNativeTestAccess::checkTimer(f.world,false,1000);need(f.count(3)==0,"failed factory no child");
 AmmoImpactNativeTestAccess::failProduct(f.world,false);AmmoImpactNativeTestAccess::producer(f.world);AmmoImpactNativeTestAccess::checkTimer(f.world,false,1100);need(f.count(3)==1,"successful retry consumes one interval");
 Fixture authored;const auto id=AmmoImpactNativeTestAccess::prepare(authored.world,2,false,true,true,false,false);AmmoImpactNativeTestAccess::rejectActorCreate(authored.world,id);AmmoImpactNativeTestAccess::actorContact(authored.world);AmmoImpactNativeTestAccess::actorTimerUnconsumed(authored.world,id);need(authored.count(3)==0&&!authored.world.actors()[id].alive,"failed contact CREATE still destroys source");
}
static void continuation(){for(int mode:{0,1,2})for(bool current:{false,true}){
 Fixture f(current,true);AmmoImpactNativeTestAccess::prepare(f.world,mode,current,true,true,false,true);f.world.takeAudioEvents();
 Fixture pristine(current,true);auto initial=pristine.scene;SaveGame saved,decoded;saved.currentLevel=f.scene.mapPath;saved.levels[saved.currentLevel].world=f.world.snapshot();std::string e;
 const auto dir=std::filesystem::temp_directory_path()/("vurus-contact-create-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 std::filesystem::create_directory(dir);struct Cleanup{std::filesystem::path p;~Cleanup(){std::filesystem::remove_all(p);}} cleanup{dir};
 const auto path=(dir/"checkpoint.vzs").string();need(writeSaveFile(path,saved,e),e);need(readSaveFile(path,decoded,e),e);
 GameWorld resumed;need(resumed.load(initial,f.level,e),e);need(resumed.restoreSnapshot(decoded.levels.at(f.scene.mapPath).world,e),e);resumed.setDebugEnemyAI(false);resumed.takeAudioEvents();
 AmmoImpactNativeTestAccess::actorContact(f.world);AmmoImpactNativeTestAccess::actorContact(resumed);
 for(unsigned n=0;n<8;++n){f.world.takeAudioEvents();resumed.takeAudioEvents();need(disk(f.world,f.scene.mapPath)==disk(resumed,f.scene.mapPath),"contact CREATE disk continuation mode="+std::to_string(mode)+" current="+std::to_string(current)+" tick="+std::to_string(n));f.world.tick(.014f,{});resumed.tick(.014f,{});}
 }
}
int main(int argc,char**argv){try{const std::string suite=argc>1?argv[1]:"all";if(suite=="all"||suite=="actor")actorCases();if(suite=="all"||suite=="moving")movingCases();if(suite=="all"||suite=="timed")timedCases();if(suite=="all"||suite=="failure")failureCase();if(suite=="all"||suite=="continuation")continuation();std::cout<<"contact CREATE: native phase/origin, bare/emitted/authored, failure/retry, reallocation, one-shot, disk continuation passed\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
