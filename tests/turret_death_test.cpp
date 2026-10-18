#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace vyruz { struct AmmoImpactNativeTestAccess {
 static void touch(GameWorld& w) {w.surprise_.kind=SurpriseKind::Touch;w.surprise_.remainingMs=8000;w.surprise_.applied=true;}
 static void moveTouch(GameWorld& w,bool physical) {
  w.move(w.player_,500,0,physical);
  auto victims=std::move(w.touchVictims_);w.touchVictims_.clear();
  for(auto id:victims)w.destroy(id,w.player_);
 }
}; }
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={v};return n;}
static TextNode block(std::string key,std::vector<TextNode> ns,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=ns;if(!type.empty())n.arguments={type};return n;}
static SceneContent fixture(){
 SceneContent s;s.mapPath="TEST/TURRET.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={block("Energy",{value("Initial","1000"),value("Max","1000")})};
 ObjectClass turret;turret.id={1};turret.name="Tower";turret.behaviours={"Fixed","Seeker"};
 turret.properties={value("Sub-type","Turret"),value("Reward","17"),value("Bounding-box-size","30"),value("Two-pieces","1"),block("Energy",{value("Initial","80"),value("Max","80")}),block("Shield",{value("Initial","200"),value("Max","200")}),block("TYPE",{value("Target","Player"),value("Sensor-Range","500")},"Seeker")};
 TextNode have;have.key="HAVE";have.arguments={"Drop"};turret.properties.push_back(have);
 ObjectClass drop;drop.id={2};drop.name="Drop";drop.properties={value("Sub-type","Item"),block("TYPE",{value("Only-for-player","1")},"Item")};
 s.classes={player,turret,drop};
 for(unsigned i=0;i<2;++i){ObjectPlacement o;o.id={i};o.classId={i};o.position={100.f+500*i,100};o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;s.objects.push_back(o);}s.player=ObjectId{0};
 // Placement overrides affect only the intact actor, never the base's reset.
 s.objects[1].properties.push_back(value("Energy","5"));s.objects[1].properties.push_back(value("Shield","0"));return s;
}
int main(){try{
 // Touch's player sweep must retain the lower piece's collision/damage body.
 for(const char* family:{"Turret","Tank"}) for(bool physical:{false,true}) {
  auto content=fixture();
  for(auto* properties:{&content.classes[1].properties,&content.objects[1].properties})
   properties->push_back(value("Sub-type",family));
  Level arena;arena.mapW=arena.mapH=100;arena.hasTerrainCollision=true;
  const auto authored=content;
  GameWorld w;std::string error;need(w.load(content,arena,error),error.c_str());
  AmmoImpactNativeTestAccess::touch(w);
  AmmoImpactNativeTestAccess::moveTouch(w,physical);
  need(w.actors()[1].alive&&w.actors()[1].turretOnlyBase,"Touch removes upper piece");
  need(w.actors()[1].solid,"Touch must not make retained lower piece nonsolid");
  w.cancelSurprise();
  SaveGame saved;saved.currentLevel=content.mapPath;saved.levels[content.mapPath].world=w.snapshot();
  std::vector<uint8_t> encoded;SaveGame disk;
  need(encodeSave(saved,encoded,error)&&decodeSave(encoded,disk,error),error.c_str());
  auto restoredContent=authored;GameWorld restored;need(restored.load(restoredContent,arena,error),error.c_str());
  if(!restored.restoreSnapshot(disk.levels.at(content.mapPath).world,error))throw std::runtime_error(error);
  for(auto* game:{&w,&restored}) {
   need(game->actors()[1].solid,"base remains solid after Touch expiry/save");
   game->damage(1,279,0);need(game->actors()[1].alive&&game->actors()[1].vitals.energy==1,"retained base damage budget");
   game->damage(1,1,0);need(!game->actors()[1].alive&&!game->actors()[1].solid,"ordinary damage destroys Touch-surviving base");
  }
 }
 //437ec6: independently test Z-shadow versus In-ground in both deaths.
 for(unsigned flags=0;flags<4;++flags) {
  auto content=fixture();content.objects[1].properties.push_back(value("Altitude","24"));
  ObjectClass fx;fx.id={3};fx.name="Death-FX";fx.behaviours={"Fixed"};fx.flags.zShadow=bool(flags&1);fx.flags.inGround=bool(flags&2);
  fx.properties={value("Life-time","1000"),block("TYPE",{},"FX")};content.classes.push_back(fx);
  content.objects[1].properties.push_back(block("TYPE",{value("Boom-fx","Death-FX")},"Explosion"));
  Level arena;arena.mapW=arena.mapH=100;arena.hasTerrainCollision=true;GameWorld w;std::string error;
  need(w.load(content,arena,error),error.c_str());const auto source=content.objects[1].position;
  w.damage(1,5,0);need(w.actors()[1].turretOnlyBase,"exact-zero upper Energy triggers conversion");
  need(content.objects.size()==3,"first phase constructs authored FX only");
  need(content.objects[2].position.y==source.y+(flags&1?0:4),"first-phase Y offset tests Z-shadow, not In-ground");
  need(w.actors()[2].projectileAltitude==(flags&1?0:24),"first-phase altitude tests Z-shadow, not In-ground");
  w.damage(1,279,0);need(w.actors()[1].alive&&w.actors()[1].vitals.energy==1,"base exact class damage budget minus1 survives");
  w.damage(1,1,0);need(!w.actors()[1].alive,"exact-zero base Energy destroys it");
  need(content.objects.back().classId==fx.id&&content.objects.back().position.y==source.y,"second phase retains same FX class without Y offset");
  need(w.actors().back().projectileAltitude==(flags&1?0:24),"second-phase native height rule");
 }
 auto s=fixture();Level level;level.mapW=level.mapH=100;level.hasTerrainCollision=true;std::string e;GameWorld world;need(world.load(s,level,e),e.c_str());
 world.damage(1,1000,0,true);
 need(world.actors()[1].alive&&world.actors()[1].solid&&world.actors()[1].turretOnlyBase,"upper destruction retains a solid live base");
 need(world.actors()[1].vitals.energy==80&&world.actors()[1].vitals.shield==200,"base restores class initial energy/shield rather than placement values");
 need(world.actors()[1].brain.base==BrainBase::Fixed&&!world.actors()[1].brain.seeker&&!world.actors()[1].brain.tracker,"base cannot seek or fire");
 need(s.objects.size()==2&&world.enemiesRemaining()==0&&world.snapshot().bonusEnemies==0,"upper death preserves drops but removes the enemy from native counter");
 bool firstNotice=false;for(const auto& n:world.notifications().entries())firstNotice|=n.text.find("Real Killer")!=std::string::npos;
 need(firstNotice,"last-enemy notice occurs at gun destruction, despite surviving solid base");
 need(world.player().vitals.money==100017&&world.player().vitals.lifetimeMoney==100017,"first phase credits clear bonus and ordinary reward to both earnings counters");
 need(world.snapshot().fastKillCount==1,"upper phase qualifies for radial Fast Kill");
 SaveGame save;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=world.snapshot();std::vector<uint8_t> bytes;SaveGame disk;need(encodeSave(save,bytes,e)&&decodeSave(bytes,disk,e),e.c_str());
 auto resumedScene=fixture();GameWorld resumed;need(resumed.load(resumedScene,level,e)&&resumed.restoreSnapshot(disk.levels.at(s.mapPath).world,e),e.c_str());
 need(resumed.actors()[1].turretOnlyBase&&resumed.actors()[1].alive&&!resumed.actors()[1].brain.seeker,"disk restore derives Only-base without new save fields");
 need(!resumed.actors()[1].hudEnemy&&resumed.enemiesRemaining()==0&&resumed.snapshot().bonusEnemies==0,"disk counter reconstruction excludes live Fixed base");
 for(GameWorld* w:{&world,&resumed}) {
  w->damage(1,50,0,true);need(w->actors()[1].alive&&w->actors()[1].vitals.shield==150,"base requires additional damage");
  const auto beforeCombo=w->snapshot().fastKillCount;w->damage(1,1000,0,true);
  need(!w->actors()[1].alive&&!w->actors()[1].solid&&w->enemiesRemaining()==0,"second phase destroys base");
  need(w->snapshot().fastKillCount==beforeCombo,"base death cannot count twice toward Fast Kill");
  need(w->player().vitals.money==100034&&w->player().vitals.lifetimeMoney==100034,"base destruction pays only second ordinary reward, without repeating clear bonus");
  need(w->snapshot().objects.size()==3,"HAVE releases exactly once on second phase");
  need(w->snapshot().bonusEnemies==0,"base destruction does not decrement or announce native zero transition twice");
 }
 auto base=fixture();base.objects[1].properties.push_back(value("Only-base","1"));GameWorld only;need(only.load(base,level,e),e.c_str());need(only.actors()[1].turretOnlyBase,"authored Only-base accepted");only.damage(1,1000,0,true);need(!only.actors()[1].alive,"authored base has only one destruction phase");
 auto ordinary=fixture();ordinary.objects[1].properties.push_back(value("Two-pieces","0"));GameWorld single;need(single.load(ordinary,level,e),e.c_str());single.damage(1,1000,0,true);need(!single.actors()[1].alive,"non Two-pieces remains single phase");
 std::cout<<"Turret two-phase destruction, class reset, drops, wallet, Fast Kill, last enemy and disk continuation passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
