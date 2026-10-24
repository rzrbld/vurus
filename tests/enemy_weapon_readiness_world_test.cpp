#include "game/world.h"
#include "game/save_file.h"
#include "game/spider_turn_native.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include "host_temp.h"
using namespace vyruz;
namespace {
void need(bool b,const std::string& m){if(!b)throw std::runtime_error(m);}
TextNode value(std::string key,std::string v){TextNode n;n.key=key;n.kind=TextKind::Assignment;n.values={v};return n;}
TextNode block(std::string key,std::vector<TextNode> c,std::string arg={}){TextNode n;n.key=key;n.kind=TextKind::Block;n.children=std::move(c);if(!arg.empty())n.arguments={arg};return n;}
SceneContent fixture(const std::string& family,bool one=false){
 SceneContent s;s.mapPath="TEST/ENEMY-READY.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Managed","1"),value("Sub-type","Vyruz"),value("Bounding-box-size","10"),block("Energy",{value("Initial","1000000"),value("Max","1000000")}),block("ADD",{value("Name","Gun")},"Weapon"),block("ADD",{value("Name","Bullet"),value("Count","10000"),value("Max","10000")},"Ammo")};
 ObjectClass enemy;enemy.id={1};enemy.name=family;enemy.behaviours={"Fixed","Seeker"};enemy.properties={value("Managed","1"),value("Sub-type",family),value("Can-turn-on-place","1"),value("Turn-timeout","1"),value("Two-pieces","0"),value("Visible-when-firing","1"),value("Bounding-box-size","25"),value("Speed-max","0"),value("Speed-min","0"),value("Use-inertia","0"),block("Energy",{value("Initial","1000000"),value("Max","1000000")}),block("TYPE",{value("Target","Player"),value("Sensor-Range","1000"),value("Min-distance","200"),value("Scan-Delay","1000"),value("Vision-Angle","360")},"Seeker"),block("ADD",{value("Name","Gun")},"Weapon"),block("ADD",{value("Name","Bullet"),value("Count","10000"),value("Max","10000")},"Ammo")};
 ObjectClass gun;gun.id={2};gun.name="Gun";gun.properties={block("TYPE",{value("Ammo","Bullet"),value("Delay","150"),value("One-by-one",one?"1":"0"),value("Fire-Range","1000"),value("Start-distance","1"),value("Fire-altitude","24")},"Weapon")};
 ObjectClass ammo;ammo.id={3};ammo.name="Bullet";ammo.behaviours={"Straight"};ammo.properties={value("Managed","1"),value("Invisible","1"),value("Sub-type","Ammo"),value("Bounding-box-size","0"),value("Speed-max","0"),value("Life-time","10000"),block("TYPE",{value("Ignore-owner","1"),value("Damage","0")},"Ammo")};
 s.classes={player,enemy,gun,ammo};for(unsigned i=0;i<2;++i){ObjectPlacement o;o.id={i};o.classId={i};o.position={1000,1000};o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;s.objects.push_back(o);}s.player=ObjectId{0};return s;
}
Level arena(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;return l;}
void drain(GameWorld&w){w.takeAudioEvents();}
void prepare(GameWorld&w,uint32_t raw=0){auto state=w.snapshot();auto&a=state.actors[1];a.direction=6;a.nativeAmmoOwner.activeAmmo=raw;if(a.enemyTurn)a.enemyTurn=initializeEnemyTurn(a.enemyTurnConfig,6,0);if(a.spiderAI&&a.spiderAI->turn)a.spiderAI->turn=initializeSpiderTurn(a.enemyTurnConfig,6);std::string e;need(w.restoreSnapshot(state,e),e);drain(w);const auto center=spiderSurfaceToNative(1000,1000);const auto target=spiderNativeToSurface(center.x+100,center.y);w.setPlayerPosition(target.x,target.y);}
uint64_t firstShot(GameWorld&w,unsigned limit=150){for(unsigned t=0;t<limit;++t){w.tick(.014f,{});drain(w);if(w.shotsFired)return *w.actors()[1].weapons[0].state.lastFireMs;}throw std::runtime_error("No first enemy volley");}
std::vector<uint8_t> encoded(GameWorld&w){SaveGame s;s.currentLevel="TEST/ENEMY-READY.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string e;need(encodeSave(s,b,e),e);return b;}
}
int main(){try{
 unsigned families=0;
 for(const std::string family:{"Cop","Turret","Walker","Burner","Jeep","Roller","Heli","Tank","Spider"}){
  for(bool one:{false,true})for(uint32_t raw:{0u,1u,0xffffffffu}){
   auto s=fixture(family,one);const auto l=arena();GameWorld w;std::string e;need(w.load(s,l,e),e);prepare(w,raw);const auto first=firstShot(w);const bool bypass=one&&raw==0;
   std::cout<<family<<" one="<<one<<" raw="<<raw<<" first="<<first<<'\n';
   need(bypass?first<150:first>=150,"Native initial enemy readiness "+family);
   const auto prior=*w.actors()[1].weapons[0].state.lastFireMs;const auto shots=w.shotsFired;
   for(unsigned tick=0;tick<10;++tick){w.tick(.014f,{});drain(w);}
   // Native successful construction increments the raw uint32 word. Starting
   // atUINT_MAX wraps tozero, so One-by-one bypasses once more on the nextvisit.
   const bool wrappedZero=one&&raw==0xffffffffu;
   need(w.shotsFired==shots+unsigned(wrappedZero),"Later cadence and raw-counter wrap "+family);
   need(*w.actors()[1].weapons[0].state.lastFireMs==prior+(wrappedZero?14:0),"Cooldown timestamp changes only on admitted volley");
  }++families;
 }
 // Readiness clears the fire bit without changing admitted/revealed target state.
 {auto s=fixture("Spider");const auto l=arena();GameWorld w;std::string e;need(w.load(s,l,e),e);prepare(w);for(int t=0;t<8;++t){w.tick(.014f,{});drain(w);}need(w.shotsFired==0&&w.actors()[1].enemyRevealed&&!w.actors()[1].nativeFireRequested,"Spider reveal admission preserved before readiness");}
 // Player first-input policy remains distinct from the enemy aim gate.
 {auto s=fixture("Cop");GameWorld w;std::string e;const auto l=arena();need(w.load(s,l,e),e);w.setDebugEnemyAI(false);WorldInput in;in.fire=true;w.tick(.014f,in);drain(w);need(w.shotsFired==1&&*w.actors()[0].weapons[0].state.lastFireMs==14,"Accepted Player immediate first input preserved");}
 // A newly born enemy does not wait Delay from its own birth.
 {auto s=fixture("Cop");s.objects.resize(1);GameWorld w;const auto l=arena();std::string e;need(w.load(s,l,e),e);for(unsigned t=0;t<72;++t){w.tick(.014f,{});drain(w);}const auto birth=w.elapsedMilliseconds();need(w.spawn({1},{1000,1000})==1,"Late enemy birth");prepare(w);const auto first=firstShot(w);need(first>=birth&&first<birth+150,"Absolute World clock, not age-since-birth");}
 // Exact same-platform continuation across the initial-not-ready boundary.
 {auto s=fixture("Cop"),r=s;GameWorld live,resumed;const auto l=arena();std::string e;need(live.load(s,l,e),e);prepare(live);for(int t=0;t<8;++t){live.tick(.014f,{});drain(live);}need(live.shotsFired==0,"Checkpoint before initial readiness");
  auto dir=hostTempDirectory("enemy-ready");SaveGame save,read;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=live.snapshot();need(writeSaveFile((dir/"save.vzs").string(),save,e)&&readSaveFile((dir/"save.vzs").string(),read,e),e);need(resumed.load(r,l,e)&&resumed.restoreSnapshot(read.levels.at(s.mapPath).world,e),e);drain(resumed);
  for(unsigned t=0;t<60;++t){for(auto*w:{&live,&resumed}){w->tick(.014f,{});drain(*w);}need(encoded(live)==encoded(resumed),"Initial readiness disk continuation");}std::filesystem::remove_all(dir);
 }
 std::cout<<"PASS"<<families<<"enemy families,54initial admissions,Player,latebirth and60disk ticks\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
