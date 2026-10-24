#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_file.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool v,const std::string&why){if(!v)throw std::runtime_error(why);}
static TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
static TextNode block(std::string k,std::vector<TextNode> c,std::string a={}){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=c;if(!a.empty())n.arguments={a};return n;}
static SceneContent scene(){
 SceneContent s;s.mapPath="TEST/GUIDANCE-ORDER.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Altitude","24"),value("Bounding-box-size","12"),block("Energy",{value("Initial","1000")}),block("ADD",{value("Name","Launcher")},"Weapon"),block("ADD",{value("Name","Missile"),value("Count","100"),value("Max","100")},"Ammo")};
 ObjectClass gun;gun.id={1};gun.name="Launcher";gun.properties={block("TYPE",{value("Ammo","Missile"),value("Delay","1"),value("Start-distance","250")},"Weapon")};
 ObjectClass ammo;ammo.id={2};ammo.name="Missile";ammo.behaviours={"Tracker"};ammo.properties={value("Sub-type","Ammo"),value("Bounding-box-size","4"),value("Altitude","24"),value("Speed-max","10"),block("TYPE",{value("Damage","1"),value("Damage-after","10000"),value("Ignore-owner","1")},"Ammo"),block("TYPE",{value("Scan-Delay","1"),value("Sensor-Range","1000"),value("Vision-Angle","360")},"Tracker")};
 ObjectClass target;target.id={3};target.name="Target";target.behaviours={"Seeker"};target.properties={value("Sub-type","Cop"),value("Bounding-box-size","8"),block("Energy",{value("Initial","1000")}),block("TYPE",{value("Target","Player")},"Seeker")};
 s.classes={player,gun,ammo,target};ObjectPlacement p;p.id={0};p.classId={0};p.position={2500,2000};p.properties=player.properties;p.behaviours=player.behaviours;s.objects={p};s.player=ObjectId{0};return s;
}
int main(){try{
 auto s=scene();Level l;l.mapW=l.mapH=200;l.hasTerrainCollision=true;GameWorld w;std::string e;
 need(vyruz_test::loadManagedFixture(w,s,l,e),e);w.setDebugEnemyAI(false);w.setPlayerDirection(9);
 const auto a=w.spawn({3},{2850,2000}),b=w.spawn({3},{2850,2000});
 need(w.actors()[a].hudEnemy&&w.actors()[b].hudEnemy,"fixture targets qualify as enemies");
 auto base=w.snapshot();const auto cell=base.actorCells->coordinate(2850,2000);
 need(base.actorCells->heads[cell]==b&&base.actorCells->next[b]==a,"newer birth is native cell head");
 const auto fire=[&](size_t want){WorldInput in;in.fire=true;w.tick(.014f,in);need(!w.projectiles().empty()&&w.projectiles().back().guidanceTarget==want,"World launch respects native cell head");};
 fire(b);
 // Leave/re-enter the cell reverses list order without changing IDs.
 need(w.restoreSnapshot(base,e),e);auto changed=w.snapshot();changed.objects[a].position={3000,2000};vyruz_test::synchronizeActor(changed,a);changed.objects[a].position={2850,2000};vyruz_test::synchronizeActor(changed,a);
 need(w.restoreSnapshot(changed,e),e);w.setDebugEnemyAI(false);fire(a);
 for(unsigned tick=0;tick<16&&!w.projectiles().empty()&&
     w.projectiles()[0].guidance.phase!=MissileGuidancePhase::Idle;++tick)w.tick(.014f,{});
 need(!w.projectiles().empty()&&w.projectiles()[0].guidance.phase==MissileGuidancePhase::Idle,"blocked tied-target route returns to Idle before reacquisition");
 // A fresh tracking search must use the same list order, even if its previous
 // target has a lower arrival rank. The overlapping targets obstruct the route,
 // so wait for its normal failure/rollback before requesting another search.
 auto reacquire=w.snapshot();need(reacquire.projectiles.size()==1,"one tracking fixture projectile");
 reacquire.projectiles[0].guidanceTarget=b;
 reacquire.projectiles[0].guidance.phase=MissileGuidancePhase::Idle;
 reacquire.projectiles[0].guidance.scanRemainingMs=0;
 need(w.restoreSnapshot(reacquire,e),e);w.setDebugEnemyAI(false);w.tick(.014f,{});
 need(!w.projectiles().empty()&&w.projectiles()[0].guidanceTarget==a,"World tracking rescan respects native cell head");
 // Serialized arrival ordinals retain that order after reconstruction.
 SaveGame save,read;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=changed;std::vector<uint8_t> bytes;
 need(encodeSave(save,bytes,e)&&decodeSave(bytes,read,e),e);auto resumedScene=scene();GameWorld resumed;
 need(vyruz_test::loadManagedFixture(resumed,resumedScene,l,e)&&resumed.restoreSnapshot(read.levels.at(s.mapPath).world,e),e);resumed.setDebugEnemyAI(false);
 WorldInput in;in.fire=true;resumed.tick(.014f,in);need(!resumed.projectiles().empty()&&resumed.projectiles().back().guidanceTarget==a,"disk continuation retains native target order");
 auto liveScene=scene();GameWorld live;
 need(vyruz_test::loadManagedFixture(live,liveScene,l,e)&&live.restoreSnapshot(changed,e),e);live.setDebugEnemyAI(false);live.tick(.014f,in);
 const auto encoded=[&](const GameWorld& world){SaveGame state;state.currentLevel=s.mapPath;state.levels[s.mapPath].world=world.snapshot();std::vector<uint8_t> data;need(encodeSave(state,data,e),e);return data;};
 for(unsigned tick=0;tick<60;++tick){need(encoded(live)==encoded(resumed),"full-state guided disk continuation");live.tick(.014f,{});resumed.tick(.014f,{});}
 // All four-target arrival permutations must beat actor-ID order. A move
 // inside the cell must not change that order; real departures/re-entry do.
 auto manyScene=scene();GameWorld many;
 need(vyruz_test::loadManagedFixture(many,manyScene,l,e),e);many.setDebugEnemyAI(false);many.setPlayerDirection(9);
 std::array<size_t,4> order{};for(auto& id:order)id=many.spawn({3},{2850,2000});
 const auto manyBase=many.snapshot();unsigned permutations=0;
 do {
  auto state=manyBase;
  for(auto id:order){state.objects[id].position={3000,2000};vyruz_test::synchronizeActor(state,id);state.objects[id].position={2850,2000};vyruz_test::synchronizeActor(state,id);}
  state.objects[order.front()].position.x+=1;vyruz_test::synchronizeActor(state,order.front());
  need(state.actorCells->heads[cell]==order.back(),"same-cell movement preserves arrival head");
  SaveGame checkpoint,decoded;checkpoint.currentLevel=manyScene.mapPath;checkpoint.levels[manyScene.mapPath].world=state;
  need(encodeSave(checkpoint,bytes,e)&&decodeSave(bytes,decoded,e),e);
  need(many.restoreSnapshot(decoded.levels.at(manyScene.mapPath).world,e),e);many.setDebugEnemyAI(false);many.tick(.014f,in);
  need(!many.projectiles().empty()&&many.projectiles().back().guidanceTarget==order.back(),"four-target arrival permutation survives disk continuation");
  ++permutations;
 }while(std::next_permutation(order.begin(),order.end()));
 need(permutations==24,"all four-target permutations exercised");
 std::cout<<"World Missile target order: birth, cell re-entry, tracking rescan and disk reconstruction passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
