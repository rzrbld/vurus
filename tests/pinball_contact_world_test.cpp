#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={v};return n;}
static TextNode group(std::string key,std::vector<TextNode> fields){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=fields;return n;}
struct Run {
 SceneContent scene;Level level;GameWorld world;std::string error;
 Run(float clearance=0,bool withFx=false){
  scene.mapPath="TEST/PINBALL.3DG";ObjectClass cls;cls.id={0};cls.name="Player";cls.behaviours={"Player"};
  cls.properties={value("Sub-type","Vyruz"),value("Managed","1"),value("Bounding-box-size","25"),group("Weight",{value("Initial","15")}),value("Grip","1"),value("Speed-max","45"),value("Turn-timeout","18"),group("Thrust",{value("Initial","0"),value("Min","-10"),value("Max","45")}),group("Energy",{value("Initial","1000000"),value("Max","1000000")})};
  auto friction=value("Friction-factor","0.972");friction.values.push_back("0.972");cls.properties.push_back(friction);
  scene.classes={cls};ObjectPlacement p;p.id={0};p.classId={0};p.position={1500,1500};p.properties=cls.properties;p.behaviours=cls.behaviours;scene.objects={p};scene.player=ObjectId{0};level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;
  const auto origin=spiderSurfaceToNative(1500,1500);const float left=origin.x-25-clearance,right=origin.x+25+clearance;
  for(float wall:{left,right}){const auto a=spiderNativeToSurface(wall,origin.y-1000),b=spiderNativeToSurface(wall,origin.y+1000);level.collisionEdges.push_back({a.x,a.y,b.x,b.y});}
  level.nativeNavigationDecor={{left-100,origin.y-1000,left,origin.y+1000,0,0,1,false,false,true},{right,origin.y-1000,right+100,origin.y+1000,0,0,1,false,false,true}};
  if(withFx) {
   scene.classes[0].properties.push_back(value("Spark-on-hit-point","0"));
   scene.classes[0].properties.push_back(value("Altitude","24"));
   for(const char* name:{"Flipper-Bong-Up-Left","Flipper-Bong-Up-Right","Flipper-Bong-Down-Left","Flipper-Bong-Down-Right"}) {
    ObjectClass fx;fx.id={static_cast<uint32_t>(scene.classes.size())};fx.name=name;fx.behaviours={"Fixed"};
    fx.properties={value("Managed","1"),value("Life-time","1000")};auto type=group("TYPE",{});type.arguments={"FX"};fx.properties.push_back(type);scene.classes.push_back(fx);
   }
  }
  if(withFx)scene.objects[0].properties=scene.classes[0].properties;
  need(world.load(scene,level,error),error.c_str());world.setDebugGodMode(true);need(world.debugSetSurprise(SurpriseKind::Pinball),"Pinball activation");for(int i=0;i<40;++i)world.tick(1.f/60,{});need(world.surprise().active(SurpriseKind::Pinball),"native pickup delay must elapse");
 }
 void restore(const WorldSnapshot& s){need(world.restoreSnapshot(s,error),error.c_str());world.setDebugGodMode(true);}
};
namespace vyruz {struct AmmoImpactNativeTestAccess {
 static void bounceFx(GameWorld& w,ContentPosition contact) {
  Projectile source;source.classId=w.scene_->objects[w.player_].classId;source.visual=w.player_;source.slopeAltitude=24;
  const auto n=spiderSurfaceToNative(contact.x,contact.y);w.spawnProjectileBounceEffect(source,w.playerPosition(),{n.x,n.y});
 }
 static void wallBounce(GameWorld& w) {
  const auto n=spiderSurfaceToNative(w.playerPosition().x,w.playerPosition().y);
  const auto start=spiderNativeToSurface(n.x+999.5f,n.y);w.setPlayerPosition(start.x,start.y);
  w.motion_.velocityX=4;w.motion_.velocityZ=0;w.motion_.magnitude=4;w.motion_.thrust=20;
  w.nativePlayerPreviousSurface_=w.playerPosition();w.nativePlayerPreviousSurface_.x-=1;
  w.currentTickMilliseconds_=14;const ContactVelocity step{4,0};const auto d=nativeToSurface(4,0);
  need(w.move(w.player_,d.x,d.y,true,&step).constrained,"native-speed threshold wall contact must occur");
 }
 static ContactVelocity effective(GameWorld& w,bool command,bool moved){w.playerMoveCommanded_=command;w.motion_.thrust=20;w.motion_.velocityX=100;w.motion_.velocityZ=200;w.nativePlayerPreviousSurface_=w.playerPosition();if(moved)w.nativePlayerPreviousSurface_.x-=1;return w.effectiveContactVelocity(w.player_);}
 static bool responseSkippedWithoutCommand(GameWorld& w){
  w.playerMoveCommanded_=false;w.motion_.thrust=20;w.motion_.velocityX=4;w.motion_.velocityZ=0;w.motion_.magnitude=4;w.nativePlayerPreviousSurface_=w.playerPosition();
  //Keep surface rollback exactly representable, as in an already-stalled visit.
  const auto n=spiderSurfaceToNative(w.playerPosition().x,w.playerPosition().y);auto p=spiderNativeToSurface(n.x,n.y);w.scene_->objects[w.player_].position={p.x,p.y};w.nativePlayerPreviousSurface_={p.x,p.y};
  w.currentTickMilliseconds_=14;ContactVelocity step{4,0};const auto d=nativeToSurface(4,0);w.move(w.player_,d.x,d.y,true,&step);
  return w.motion_.velocityX==4 && w.motion_.velocityZ==0;
 }
};}
static std::vector<uint8_t> bytes(GameWorld& world){SaveGame s;s.currentLevel="TEST/PINBALL.3DG";s.levels[s.currentLevel].world=world.snapshot();std::vector<uint8_t>b;std::string error;need(encodeSave(s,b,error),error.c_str());return b;}
int main(){try{
 {Run r(1000,true);AmmoImpactNativeTestAccess::wallBounce(r.world);
  const auto before=r.world.snapshot();need(std::abs(before.motion.velocityX+6)<.00001f,"Pinball retains native1.5 reflection");
  need(before.motion.magnitude==4,"Pinball collision must preserve precontact magnitude cache");
  bool flipper=false;for(const auto& o:r.scene.objects)flipper|=r.scene.classes[o.classId.value].name.starts_with("Flipper-Bong-");
  need(flipper,"live Pinball wall impact emits directional FX despite Spark0");
  SaveGame saved;std::string error;need(decodeSave(bytes(r.world),saved,error),error.c_str());Run resumed(1000,true);resumed.restore(saved.levels.begin()->second.world);
  WorldInput input;input.movement.forward=1;const auto h=nativeHeading(before.motion.direction);
  const float expectedX=(before.motion.velocityX+h.x*21.f/15.f)*.972f;
  const float expectedZ=(before.motion.velocityZ+h.y*21.f/15.f)*.972f;
  r.world.tick(.014f,input);resumed.world.tick(.014f,input);
  const auto after=r.world.snapshot();need(std::abs(after.motion.velocityX-expectedX)<.00001f&&std::abs(after.motion.velocityZ-expectedZ)<.00001f,
   "next native propulsion admits thrust from cache4 even though reflected speed6 exceeds4.5");
  // Disk saves deliberately remove Surprise effects. Compare two normalized
  // worlds, independently of the active-Pinball propulsion assertion above.
  auto normalized=before;normalizeSavedSurprise(normalized);
  Run normalizedRun(1000,true);normalizedRun.restore(normalized);
  normalizedRun.world.tick(.014f,input);
  need(bytes(normalizedRun.world)==bytes(resumed.world),"normalized Pinball cache/FX disk continuation differs");
 }
 {Run r(1000,true);const auto origin=r.world.playerPosition();
  for(unsigned q=0;q<4;++q){const ContentPosition point{origin.x+(q&1?30.f:-30.f),origin.y+(q&2?15.f:-15.f)};
   AmmoImpactNativeTestAccess::bounceFx(r.world,point);need(r.scene.objects.size()==q+2,"one directional Pinball effect");
   const auto id=r.scene.objects.size()-1;need(r.scene.objects[id].classId.value==q+1,"Pinball original quadrant class");
   need(r.world.actors()[id].projectileAltitude==24,"Pinball effect inherits current altitude");
   const auto n=spiderSurfaceToNative(point.x,point.y);const auto projected=spiderNativeToSurface(n.x,n.y);
   need(r.scene.objects[id].position.x==std::trunc(projected.x)&&r.scene.objects[id].position.y==std::trunc(projected.y-8),"Pinball effect projection/truncation");
   AmmoImpactNativeTestAccess::bounceFx(r.world,point);need(r.scene.objects.size()==q+2,"class-per-cell Pinball effect suppression");
  }
  need(r.world.debugSetSurprise(SurpriseKind::None),"end Pinball");AmmoImpactNativeTestAccess::bounceFx(r.world,{origin.x+150,origin.y+150});
  need(r.scene.objects.size()==5,"ordinary Spark0 must not emit Pinball FX");
 }
 {Run r;const auto h=nativeHeading(r.world.player().direction);auto v=AmmoImpactNativeTestAccess::effective(r.world,true,false);need(v.x==h.x*20&&v.z==h.y*20,"stationary player collision uses thrust instead of accumulated bounce");v=AmmoImpactNativeTestAccess::effective(r.world,true,true);need(v.x==100&&v.z==200,"moving player keeps real momentum");v=AmmoImpactNativeTestAccess::effective(r.world,false,false);need(v.x==100&&v.z==200,"stationary no-command retains momentum for caller bypass");}
 {Run r(-1);need(AmmoImpactNativeTestAccess::responseSkippedWithoutCommand(r.world),"uncommanded stalled contact must not multiply bounce");}
 for(float clearance:{-1.f,0.f,1.f,8.f})for(int heading:{0,3,6,9,15,21}){
  Run r(clearance);r.world.setPlayerDirection(heading);auto s=r.world.snapshot();auto h=nativeHeading(heading);s.motion.velocityX=h.x*40;s.motion.velocityZ=h.y*40;s.motion.magnitude=4;s.motion.thrust=20;r.restore(s);
  for(int tick=0;tick<1260;++tick){WorldInput input;input.movement.forward=1;input.movement.strafe=tick<300?1:0;const auto before=r.world.playerPosition();r.world.tick(1.f/60,input);const auto after=r.world.playerPosition();
   // This fixture protects the accepted active-Pinball swept containment.
   // After expiry, ordinary Player deliberately uses original shape queries;
   // an initially penetrating rectangle can admit departure under that policy.
   // Ordinary contact selection/crossing is covered by the native World matrix.
   if(r.world.surprise().active(SurpriseKind::Pinball))need(terrainMoveAllowed(r.level,before.x,before.y,after.x,after.y,25),"Pinball crossed authored corridor wall");const auto speed=r.world.playerNativeSpeed();const auto pos=r.world.playerPosition();need(pos.x>=0&&pos.x<6400&&pos.y>=0&&pos.y<3200,"Pinball bypassed accepted map boundary");need(std::isfinite(speed)&&speed<1000,"narrow-corridor response ran away (this fixture previously exceeded65536)");}
  need(!r.world.surprise().active(SurpriseKind::Pinball),"effect expiry must keep advancing during contacts");
 }
 // A single endpoint can land beyond both walls. Sweep must still reject it.
 {Run r(8);auto s=r.world.snapshot();s.motion.velocityX=1000;s.motion.velocityZ=0;s.motion.magnitude=1000;r.restore(s);
  for(int tick=0;tick<120;++tick){const auto before=r.world.playerPosition();WorldInput input;input.movement.forward=1;r.world.tick(1.f/60,input);const auto after=r.world.playerPosition();need(terrainMoveAllowed(r.level,before.x,before.y,after.x,after.y,25),"high-speed Pinball tunneled through wall");const auto n=spiderSurfaceToNative(after.x,after.y);const auto origin=spiderSurfaceToNative(1500,1500);need(n.x>=origin.x-8.01f&&n.x<=origin.x+8.01f,"high-speed Pinball escaped corridor");}
 }
 {Run r(1);WorldInput input;input.movement.forward=1;input.movement.strafe=1;for(int tick=0;tick<100;++tick)r.world.tick(1.f/60,input);
  Run resumed(1);resumed.restore(r.world.snapshot());for(int tick=0;tick<60;++tick){r.world.tick(1.f/60,input);resumed.world.tick(1.f/60,input);need(bytes(r.world)==bytes(resumed.world),"active in-memory continuation differs");}
  SaveGame saved;std::string error;auto encoded=bytes(r.world);need(decodeSave(encoded,saved,error),error.c_str());auto normalized=r.world.snapshot();normalizeSavedSurprise(normalized);r.restore(normalized);resumed.restore(saved.levels.at(saved.currentLevel).world);
  for(int tick=0;tick<60;++tick){r.world.tick(1.f/60,input);resumed.world.tick(1.f/60,input);need(bytes(r.world)==bytes(resumed.world),"disk normalized continuation differs");}
 }
 std::cout<<"PASS Pinball player admission,24 live narrow corridors/expiry, active and disk continuation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
