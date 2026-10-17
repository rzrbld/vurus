#include "game/world.h"
#include "game/save_state.h"
#include "game/demo_native.h"
#include "game/properties.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static TextNode value(std::string key,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={v};return n;}
static TextNode group(std::string key,std::vector<TextNode> fields){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=fields;return n;}
struct Run {
 SceneContent scene;Level level;GameWorld world;std::string error;
 Run(int target=0){scene.mapPath="TEST/DEMO.3DG";ObjectClass c;c.id={0};c.name="Player";c.behaviours={"Player"};c.properties={value("Sub-type","Vyruz"),value("Managed","1"),value("Bounding-box-size","25"),group("Weight",{value("Initial","15")}),value("Grip","1"),value("Speed-max","45"),value("Turn-timeout","18"),group("Thrust",{value("Initial","0"),value("Min","-10"),value("Max","45")}),group("Energy",{value("Initial","1000000"),value("Max","1000000")})};
 auto friction=value("Friction-factor","0.972");friction.values.push_back("0.972");c.properties.push_back(friction);scene.classes={c};ObjectPlacement p;p.id={0};p.classId={0};p.position={1000,1000};p.properties=c.properties;p.behaviours=c.behaviours;scene.objects={p};scene.player=ObjectId{0};level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;
 if(target){ObjectClass ammo;ammo.id={1};ammo.name="Mine";ammo.behaviours={"Fixed"};auto type=group("TYPE",{value("Damage","500"),value("Damage-zone","1"),value("Ignore-owner","0")});type.arguments={"Ammo"};ammo.properties={value("Sub-type","Ammo"),value("Managed","1"),value("Bounding-box-size","8"),type};if(target==2){ammo.name="Box";ammo.properties={value("Sub-type","Thing"),value("Managed","1"),value("Bounding-box-size","8"),group("Weight",{value("Initial","5")}),group("Energy",{value("Initial","10000"),value("Max","10000")}),value("Grip","1")};auto friction=value("Friction-factor","0.9");friction.values.push_back("0.9");ammo.properties.push_back(friction);}
 scene.classes.push_back(ammo);ObjectPlacement placed;placed.id={1};placed.classId={1};placed.properties=ammo.properties;placed.behaviours=ammo.behaviours;const auto offset=nativeToSurface(60,0);placed.position={1000+offset.x,1000+offset.y};scene.objects.push_back(placed);}
 need(world.load(scene,level,error),error.c_str());}
};
int main(){try{
 for(int heading:{0,3,9,15,21})for(bool forward:{false,true}){
  Run r;auto s=r.world.snapshot();s.motion.velocityX=100;s.motion.velocityZ=200;s.motion.magnitude=123;s.motion.thrust=17;r.world.restoreSnapshot(s,r.error);
  WorldInput input;input.replayPose=DemoRecord{0,forward?4u:0u,1004,1001,unsigned(heading)};input.movement.forward=forward?1:0;
  auto expected=s.motion;expected.direction=heading;const auto velocity=nativeDemoRecordedVelocity(1004,1001,1000,1000);expected.velocityX=velocity.x;expected.velocityZ=velocity.y;const auto delta=advancePlayerMotion(expected,input.movement,1.f/60);
  r.world.tick(1.f/60,input);const auto after=r.world.snapshot();const auto p=r.world.playerPosition();
  need(after.motion.velocityX==expected.velocityX&&after.motion.velocityZ==expected.velocityZ&&after.motion.thrust==expected.thrust,"replay cleared native momentum/thrust or missed digital input");
  need(std::abs(p.x-(1004+delta.x))<.002f&&std::abs(p.y-(1001+delta.y))<.002f,"replay pose must precede ordinary physics, without a post-tick teleport");
  need(after.nativePlayerPreviousSurface.x==1004&&after.nativePlayerPreviousSurface.y==1001,"replay must retain admitted surface history");
 }
 {Run r(1);const auto before=r.world.player().vitals.energy;const auto offset=nativeToSurface(20,0);WorldInput input;input.replayPose=DemoRecord{0,0,1000+offset.x,1000+offset.y,0};r.world.tick(1.f/60,input);need(r.world.player().vitals.energy<before,"moving replay must enter normal player-to-Ammo damage query");}
 {Run r(2);const auto before=r.scene.objects[1].position;need(r.world.actors()[1].contactConfig.enabled&&r.world.actors()[1].contactConfig.frictionX>0,"movable box fixture");const auto offset=nativeToSurface(20,0);WorldInput input;input.replayPose=DemoRecord{0,0,1000+offset.x,1000+offset.y,0};r.world.tick(1.f/60,input);need(r.scene.objects[1].position.x!=before.x||r.scene.objects[1].position.y!=before.y,"replay collision must transfer momentum to debris");}
 {Run r;need(r.world.debugSetSurprise(SurpriseKind::Drone),"activate Drone");for(int i=0;i<40;++i)r.world.tick(1.f/60,{});const auto origin=r.world.surprise().droneOrigin;need(r.world.surprise().active(SurpriseKind::Drone),"Drone admission");
  WorldInput input;input.replayPose=DemoRecord{0,0,origin.x+400,origin.y,0};int steps=0;
  while(r.world.surprise().remainingMs>400&&steps++<1200)r.world.tick(1.f/60,input);
  need(steps<1200&&r.world.surprise().droneReturning,"demo must advance Drone return");
  need(r.world.playerPosition().x<origin.x+300,"recorded pose overwrote the Drone return branch");
  need(r.scene.objects[0].flags.invisible.value_or(false),"Drone original body must remain hidden during replay");
 }
 {Run r;auto s=r.world.snapshot();s.surprise.kind=SurpriseKind::Circular;s.surprise.applied=true;s.surprise.remainingMs=5000;s.surprise.circularRemaining=10;need(r.world.restoreSnapshot(s,r.error),r.error.c_str());WorldInput input;input.replayPose=DemoRecord{0,2,1400,1000,9};input.fire=true;r.world.tick(1.f/60,input);need(r.world.playerPosition().x==1000,"replay overwrote Circular's skipped motion branch");}
 std::cout<<"PASS10 replay motion/history controls, Ammo damage, movable debris, Drone return and Circular admission\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
