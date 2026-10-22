#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace {
unsigned checks=0;
void need(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
TextNode value(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(text)};return n;}
TextNode block(std::string key,std::string type,std::vector<TextNode> fields){TextNode n;n.kind=TextKind::Block;n.key=std::move(key);n.arguments={std::move(type)};n.children=std::move(fields);return n;}
SceneContent content(){
 SceneContent s;s.mapPath="TEST/DIRTY.3DG";
 for(unsigned i=0;i<4;++i){ObjectClass c;c.id={i};c.name=i==0?"Player":i==1?"Crate":i==2?"Hidden":"FX";c.behaviours={i==0?"Player":"Fixed"};c.properties={value("Managed","1"),value("Sub-type",i==0?"Vyruz":i==3?"FX":"Thing"),value("Bounding-box-size","8")};if(i==2)c.flags.invisible=true;if(i==3)c.properties.push_back(block("TYPE","FX",{}));s.classes.push_back(c);}
 for(unsigned i=0;i<5;++i){ObjectPlacement o;o.id={i};o.classId={i==0?0u:i==3?2u:i==4?3u:1u};const auto& c=s.classes[o.classId.value];o.properties=c.properties;o.behaviours=c.behaviours;o.flags=c.flags;o.position={200.f+100*i,300};s.objects.push_back(o);}
 s.player=ObjectId{0};return s;
}
std::vector<uint8_t> bytes(const GameWorld& w){SaveGame s;s.currentLevel="TEST/DIRTY.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string error;need(encodeSave(s,b,error),error);return b;}
}
namespace vyruz {struct AmmoImpactNativeTestAccess {
 static void initialize(GameWorld&w){w.initializeSpiderNavigation(true);}
 static void flush(GameWorld&w){w.publishSpatialDirtyActors();}
 static size_t pending(const GameWorld&w){return w.spatialDirtyActors_.size();}
 static bool dirty(const GameWorld&w,size_t id){return w.spatialDirtyFlags_[id]!=0;}
 static size_t node(const GameWorld&w,size_t id){return w.spiderNavigationActors_[id];}
 static NativeNavObstacle bounds(const GameWorld&w,size_t id){return w.spiderNavigation_->objects[node(w,id)].bounds;}
 static void geometry(GameWorld&w,size_t id){w.actors_[id].radius=11;w.actors_[id].nativeMotion=SpiderMotionHistory{{777,888},{}};w.invalidateActorSpatialState(id);}
 static void heldFixture(GameWorld&w,size_t id,bool held){w.actors_[id].nativeHeldOriginal=held;w.synchronizeActorCell(id);}
 static void expire(GameWorld&w,size_t id){w.actors_[id].effect=true;w.expireVisual(id);}
 static void die(GameWorld&w,size_t id){w.destroy(id,{});}
 static void reuse(GameWorld&w,size_t id,ClassId cls){w.freeTransientRecords_[cls.value].push_back(id);}
};}
int main()try{
 auto scene=content();Level level;level.mapW=level.mapH=64;GameWorld w;std::string error;need(w.load(scene,level,error),error);AmmoImpactNativeTestAccess::initialize(w);
 need(AmmoImpactNativeTestAccess::pending(w)==w.actors().size(),"initial full reconciliation");AmmoImpactNativeTestAccess::flush(w);need(AmmoImpactNativeTestAccess::pending(w)==0,"initial drain");
 for(unsigned tick=0;tick<20;++tick){w.tick(.014f,{});need(!AmmoImpactNativeTestAccess::dirty(w,1)&&!AmmoImpactNativeTestAccess::dirty(w,2),"stationary managed crates remain clean during live timer visits");}need(w.actors()[1].ageMs>0,"stationary crate was visited");
 const auto stable=bytes(w);for(unsigned i=0;i<10;++i)AmmoImpactNativeTestAccess::flush(w);need(stable==bytes(w),"stationary empty queue has no persistent effect");
 scene.objects[2].position={700,700};scene.objects[1].position={700,700};for(unsigned i=0;i<20;++i){w.invalidateActorSpatialState(2);w.invalidateActorSpatialState(1);}need(AmmoImpactNativeTestAccess::pending(w)==2,"deduplicated pending IDs");AmmoImpactNativeTestAccess::flush(w);
 AmmoImpactNativeTestAccess::geometry(w,1);AmmoImpactNativeTestAccess::flush(w);const auto b=AmmoImpactNativeTestAccess::bounds(w,1);need(b.minX==766&&b.maxX==788&&b.minY==877&&b.maxY==899,"radius and authoritative center publication");
 w.setPlayerPosition(777,700);AmmoImpactNativeTestAccess::flush(w);need(w.playerPosition().x==777,"stationary teleport survives publication");
 need(AmmoImpactNativeTestAccess::node(w,3)==SIZE_MAX,"invisible shape starts absent");scene.objects[3].flags.invisible=false;w.invalidateActorSpatialState(3);AmmoImpactNativeTestAccess::flush(w);need(AmmoImpactNativeTestAccess::node(w,3)!=SIZE_MAX,"admission change inserts missing shape");
 AmmoImpactNativeTestAccess::expire(w,4);AmmoImpactNativeTestAccess::flush(w);need(!w.actors()[4].alive,"visual expiry reconciled");
 const auto id=w.spawn(ClassId{3},{800,800});AmmoImpactNativeTestAccess::flush(w);AmmoImpactNativeTestAccess::expire(w,id);AmmoImpactNativeTestAccess::flush(w);AmmoImpactNativeTestAccess::reuse(w,id,ClassId{3});const auto reused=w.spawn(ClassId{3},{900,800});need(reused==id,"fixture reuses expired FX row");AmmoImpactNativeTestAccess::flush(w);
 AmmoImpactNativeTestAccess::die(w,2);AmmoImpactNativeTestAccess::flush(w);need(!w.actors()[2].alive&&AmmoImpactNativeTestAccess::node(w,2)==SIZE_MAX,"death removes shape");
 w.invalidateActorSpatialState(2);AmmoImpactNativeTestAccess::reuse(w,2,ClassId{1});need(w.spawn(ClassId{1},{820,770})==2,"shape-bearing current row replaces queued dead ID");scene.objects[2].position.x+=1;w.invalidateActorSpatialState(2);need(AmmoImpactNativeTestAccess::pending(w)==1,"replacement preserves one current-row request");AmmoImpactNativeTestAccess::flush(w);need(AmmoImpactNativeTestAccess::node(w,2)!=SIZE_MAX,"reused shape published");
#if VURUS_SPATIAL_ORACLE
 scene.objects[0].position.x+=1;bool detected=false;try{AmmoImpactNativeTestAccess::flush(w);}catch(const std::runtime_error&e){detected=std::string(e.what()).find("Spatial oracle:")==0;}need(detected,"independent full sweep detects a missed external invalidation");w.invalidateActorSpatialState(0);AmmoImpactNativeTestAccess::flush(w);
#endif
 auto rebound=scene;rebound.objects[0].position.y+=4;w.rebind(rebound,level);need(AmmoImpactNativeTestAccess::pending(w)==w.actors().size(),"rebind reconciles all current IDs");AmmoImpactNativeTestAccess::flush(w);
 auto saved=w.snapshot();need(w.restoreSnapshot(saved,error),error);need(AmmoImpactNativeTestAccess::pending(w)==w.actors().size(),"restore rebuilds derived pending state");w.setDebugGodMode(true);
 need(w.debugSetSurprise(SurpriseKind::TimeWarp),"Time Warp fixture");for(unsigned i=0;i<24;++i)w.tick(.014f,{});w.cancelSurprise();
 SaveGame decoded;need(decodeSave(bytes(w),decoded,error),error);auto otherScene=content();GameWorld other;need(other.load(otherScene,level,error)&&other.restoreSnapshot(decoded.levels.begin()->second.world,error),error);w.setDebugEnemyAI(false);other.setDebugEnemyAI(false);w.setDebugGodMode(true);other.setDebugGodMode(true);
 for(unsigned i=0;i<60;++i){w.tick(.014f,{});other.tick(.014f,{});need(bytes(w)==bytes(other),"serialized dirty-queue reconstruction continuation");}
 std::cout<<"PASS "<<checks<<" spatial queue/geometry/lifecycle/admission/rebind/restore checks;60serialized continuation ticks\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
