#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/spider_contact_native.h"
#include "game/native_body_shape.h"
#include <bit>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const std::string& why){if(!b)throw std::runtime_error(why);}
static bool equal(NativeNavObstacle a,NativeNavObstacle b){return std::bit_cast<uint32_t>(a.minX)==std::bit_cast<uint32_t>(b.minX)&&std::bit_cast<uint32_t>(a.minY)==std::bit_cast<uint32_t>(b.minY)&&std::bit_cast<uint32_t>(a.maxX)==std::bit_cast<uint32_t>(b.maxX)&&std::bit_cast<uint32_t>(a.maxY)==std::bit_cast<uint32_t>(b.maxY);}
namespace vyruz {
struct AmmoImpactNativeTestAccess {
 static void cold(GameWorld& w){w.actorNavigationProjections_.clear();}
 static NativeNavObstacle bounds(GameWorld& w,size_t id){w.updateSpiderNavigation(id);return w.actorNavigationBounds(id,w.nativeDamageSubtype(id));}
 static NativeNavObstacle fresh(GameWorld& w,size_t id){const auto&a=w.actors_[id];const auto&o=w.scene_->objects[id];const auto*h=a.nativeMotionHistory();const auto p=h?Displacement{h->center.x,h->center.y}:spiderSurfaceToNative(o.position.x,o.position.y);const auto shape=nativeBodyContactShape(w.nativeDamageSubtype(id),a.radius);const float r=shape==2?0.f:shape==4?a.radius*.7071067690849304f:a.radius;return {p.x-r,p.y-r,p.x+r,p.y+r};}
 static void history(GameWorld& w,size_t id,bool present){w.actors_[id].spiderAI.reset();if(present)w.actors_[id].nativeMotion=SpiderMotionHistory{{271.25f,-81.5f},{}};else w.actors_[id].nativeMotion.reset();}
 static void radius(GameWorld&w,size_t id,float r){w.actors_[id].radius=r;}
 static void init(GameWorld&w){w.initializeSpiderNavigation(true);}
};
}
static TextNode value(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(text)};return n;}
struct Fixture {
 SceneContent scene;Level level;GameWorld world;
 Fixture(){scene.mapPath="TEST/PROJECTION.3DG";level.mapW=level.mapH=100;
  for(unsigned n=0;n<2;++n){ObjectClass c;c.id={n};c.name=n?"Body":"Player";c.properties={value("Sub-type",n?"Thing":"Vyruz"),value("Bounding-box-size","8")};c.behaviours={n?"Fixed":"Player"};scene.classes.push_back(c);}
  for(unsigned n=0;n<3;++n){ObjectPlacement o;o.id={n};o.classId={n?1u:0u};o.position={1000.125f+float(n)*200,1000.375f};o.properties=scene.classes[o.classId.value].properties;o.behaviours=scene.classes[o.classId.value].behaviours;scene.objects.push_back(o);}
  scene.player=ObjectId{0};std::string error;need(world.load(scene,level,error),error);world.setDebugEnemyAI(false);world.setDebugGodMode(true);AmmoImpactNativeTestAccess::init(world);
 }
};
static std::vector<uint8_t> bytes(const GameWorld&w){SaveGame s;s.currentLevel="TEST/PROJECTION.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string error;need(encodeSave(s,b,error),error);return b;}
int main(){try{
 Fixture warm,cold;std::string error;need(cold.world.restoreSnapshot(warm.world.snapshot(),error),error);
 auto verify=[&](size_t id){need(equal(AmmoImpactNativeTestAccess::bounds(warm.world,id),AmmoImpactNativeTestAccess::fresh(warm.world,id)),"World bounds equal uncached native projection");};
 AmmoImpactNativeTestAccess::history(warm.world,1,false);verify(1);const auto saved=bytes(warm.world);for(int i=0;i<20;++i)verify(1);need(bytes(warm.world)==saved,"warming derived cache does not change save bytes");
 warm.scene.objects[1].position.x=std::nextafter(warm.scene.objects[1].position.x,2000.f);verify(1);
 AmmoImpactNativeTestAccess::radius(warm.world,1,1);verify(1);AmmoImpactNativeTestAccess::radius(warm.world,1,23);verify(1);
 AmmoImpactNativeTestAccess::history(warm.world,1,true);verify(1);warm.scene.objects[1].position={1500.75f,1100.5f};verify(1);AmmoImpactNativeTestAccess::history(warm.world,1,false);verify(1);
 const auto checkpoint=warm.world.snapshot();
 need(warm.world.restoreSnapshot(checkpoint,error),error);need(cold.world.restoreSnapshot(checkpoint,error),error);
 need(bytes(warm.world)==bytes(cold.world),"paired cache runs start from the same normalized state");
 for(unsigned tick=0;tick<240;++tick){
  if(tick%17==0){warm.scene.objects[1].position.x+=.03125f;cold.scene.objects[1].position=warm.scene.objects[1].position;warm.world.invalidateActorSpatialState(1);cold.world.invalidateActorSpatialState(1);}
  AmmoImpactNativeTestAccess::cold(cold.world);WorldInput input;warm.world.tick(1.f/60,input);cold.world.tick(1.f/60,input);verify(1);if(bytes(warm.world)!=bytes(cold.world))throw std::runtime_error("warm/cold World mismatch at tick "+std::to_string(tick));
  if(tick==119){SaveGame decoded;need(decodeSave(bytes(warm.world),decoded,error),error);need(warm.world.restoreSnapshot(decoded.levels.at(decoded.currentLevel).world,error),error);need(cold.world.restoreSnapshot(decoded.levels.at(decoded.currentLevel).world,error),error);}
 }
 auto invalid=warm.world.snapshot();invalid.actors.pop_back();const auto before=bytes(warm.world);need(!warm.world.restoreSnapshot(invalid,error),"invalid snapshot rejected");need(bytes(warm.world)==before,"failed restore remains transactional");verify(1);
 auto replacement=warm.scene;replacement.objects[1].position={2199.5f,779.25f};need(warm.world.load(replacement,warm.level,error),error);AmmoImpactNativeTestAccess::init(warm.world);verify(1);
 std::cout<<"PASS World cache position/radius/history/load/failed-restore checks and 240 byte-exact warm/cold ticks with disk continuation\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
