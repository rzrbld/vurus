#include "snapshot_fixture.h"
#include "game/save_file.h"
#include "game/save_state.h"
#include "host_temp.h"
#include "game/triple_fire.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace {
void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
TextNode value(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(text)};return n;}
TextNode block(std::string key,std::vector<TextNode> fields,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=std::move(key);n.children=std::move(fields);if(!type.empty())n.arguments={std::move(type)};return n;}
struct Run {
 SceneContent scene;Level level;GameWorld world;std::string error;
 Run(bool paired=true,bool backup=false,bool visual=false,bool randomStart=false){
  scene.mapPath="TEST/TRIPLE-AMMO.3DG";
  ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};p.properties={value("Sub-type","Vyruz"),value("Bounding-box-size","12"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("Shield",{value("Initial","100"),value("Max","100")}),block("ADD",{value("Name","Gun")},"Weapon"),block("ADD",{value("Name","Bullet"),value("Count","50"),value("Max","50")},"Ammo")};
  ObjectClass gun;gun.id={1};gun.name="Gun";gun.properties={block("TYPE",{value("Ammo","Bullet"),value("Delay","98"),value("Initial-slope","-33"),value("Slope-step","0.5"),value("Weapons-gap",paired?"20":"0"),value("Left-side","1"),value("Right-side","1"),value("Fire-altitude","24"),value("Start-distance",randomStart?"0":"100")},"Weapon")};
  ObjectClass ammo;ammo.id={2};ammo.name="Bullet";ammo.properties={value("Speed-max","100"),value("Altitude","24"),block("TYPE",{value("Damage","1"),value("Ignore-owner","1")},"Ammo")};
  if(visual)ammo.sprite.sequence={1};
  scene.classes={p,gun,ammo};
  if(backup){auto other=gun;other.id={3};other.name="Backup";other.properties={block("TYPE",{value("Ammo","OtherBullet"),value("Delay","98"),value("Fire-altitude","24"),value("Start-distance",randomStart?"0":"100")},"Weapon")};auto stock=ammo;stock.id={4};stock.name="OtherBullet";scene.classes.push_back(other);scene.classes.push_back(stock);scene.classes[0].properties.push_back(block("ADD",{value("Name","Backup")},"Weapon"));scene.classes[0].properties.push_back(block("ADD",{value("Name","OtherBullet"),value("Count","5"),value("Max","5")},"Ammo"));}
  ObjectPlacement player;player.id={0};player.classId={0};player.position={1000,1000};player.properties=scene.classes[0].properties;player.behaviours=scene.classes[0].behaviours;scene.objects={player};scene.player=ObjectId{0};level.mapW=level.mapH=100;level.hasTerrainCollision=true;
  need(vyruz_test::loadManagedFixture(world,scene,level,error),error);world.setDebugEnemyAI(false);world.setPlayerDirection(9);
  auto state=world.snapshot();state.surprise.kind=SurpriseKind::Triple;state.surprise.applied=true;state.surprise.remainingMs=10000;state.surprise.tripleOffset=400;restore(state);
 }
 void restore(const WorldSnapshot& state){need(world.restoreSnapshot(state,error),error);}
 void stock(unsigned count){auto s=world.snapshot();s.actors[0].ammo.at("bullet").count=count;restore(s);}
 void tick(bool held=false){WorldInput in;in.fire=held;world.tick(1.f/60,in);}
};
std::vector<uint8_t> bytes(const Run& r){SaveGame s;s.currentLevel=r.scene.mapPath;s.levels[s.currentLevel].world=r.world.snapshot();std::vector<uint8_t> b;std::string e;need(encodeSave(s,b,e),e);return b;}
}
int main(){try{
 for(int heading=0;heading<24;++heading)for(bool paired:{false,true})for(unsigned stock:{1u,2u,10u}){
  Run ordinary(paired),triple(paired);auto plain=ordinary.world.snapshot();plain.surprise={};ordinary.restore(plain);ordinary.world.setPlayerDirection(heading);ordinary.tick(true);
  triple.world.setPlayerDirection(heading);triple.stock(stock);triple.tick(true);
  const auto& shots=triple.world.projectiles();const size_t expected=stock==1?1:(paired?6:3);
  need(shots.size()==expected,"enabled barrels expand before charging the final round: heading="+std::to_string(heading)+" paired="+std::to_string(paired)+" stock="+std::to_string(stock)+" count="+std::to_string(shots.size())+" fired="+std::to_string(triple.world.shotsFired));need(triple.world.player().ammo.at("bullet").count==stock-1,"active Triple charges once per volley");
  for(size_t i=0;i<shots.size();++i){const auto center=ordinary.world.projectiles()[i/3].position;const int side=i%3==0?1:i%3==1?-1:0;const auto offset=tripleFireOffset(heading,400,side);
   need(std::abs(shots[i].position.x-center.x-offset.x)<.003f&&std::abs(shots[i].position.y-center.y-offset.y)<.003f,"native order is body+6, body-6, center within each barrel");
   need(shots[i].directionX==ordinary.world.projectiles()[i/3].directionX&&shots[i].directionZ==ordinary.world.projectiles()[i/3].directionZ,"copies preserve selected firing direction");
  }
 }
 {Run r(true,false,true);r.stock(2);r.tick(true);need(r.world.shotsFired==6&&r.world.player().ammo.at("bullet").count==1&&r.world.actors().size()>1,"visual actor growth retains original stock identity");}
 {Run r(true,true);r.stock(1);r.tick(true);need(r.world.shotsFired==1&&r.world.player().selectedWeapon==1&&r.world.player().ammo.at("otherbullet").count==5,"replacement weapon waits until the next call");for(int i=0;i<10;++i)r.tick();r.tick(true);need(r.world.shotsFired==4&&r.world.player().ammo.at("otherbullet").count==4,"replacement weapon emits ordered Triple on next call");}
 {Run r;auto s=r.world.snapshot();s.actors[0].ammo.slots[0].second.count=1;s.actors[0].ammo.append("bullet",{5,5});r.restore(s);r.tick(true);need(r.world.shotsFired==1&&r.world.player().weapons[0].ammoSlot==1&&r.world.player().ammo.slots[1].second.count==5,"compatible stock is not spent in the old volley");}
 // Original forward-distance randomness belongs to the barrel, not each copy.
 {Run ordinary(true,false,false,true),triple(true,false,false,true);auto s=ordinary.world.snapshot();s.surprise={};ordinary.restore(s);ordinary.tick(true);triple.tick(true);const auto a=ordinary.world.snapshot().nativeWorldRandom,b=triple.world.snapshot().nativeWorldRandom;
  need(a.seed==b.seed&&a.counter==b.counter,"Triple consumes one start-distance draw per enabled barrel");const auto& shots=triple.world.projectiles();need(shots.size()==6,"random-start Triple retains both triples");for(size_t i=0;i<6;++i){const auto origin=ordinary.world.projectiles()[i/3].position;const auto offset=tripleFireOffset(9,400,i%3==0?1:i%3==1?-1:0);need(std::abs(shots[i].position.x-origin.x-offset.x)<.003f&&std::abs(shots[i].position.y-origin.y-offset.y)<.003f,"copies reuse their own barrel start draw");}}
 // Contraction leaves actor+114 nonzero after the shared-charge flag expires.
 {Run r;auto s=r.world.snapshot();s.surprise.remainingMs=0;r.restore(s);r.stock(2);r.tick(true);need(r.world.shotsFired==2&&r.world.player().ammo.at("bullet").count==0,"expired Triple charges each surviving lateral copy");}
 // The first expansion tick also shares a charge across both barrels.
 {Run r;auto s=r.world.snapshot();s.surprise.tripleOffset=0;r.restore(s);r.stock(2);r.tick(true);need(r.world.shotsFired==6&&r.world.player().ammo.at("bullet").count==1,"first expansion tick shares stock across enabled barrels");}
 {Run r,resumed;r.stock(2);r.tick(true);resumed.restore(r.world.snapshot());for(int i=0;i<20;++i){r.tick(i==10);resumed.tick(i==10);need(bytes(r)==bytes(resumed),"Triple memory continuation matches complete encoded state");}
  const auto dir=hostTempDirectory("vurus-triple-ammo-");const auto file=dir/"state.vzs";SaveGame save,read;save.currentLevel=r.scene.mapPath;save.levels[save.currentLevel].world=r.world.snapshot();need(writeSaveFile(file.string(),save,r.error)&&readSaveFile(file.string(),read,r.error),r.error);const auto& disk=read.levels.at(save.currentLevel).world;need(disk.surprise.kind==SurpriseKind::None&&disk.surprise.tripleOffset==0,"existing disk Surprise normalization preserved");resumed.restore(disk);resumed.tick();std::filesystem::remove_all(dir);
 }
 std::cout<<"PASS Triple 24-heading final-round/order, paired barrels, replacement, expiry, actor growth and continuation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
