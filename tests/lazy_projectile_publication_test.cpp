#include "game/world.h"
#include "game/save_file.h"
#include "game/save_state.h"
#include "game/spider_contact_native.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace {
unsigned checks=0;
void need(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
TextNode value(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(text)};return n;}
TextNode group(std::string key,std::vector<TextNode> children,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=std::move(key);n.children=std::move(children);if(!type.empty())n.arguments={std::move(type)};return n;}
SceneContent content(){
 SceneContent s;s.mapPath="TEST/LAZY-PUBLICATION.3DG";
 for(unsigned id=0;id<3;++id){ObjectClass c;c.id={id};c.name=id==0?"Player":id==1?"Cop":"Ammo";c.behaviours=id==0?std::vector<std::string>{"Player"}:id==1?std::vector<std::string>{"Fixed","Seeker"}:std::vector<std::string>{"Fixed","Tracker"};
  c.properties={value("Managed","1"),value("Sub-type",id==0?"Vyruz":id==1?"Cop":"Ammo"),value("Bounding-box-size","8"),value("Speed-max","0"),value("Speed-min","0"),group("Energy",{value("Initial","1000"),value("Max","1000")})};
  if(id==1)c.properties.push_back(group("TYPE",{value("Target","Player"),value("Sensor-Range","600"),value("Scan-Delay","1000"),value("Min-distance","50"),value("Vision-Angle","360")},"Seeker"));
  if(id==2){c.properties.push_back(group("TYPE",{value("Damage","0")},"Ammo"));c.properties.push_back(group("TYPE",{value("Sensor-Range","600"),value("Scan-Delay","20"),value("Vision-Angle","360")},"Tracker"));}
  s.classes.push_back(c);
 }
 for(unsigned id=0;id<2;++id){ObjectPlacement o;o.id={id};o.classId={id};o.position={1000.f+100.f*id,1000};o.properties=s.classes[id].properties;o.behaviours=s.classes[id].behaviours;s.objects.push_back(o);}
 s.player=ObjectId{0};return s;
}
std::vector<uint8_t> encoded(const GameWorld& w){SaveGame s;s.currentLevel="TEST/LAZY-PUBLICATION.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t> b;std::string error;need(encodeSave(s,b,error),error);return b;}
}
namespace vyruz {
struct AmmoImpactNativeTestAccess {
 static void seed(GameWorld& w,unsigned count,bool guided){
  for(unsigned i=0;i<count+unsigned(guided);++i){Projectile p;p.classId={2};p.owner=SIZE_MAX;p.position={2000.f+i*4,2000};p.radius=i%3?8:1;p.ageMs=1000;p.phaseInitialized=true;p.inactiveMs=i%2?2000:0;p.phaseHidden=i%2;
   if(guided&&i==count){p.position=w.playerPosition();p.radius=1;p.guidanceClass=ClassId{1};p.guidanceTarget=1;p.phaseHidden=false;p.inactiveMs=0;}
   w.initializeBareAmmoBody(p);w.projectiles_.push_back(std::move(p));
  }
  w.rebuildBareAmmoIndex();for(const auto& p:w.projectiles_)w.synchronizeBareAmmoCell(p);
  w.spiderNavigation_.reset();w.spiderNavigationActors_.clear();w.actorNavigationProjections_.clear();
 }
 static void initialize(GameWorld& w){w.initializeSpiderNavigation(true);}
 static bool invoke(GameWorld& w,unsigned mode){
  const auto p=w.playerPosition(),target=w.scene_->objects[1].position;const auto delta=spiderSurfaceToNative(target.x-p.x,target.y-p.y);const auto heading=spiderContactHeading({delta.x,delta.y});w.setPlayerDirection(heading);
  // The AI return value is its firing lock, not whether the visit ran.
  if(mode==0){w.advanceOrdinaryEnemyAI(1,1.f/60,{});return w.actors_[1].ordinaryAI.has_value()&&w.spiderNavigation_.has_value();}
  if(mode==1){Projectile shot;shot.classId={2};shot.position=p;w.initializeProjectileGuidance(shot,w.player_);return shot.guidanceClass.has_value()&&shot.guidanceTarget==1;}
  auto& shot=w.projectiles_.back();shot.guidance.pose48=heading*2+1;w.updateProjectileGuidance(shot,17);return shot.guidanceNative.has_value()&&shot.guidanceNative->search.has_value();
 }
 static void inspect(const GameWorld& w,unsigned count){
  need(w.spiderNavigation_.has_value(),"lazy caller initialized graph");need(w.projectiles_.size()==count,"existing projectile count unchanged");
  for(const auto& shot:w.projectiles_){const auto id=shot.spiderGraphNode;need(id<w.spiderNavigation_->objects.size(),"existing bare Ammo published before first query");const auto& body=w.spiderNavigation_->objects[id];
   need(body.alive&&!body.point,"Ammo retains physical body");need((body.reference&((uint64_t(1)<<61)-1))==shot.nativeBodyIdentity,"bare body identity retained");need(bool(body.reference&(uint64_t(1)<<61))==shot.phaseHidden,"disabled bounds flag retained");
   const auto center=shot.nativeBareBody->motion.center;const float radius=shot.radius<2?0:shot.radius;need(body.bounds.minX==center.x-radius&&body.bounds.maxX==center.x+radius&&body.bounds.minY==center.y-radius&&body.bounds.maxY==center.y+radius,"native bounds retained");
  }
 }
};
}
int main()try{
 for(unsigned mode=0;mode<3;++mode)for(unsigned count:{0u,1u,16u}){
  auto a=content(),b=a;Level level;level.mapW=level.mapH=100;GameWorld lazy,ready;std::string error;need(lazy.load(a,level,error)&&ready.load(b,level,error),error);
  AmmoImpactNativeTestAccess::seed(lazy,count,mode==2);AmmoImpactNativeTestAccess::seed(ready,count,mode==2);AmmoImpactNativeTestAccess::initialize(ready);
  need(AmmoImpactNativeTestAccess::invoke(lazy,mode),"lazy path exercised, mode="+std::to_string(mode));need(AmmoImpactNativeTestAccess::invoke(ready,mode),"prepublished control path exercised, mode="+std::to_string(mode));
  AmmoImpactNativeTestAccess::inspect(lazy,count+unsigned(mode==2));need(encoded(lazy)==encoded(ready),"lazy and prepublished World bytes match");
  SaveGame disk;need(decodeSave(encoded(lazy),disk,error),error);auto c=a;GameWorld restored;need(restored.load(c,level,error)&&restored.restoreSnapshot(disk.levels.begin()->second.world,error),error);
  for(unsigned tick=0;tick<10;++tick){lazy.tick(1.f/60,{});ready.tick(1.f/60,{});restored.tick(1.f/60,{});need(encoded(lazy)==encoded(ready)&&encoded(lazy)==encoded(restored),"lazy/prepublished/disk continuation matches");}
 }
 std::cout<<"PASS "<<checks<<" lazy AI/launch/reacquire projectile-publication controls and90serialized save/load continuation ticks\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
