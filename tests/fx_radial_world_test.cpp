#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/properties.h"
#include "snapshot_fixture.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace vyruz {struct AmmoImpactNativeTestAccess {
 static void blast(GameWorld& w,size_t id,float damage,float radius){w.applyNativeExplosionRadial(id,damage,radius);}
 static void owner(GameWorld& w,size_t id,std::optional<size_t> owner,bool stale=false){
  w.assignNativeCreator(id,owner);if(stale)--w.actors_[id].nativeCreatorGeneration;
 }
 static void retainedCenter(GameWorld& w,size_t id,ContentPosition p){const auto n=spiderSurfaceToNative(p.x,p.y);w.actors_[id].nativeMotion=SpiderMotionHistory{{n.x,n.y},p};}
 static void explosion(GameWorld& w,size_t id,uint32_t duration,float delay){
  GafSequence sequence;sequence.subframes.resize(8);for(auto& frame:sequence.subframes)frame.duration=duration;
  const auto& sprite=w.scene_->objects[id].sprite;w.effectSequences_[{sprite.path,sprite.sequence.value}]=sequence;
  PendingExplosion payload;payload.damage=1;payload.radius=64;payload.remainingMs=delay;w.actors_[id].explosion=payload;
 }
 static void visit(GameWorld& w,size_t id,float milliseconds){w.updateExplosion(id,milliseconds);}
 static void topExplosion(GameWorld& w,size_t id){w.spawnExplosions(w.scene_->objects[id],0,0,true);}
 static void touch(GameWorld& w){w.surprise_.kind=SurpriseKind::Touch;w.surprise_.remainingMs=10000;w.surprise_.applied=true;}
};}
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode value(std::string k,std::string v){TextNode n;n.key=std::move(k);n.kind=TextKind::Assignment;n.values={std::move(v)};return n;}
static TextNode block(std::string k,std::vector<TextNode> c,std::string type={}){TextNode n;n.key=std::move(k);n.kind=TextKind::Block;n.children=std::move(c);if(!type.empty())n.arguments={std::move(type)};return n;}
static void place(SceneContent& s,ClassId c,ContentPosition p){ObjectPlacement o;o.id={uint32_t(s.objects.size())};o.classId=c;o.position=p;o.properties=s.classes[c.value].properties;o.behaviours=s.classes[c.value].behaviours;o.flags=s.classes[c.value].flags;o.sprite=s.classes[c.value].sprite;s.objects.push_back(std::move(o));}
static SceneContent fixture(){
 SceneContent s;s.mapPath="TEST/FX-RADIAL.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Sub-type","Vyruz"),value("Bounding-box-size","20"),block("Energy",{value("Initial","10000"),value("Max","10000")})};
 ObjectClass target;target.id={1};target.name="Target";target.behaviours={"Fixed","Seeker"};target.properties={block("TYPE",{value("Target","Player"),value("Sensor-Range","0")},"Seeker"),value("Sub-type","Cop"),value("Bounding-box-size","1"),block("Energy",{value("Initial","100"),value("Max","100")}),block("Shield",{value("Initial","2.5"),value("Min","0")})};
 ObjectClass fx;fx.id={2};fx.name="FX";fx.properties={value("Sub-type","FX"),block("Energy",{value("Initial","1000")}),block("TYPE",{},"FX")};
 ObjectClass bare;bare.id={3};bare.name="Bare";bare.flags.invisible=true;bare.behaviours={"Straight"};bare.properties={value("Sub-type","Ammo"),value("Bounding-box-size","1"),value("Keep-bb-when-invisible","1"),value("Speed-max","0"),block("Energy",{value("Initial","10")}),block("Shield",{value("Initial","5.5"),value("Min","0")}),block("TYPE",{value("Damage","0")},"Ammo")};
 ObjectClass item;item.id={4};item.name="Item";item.properties={value("Sub-type","Item"),block("Energy",{value("Initial","100")}),block("TYPE",{value("Only-for-player","1")},"Item")};
 s.classes={player,target,fx,bare,item};place(s,{0},{1000,1000});place(s,{1},{500,500});place(s,{2},{500,500});place(s,{1},{4000,2000});s.player=ObjectId{0};return s;
}
static Level arena(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;return l;}
static void load(GameWorld& w,SceneContent& s,const Level& l){std::string e;need(w.load(s,l,e),e);w.setDebugEnemyAI(false);AmmoImpactNativeTestAccess::owner(w,2,0);}
static WorldSnapshot disk(const GameWorld& w){SaveGame save;save.currentLevel="TEST/FX-RADIAL.3DG";save.levels[save.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string e;need(encodeSave(save,b,e),e);SaveGame decoded;need(decodeSave(b,decoded,e),e);return decoded.levels.begin()->second.world;}
int main(){try{
 const auto level=arena();
 {auto s=fixture();s.objects[1].properties.push_back(block("TYPE",{value("Boom-fx","FX"),value("Damage","100"),value("Damage-zone","64")},"Explosion"));GameWorld w;load(w,s,level);AmmoImpactNativeTestAccess::topExplosion(w,1);const auto id=w.actors().size()-1;const auto center=spiderSurfaceToNative(500,500);
  need(s.objects[id].position.y==504&&w.actors()[id].nativeMotionHistory()&&w.actors()[id].nativeMotionHistory()->center.x==center.x&&w.actors()[id].nativeMotionHistory()->center.y==center.y,"upper Explosion retains construction center before surfaceY+4 and cell enrollment");
 }
 for(bool delayed:{false,true}){auto s=fixture();GameWorld w;load(w,s,level);
  AmmoImpactNativeTestAccess::explosion(w,2,delayed?100:2,delayed?50:500);
  // 440cee -> 431ae0 advances before444ca0 consumes the current cursor.
  // Eight28ms subframes start at cursor1: halfway cursor4 is reached at84ms.
  AmmoImpactNativeTestAccess::visit(w,2,delayed?49:83);
  need(w.actors()[1].vitals.shield==2.5f,"Explosion waits until the exact post-advance halfway/delay boundary");
  AmmoImpactNativeTestAccess::visit(w,2,1);
  need(w.actors()[1].vitals.shield==1.5f,"Explosion exact delay or post-advance halfway cursor triggers native radial");
  need(w.actors()[2].explosion&&w.actors()[2].explosion->damage==0&&w.actors()[2].explosion->remainingMs==-1,"Explosion consumes live payload after native radial callback");
  AmmoImpactNativeTestAccess::visit(w,2,1);need(w.actors()[1].vitals.shield==1.5f,"Explosion cannot apply consumed payload twice");
 }

 {auto s=fixture();GameWorld w;load(w,s,level);AmmoImpactNativeTestAccess::blast(w,2,102.75f,64);
  const auto& a=w.actors()[1];need(a.alive&&a.nativeDamagePending&&a.vitals.energy==-.25f&&a.vitals.shield==0,"FX preserves fractional scalar health and defers death to recipient visit");
  need(w.snapshot().fastKillCount==1,"player-owned lethal FX contributes Fast Kill");need(!a.nativeKillCounted&&w.playerKills()==0&&w.player().nativeAmmoOwner.kills==0,"FX never uses Ammo-only counted or owner-kill tail");
  const auto saved=disk(w);auto copy=fixture();GameWorld resumed;load(resumed,copy,level);std::string e;need(resumed.restoreSnapshot(saved,e),e);need(resumed.actors()[1].nativeDamagePending&&resumed.actors()[1].alive,"FX pending death survives disk restore");
  w.tick(.017f,{});resumed.tick(.017f,{});need(!w.actors()[1].alive&&!resumed.actors()[1].alive,"later own visit consumes lethal FX pending death");
 }
 {auto s=fixture();GameWorld w;load(w,s,level);auto state=w.snapshot();Projectile p;p.classId={3};p.owner=0;p.position={500,500};p.radius=1;p.range=10000;p.ageMs=state.actors[2].ageMs;p.lifetimeMs=10000;vyruz_test::appendProjectile(state,s,p);std::string e;need(w.restoreSnapshot(state,e),e);AmmoImpactNativeTestAccess::blast(w,2,100,64);
  const auto& b=*w.projectiles()[0].nativeBareBody;need(b.energy==-84.5f&&b.shield==0&&b.damagePending&&w.projectiles()[0].alive,"FX visits compact bare Ammo even at same age");need(!b.killCounted&&w.player().nativeAmmoOwner.kills==0,"FX bare victim has no Ammo counted/owner kill tail");
 }
 for(bool middle:{false,true}){auto s=fixture();GameWorld w;load(w,s,level);if(middle){AmmoImpactNativeTestAccess::owner(w,2,3);AmmoImpactNativeTestAccess::owner(w,3,0,true);}else AmmoImpactNativeTestAccess::owner(w,2,0,true);const auto captured=w.actors()[middle?3:2].nativeCreatorGeneration;
  AmmoImpactNativeTestAccess::blast(w,2,103,1);need(!s.objects[middle?3:2].owner&&w.actors()[middle?3:2].nativeCreatorGeneration==captured,"FX ancestry clears first stale pointer while preserving captured generation");if(middle)need(s.objects[2].owner==ObjectId{3},"valid immediate FX creator remains after stale middle link");need(w.snapshot().fastKillCount==0,"stale ancestry cannot gain player Fast Kill");
 }
 {auto s=fixture();s.classes[1].properties.push_back(value("Player-direct-fire-only","1"));s.objects[1].properties=s.classes[1].properties;GameWorld w;load(w,s,level);AmmoImpactNativeTestAccess::owner(w,2,3);AmmoImpactNativeTestAccess::owner(w,3,0);AmmoImpactNativeTestAccess::blast(w,2,102.75f,64);need(w.actors()[1].vitals.energy==-.25f,"FX uses captured creator ancestry for Player-direct-fire-only recipient");}
 {auto s=fixture();s.classes[1].properties.push_back(value("Player-direct-fire-only","1"));s.objects[1].properties=s.classes[1].properties;GameWorld w;load(w,s,level);AmmoImpactNativeTestAccess::owner(w,2,std::nullopt);AmmoImpactNativeTestAccess::blast(w,2,100,64);need(w.actors()[1].vitals.energy==100&&w.actors()[1].vitals.shield==2.5f,"unowned FX cannot damage Player-direct-fire-only recipient");}
 {auto s=fixture();s.objects[0].position={500,500};place(s,{4},{500,500});GameWorld w;load(w,s,level);AmmoImpactNativeTestAccess::touch(w);AmmoImpactNativeTestAccess::blast(w,2,100,64);need(w.player().vitals.energy==10000,"FX excludes Touch recipients");need(w.actors()[4].vitals.energy==100,"FX excludes Only-for-player Items");need(w.actors()[2].vitals.energy==1000,"FX excludes itself and other FX subtypes");}
 {auto s=fixture();GameWorld w;load(w,s,level);AmmoImpactNativeTestAccess::retainedCenter(w,2,{600,500});AmmoImpactNativeTestAccess::blast(w,2,100,64);need(w.actors()[1].vitals.energy==100&&w.actors()[1].vitals.shield==2.5f,"FX distance uses retained native center independently of surface");}
 std::cout<<"FX World: ancestry, exact/deferred health, actor+bare Ammo admission, FX-only credit, retained center and disk pending continuation passed\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
