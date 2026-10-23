#include "game/world.h"
#include "game/actor_contact_native.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static bool close(float a,float b){return std::abs(a-b)<.00002f;}
static TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
static TextNode group(std::string k,std::vector<TextNode> c){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=std::move(c);return n;}
static ContentPosition surface(float x,float z){const auto p=spiderNativeToSurface(x,z);return {p.x,p.y};}
struct Run {
 SceneContent scene;Level level;GameWorld world;
 Run(std::vector<ContactVelocity> targets={},std::vector<float> radii={},float firstFriction=1,float secondFriction=1){
  scene.mapPath="TEST/PLAYER-CONTACT.3DG";
  for(unsigned i=0;i<=targets.size();++i){ObjectClass c;c.id={i};c.name=i?"Cop"+std::to_string(i):"Player";c.behaviours={i?"Fixed":"Player"};const float radius=i?radii[i-1]:8;
   c.properties={value("Sub-type",i?"Cop":"Vyruz"),value("Managed","1"),value("Bounding-box-size",std::to_string(radius)),group("Weight",{value("Initial",i?"10":"15")}),value("Elasticity-factor",i?"0.7":"1"),group("Energy",{value("Initial","100000"),value("Max","100000")})};
   auto f=value("Friction-factor",std::to_string(i?secondFriction:firstFriction));f.values.push_back(f.values.front());c.properties.push_back(f);
   scene.classes.push_back(c);ObjectPlacement o;o.id={i};o.classId={i};o.properties=c.properties;o.behaviours=c.behaviours;o.position=i?surface(targets[i-1].x,targets[i-1].z):surface(1500,500);scene.objects.push_back(o);
  }
  scene.player=ObjectId{0};level.mapW=level.mapH=128;level.hasTerrainCollision=true;
 }
 void load(){std::string e;need(world.load(scene,level,e),e.c_str());world.setDebugGodMode(true);}
};
namespace vyruz {struct AmmoImpactNativeTestAccess {
 static void prepare(GameWorld&w,ContactVelocity velocity={20,0}){
  w.elapsedMs_=100;w.currentTickMilliseconds_=14;w.motion_.velocityX=velocity.x;w.motion_.velocityZ=velocity.z;w.motion_.magnitude=37;w.motion_.thrust=0;
  w.nativePlayerPreviousSurface_=w.playerPosition();w.nativePlayerPreviousSurface_.x-=1;
  for(auto&a:w.actors_){a.ageMs=1000;a.solid=true;a.ammoContactDisabled=false;a.contactConfig.enabled=true;a.enemyMovedLastStep=true;a.enemyMoveCommand=0;}
  w.initializeActorCells();w.initializeSpiderNavigation(true);
 }
 static void move(GameWorld&w,ContactVelocity step){const auto d=nativeToSurface(step.x,step.z);w.move(w.player_,d.x,d.y,true,&step);}
 static ContactVelocity center(GameWorld&w){const auto p=w.playerPosition();const auto n=spiderSurfaceToNative(p.x,p.y);return {n.x,n.y};}
 static void winner(GameWorld&w){prepare(w);move(w,{20,0});const auto n=center(w);need(close(n.x,1500)&&close(n.z,500),"blocked Player did not roll back");need(w.actors_[1].contactResponseMs!=100&&w.actors_[2].contactResponseMs==100,"Player did not consume native actor2 winner");need(w.motion_.magnitude==37,"collision refreshed native magnitude cache");}
 static void actorResponse(GameWorld&w,ContactVelocity step,ContactVelocity targetVelocity){
  prepare(w,step);w.actors_[1].contactVelocity=targetVelocity;
  const auto old=center(w);const auto p=spiderNativeToSurface(old.x+step.x,old.z+step.z);const auto to=spiderSurfaceToNative(p.x,p.y);
  const auto q=w.queryNativeBodyContacts(0,old,{to.x,to.y},8);need(q.used&&q.selected&&q.selected->reference==1&&q.selectedShape,"missing native oblique actor contact");
  const auto a=w.actors_[0].contactConfig,b=w.actors_[1].contactConfig;
  // The helper has independent original436dd0 fixtures. This checks its real
  // World call boundary, including absolute coordinates and write ownership.
  const auto expected=resolveNativeActorContact(step,targetVelocity,old,q.selectedShape->center,q.selected->point,a.weight,b.weight,a.elasticity,b.elasticity,a.frictionX!=0,b.frictionX!=0);
  move(w,step);const auto source=w.contactVelocity(0),target=w.contactVelocity(1);
  need(close(source.x,expected.first.x)&&close(source.z,expected.first.z),"World used a centerline Player response");
  need(close(target.x,expected.second.x)&&close(target.z,expected.second.z),"native target response/write ownership differs");
  const auto rollback=spiderNativeToSurface(old.x,old.z);need(w.playerPosition().x==rollback.x&&w.playerPosition().y==rollback.y,"actor response did not use original rollback stores");
  need(w.motion_.magnitude==37,"actor response changed cached magnitude");
 }
 static void wall(GameWorld&w,ContactVelocity step){prepare(w,step);const auto old=center(w);move(w,step);const auto p=w.playerPosition();const auto rollback=spiderNativeToSurface(old.x,old.z);need(p.x==rollback.x&&p.y==rollback.y,"Player tunneled/advanced through native edge crossing");const auto v=w.contactVelocity(0);need(close(v.x,-step.x)&&close(v.z,-step.z),"wrong native axis response");need(w.motion_.magnitude==37,"terrain response refreshed magnitude");}
 static void clear(GameWorld&w,ContactVelocity step){prepare(w,step);const auto from=w.playerPosition();const auto d=nativeToSurface(step.x,step.z);move(w,step);need(w.playerPosition().x==from.x+d.x&&w.playerPosition().y==from.y+d.y,"clear Player surface integration changed");need(w.motion_.velocityX==step.x&&w.motion_.velocityZ==step.z&&w.motion_.magnitude==37,"clear Player propulsion state changed");}
 static void restoredPlayerHistory(GameWorld&w,int mode){const auto p=w.playerPosition();const auto n=spiderSurfaceToNative(p.x,p.y);const auto* h=w.actors_[w.player_].nativeMotionHistory();need(h&&h->center.x==n.x&&h->center.y==n.y&&h->previousSurface.x==p.x&&h->previousSurface.y==p.y,mode==0?"Drone cancellation retained excursion contact coordinates":mode==1?"Drone expiry retained excursion contact coordinates":"Drone disk normalization retained excursion contact coordinates");}
 static void stationary(GameWorld&w){prepare(w,{100,200});w.motion_.thrust=20;w.playerMoveCommanded_=true;w.nativePlayerPreviousSurface_=w.playerPosition();if(auto* h=w.actors_[0].nativeMotionHistory())h->previousSurface=w.playerPosition();const auto h=nativeHeading(w.motion_.direction);auto v=w.effectiveContactVelocity(w.player_);need(v.x==h.x*20&&v.z==h.y*20,"ordinary stationary commanded Player did not use thrust");w.playerMoveCommanded_=false;v=w.effectiveContactVelocity(w.player_);need(v.x==100&&v.z==200,"uncommanded Player momentum was discarded");}
};}
static std::vector<uint8_t> bytes(GameWorld&w){SaveGame s;s.currentLevel="TEST/PLAYER-CONTACT.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string e;need(encodeSave(s,b,e),e.c_str());return b;}
int main(){try{
 {Run r({{1523,500},{1527,500}},{8,12});r.load();AmmoImpactNativeTestAccess::winner(r.world);}
 for(const auto step:{ContactVelocity{20,0},ContactVelocity{12,4},ContactVelocity{0,20},ContactVelocity{-20,0}})
  for(float first:{0.f,1.f})for(float second:{0.f,1.f}){
   const ContactVelocity target{1500+step.x+(step.x<0?-3:3),500+step.z+(step.z<0?-8:8)};
   Run r({target},{12},first,second);r.load();AmmoImpactNativeTestAccess::actorResponse(r.world,step,{-.25f,.4f});
  }
 for(const auto step:{ContactVelocity{40,0},ContactVelocity{-40,0},ContactVelocity{0,40},ContactVelocity{0,-40}}){
  Run r;const bool x=step.x!=0;const float edge=x?1500+(step.x>0?10:-10):500+(step.z>0?10:-10);
  NativeNavigationDecor d=x?NativeNavigationDecor{edge,400,edge,600}:NativeNavigationDecor{1400,edge,1600,edge};r.level.nativeNavigationDecor.push_back(d);
  auto a=surface(d.minX,d.minY),b=surface(d.maxX,d.maxY);r.level.collisionEdges.push_back({a.x,a.y,b.x,b.y});r.load();AmmoImpactNativeTestAccess::wall(r.world,step);
 }
 {Run r;r.load();AmmoImpactNativeTestAccess::clear(r.world,{1.2f,.3f});AmmoImpactNativeTestAccess::stationary(r.world);}
 {Run live({{1523,508}},{12});live.load();AmmoImpactNativeTestAccess::actorResponse(live.world,{20,0},{-.25f,.4f});live.world.tick(.014f,{});
  const auto dir=std::filesystem::temp_directory_path()/("vurus-player-contact-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));need(std::filesystem::create_directory(dir),"temporary save directory");
  struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{dir};
  SaveGame s,decoded;s.currentLevel=live.scene.mapPath;s.levels[s.currentLevel].world=live.world.snapshot();std::string e;need(writeSaveFile((dir/"checkpoint.vzs").string(),s,e)&&readSaveFile((dir/"checkpoint.vzs").string(),decoded,e),e.c_str());
  Run resumed({{1523,508}},{12});resumed.load();need(resumed.world.restoreSnapshot(decoded.levels.at(s.currentLevel).world,e),e.c_str());
  for(unsigned tick=0;tick<120;++tick){WorldInput in;in.movement.forward=tick<60?1:0;live.world.tick(.014f,in);resumed.world.tick(.014f,in);need(bytes(live.world)==bytes(resumed.world),"native Player collision disk continuation differs");}
 }
 // Contact history must return with the parked Player, both live and on disk.
 for(int mode=0;mode<3;++mode){
  Run r;r.load();WorldInput forward;forward.movement.forward=1;
  for(int i=0;i<40;++i)r.world.tick(.014f,forward);
  need(r.world.debugSetSurprise(SurpriseKind::Drone),"activate Drone history fixture");
  for(int i=0;i<40;++i)r.world.tick(.014f,{});
  const auto origin=r.world.surprise().droneOrigin;
  r.world.setPlayerPosition(origin.x+300,origin.y+100);
  if(mode==0)r.world.cancelSurprise();
  else if(mode==1){int ticks=0;while(r.world.surprise().kind!=SurpriseKind::None&&ticks++<4000)r.world.tick(.014f,{});need(ticks<4000,"Drone did not expire");}
  else {
   SaveGame saved;std::string e;const auto excursion=r.world.playerPosition();
   need(decodeSave(bytes(r.world),saved,e),e.c_str());
   need(r.world.playerPosition().x==excursion.x&&r.world.playerPosition().y==excursion.y,"Drone disk normalization changed live excursion");
   need(r.world.restoreSnapshot(saved.levels.begin()->second.world,e),e.c_str());
  }
  need(r.world.playerPosition().x==origin.x&&r.world.playerPosition().y==origin.y,"Drone did not restore its parked body");
  AmmoImpactNativeTestAccess::restoredPlayerHistory(r.world,mode);
  for(int i=0;i<10;++i)r.world.tick(.014f,forward);
  const auto after=r.world.playerPosition();need(std::hypot(after.x-origin.x,after.y-origin.y)<20,"Player jumped back to Drone excursion after reset");
 }
 std::cout<<"PASS native Player winner/rollback,16 oblique/write-ownership cases,4 full wall crossings,clear integration,stationary drive 120disk continuation frames and3Drone reset paths\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
