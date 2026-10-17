// Public stop/drop/depart regression. Optional DATA argument also checks the
// resolved original player/Mine-Dropper/Mine-Ammo in every shipped map.
#include "game/world.h"
#include "game/save_file.h"
#include "game/save_state.h"
#include "game/properties.h"
#include "game/menu.h"
#include "io/vfs.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string val){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(val)};return n;}
static TextNode block(std::string key,std::vector<TextNode> children,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=std::move(key);n.children=std::move(children);if(!type.empty())n.arguments={std::move(type)};return n;}
static SceneContent synthetic(){
 SceneContent s;s.mapPath="TEST/MINE.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Sub-type","Vyruz"),value("Bounding-box-size","48"),block("Energy",{value("Initial","10000"),value("Max","10000")})};
 ObjectClass gun;gun.id={1};gun.name="Mine-Dropper";gun.properties={block("TYPE",{value("Ammo","Mine-Ammo"),value("Delay","1000")},"Weapon")};
 ObjectClass mine;mine.id={2};mine.name="Mine-Ammo";mine.behaviours={"Fixed"};mine.sprite.sequence.value=1;mine.properties={value("Sub-type","Ammo"),value("Bounding-box-size","20"),value("Speed-max","0"),value("Keep-bb-when-invisible","1"),block("Energy",{value("Initial","100"),value("Max","100")}),block("Thrust",{value("Initial","0")}),block("TYPE",{value("Damage","0"),value("Damage-zone","0"),value("Active-after","5000"),value("Damage-after","30000"),value("Ignore-owner","0")},"Ammo")};
 player.properties.push_back(value("Managed","1"));mine.properties.push_back(value("Managed","1"));
 s.classes={player,gun,mine};ObjectPlacement p;p.id={0};p.classId={0};p.position={4000,3000};p.properties=player.properties;p.behaviours=player.behaviours;
 p.properties.push_back(block("ADD",{value("Name","Mine-Dropper")},"Weapon"));p.properties.push_back(block("ADD",{value("Name","Mine-Ammo"),value("Count","100"),value("Max","100")},"Ammo"));s.objects={p};s.player=ObjectId{0};return s;
}
static std::vector<uint8_t> bytes(const GameWorld& w){SaveGame s;s.currentLevel="TEST/MINE.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string e;need(encodeSave(s,b,e),e);return b;}
static void run(SceneContent original,const std::string& label,int hz,int heading,int waitMs){
 // Original classes and resolved player overrides, isolated from terrain/AI.
 auto player=original.objects[original.player->value];player.id={0};player.owner.reset();player.position={4000,3000};original.objects={player};original.player=ObjectId{0};
 auto scene=original;Level l;l.mapW=l.mapH=125;l.hasTerrainCollision=true;GameWorld w;std::string e;need(w.load(scene,l,e),e);w.setDebugEnemyAI(false);w.setDebugGodMode(true);w.setPlayerDirection(heading);
 auto setup=w.snapshot();auto&a=setup.actors[0];size_t slot=SIZE_MAX;
 for(size_t n=0;n<a.weapons.size();++n)if(contentNameEqual(a.weapons[n].ammo,"Mine-Ammo"))slot=n;
 need(slot!=SIZE_MAX,label+": Mine-Dropper equipped");a.selectedWeapon=slot;a.weapons[slot].state.lastFireMs.reset();auto&stock=a.ammo[a.weapons[slot].ammo];stock.count=stock.maximum=100;
 need(w.restoreSnapshot(setup,e),e);w.setDebugEnemyAI(false);w.setDebugGodMode(true);
 const float dt=1.f/hz;WorldInput fire;fire.fire=true;w.tick(dt,fire);
 need(w.projectiles().size()==1&&w.projectiles()[0].visual,label+": one real dropped mine");const size_t visual=*w.projectiles()[0].visual;
 need(w.projectiles()[0].activeAfterMs==5000&&w.projectiles()[0].inactiveMs>4900,label+": authored initial wait");
 for(int n=0;n<hz*waitMs/1000;++n){w.tick(dt,{});need(w.projectiles().size()==1&&w.actors()[visual].alive,label+": inactive mine detonated while stopped at "+std::to_string((n+2)*1000/hz)+" ms");}
 if(waitMs>=500)need(w.projectiles()[0].ageMs>=500,label+": owner grace expired while stopped");
 const auto start=w.playerPosition();WorldInput move;move.movement.forward=1;
 for(int n=0;n<hz;++n){w.tick(dt,move);need(w.actors()[visual].alive,label+": departure detonated inactive mine at "+std::to_string(n)+" ticks");}
 const auto finish=w.playerPosition();need(nativeDistanceSquared(finish.x-start.x,finish.y-start.y)>10000,label+": actual player departs");
 need(w.projectiles().size()==1&&w.projectiles()[0].damageRemainingMs==30000,label+": arming pauses fuse");
 // Repeated crossing after owner grace, including decoded save with the live
 // graph and graph-absent legacy reconstruction. Do not test teleport overlap.
 for(bool absentGraph:{false,true}){
  SaveGame saved;need(decodeSave(bytes(w),saved,e),e);auto snapshot=saved.levels.begin()->second.world;if(absentGraph)snapshot.spiderNavigation.reset();
  auto copy=original;GameWorld resumed;need(resumed.load(copy,l,e)&&resumed.restoreSnapshot(snapshot,e),e);resumed.setDebugEnemyAI(false);resumed.setDebugGodMode(true);
  const auto p=resumed.projectiles()[0].position;resumed.setPlayerPosition(p.x-52,p.y);resumed.movePlayer(12,0);
  need(resumed.actors()[visual].alive,label+": saved inactive mine detonates on crossing");
 }
 while(w.projectiles()[0].ageMs<4900)w.tick(dt,{});
 const auto almostArmed=w.projectiles()[0].position;
 w.setPlayerPosition(almostArmed.x-52,almostArmed.y);w.movePlayer(12,0);
 need(w.actors()[visual].alive&&w.projectiles()[0].inactiveMs>0,label+": contact before full five-second wait is inert");
 // Native bounds reactivate at timeout. The same contact must then consume
 // the mine; Keep-bb-when-invisible must not become permanent owner immunity.
 const auto p=w.projectiles()[0].position;w.setPlayerPosition(p.x+500,p.y+300);
 for(int n=0;n<hz*5&&w.projectiles()[0].inactiveMs!=0;++n)w.tick(dt,{});
 need(w.projectiles().size()==1&&w.projectiles()[0].inactiveMs==0,label+": mine activates");
 w.setPlayerPosition(p.x-52,p.y);w.movePlayer(12,0);need(!w.actors()[visual].alive,label+": armed mine still detonates on owner contact");
}
int main(int argc,char**argv){try{
 unsigned cases=0;
 if(argc==2){Vfs v;need(v.init(argv[1]),v.error());ContentReader reader=[&](const auto&p,auto&b){return v.read(p,b);};std::string e;
  for(const auto&map:debugLevels()){SceneContent s;need(loadSceneContent(reader,map,s,e),e);for(int hz:{60,90})for(int heading:{0,6,12,18})for(int wait:{0,600}){run(s,map,hz,heading,wait);++cases;}}
 }else{need(argc==1,"usage: mine_arming_world_test [ORIGINAL_DATA]");for(int hz:{60,90})for(int heading=0;heading<24;++heading)for(int wait:{0,600}){run(synthetic(),"synthetic",hz,heading,wait);++cases;}}
 std::cout<<"PASS mine stop/drop/depart, inactive disk/graph rebuild and armed owner contact: "<<cases<<" cases\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
