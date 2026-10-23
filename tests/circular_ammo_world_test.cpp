#include "snapshot_fixture.h"
#include "game/save_file.h"
#include "game/save_state.h"
#include "host_temp.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace {
void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
TextNode value(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(text)};return n;}
TextNode block(std::string key,std::vector<TextNode> fields,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=std::move(key);n.children=std::move(fields);if(!type.empty())n.arguments={std::move(type)};return n;}
struct Run {
 SceneContent scene;Level level;GameWorld world;std::string error;
 Run(bool paired=true,bool backup=false,bool visual=false){
  scene.mapPath="TEST/CIRCULAR-AMMO.3DG";
  ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};p.properties={value("Sub-type","Vyruz"),value("Bounding-box-size","12"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("Shield",{value("Initial","100"),value("Max","100")}),block("ADD",{value("Name","Gun")},"Weapon"),block("ADD",{value("Name","Bullet"),value("Count","50"),value("Max","50")},"Ammo")};
  ObjectClass gun;gun.id={1};gun.name="Gun";gun.properties={block("TYPE",{value("Ammo","Bullet"),value("Delay","98"),value("Initial-slope","-33"),value("Slope-step","0.5"),value("Weapons-gap",paired?"20":"0"),value("Left-side","1"),value("Right-side","1"),value("Start-distance","100")},"Weapon")};
  ObjectClass ammo;ammo.id={2};ammo.name="Bullet";ammo.properties={value("Speed-max","100"),value("Altitude","24"),block("TYPE",{value("Damage","1"),value("Ignore-owner","1")},"Ammo")};
  if(visual)ammo.sprite.sequence={1};
  scene.classes={p,gun,ammo};
  if(backup){auto other=gun;other.id={3};other.name="Backup";other.properties={block("TYPE",{value("Ammo","OtherBullet"),value("Delay","98"),value("Start-distance","100")},"Weapon")};auto stock=ammo;stock.id={4};stock.name="OtherBullet";scene.classes.push_back(other);scene.classes.push_back(stock);scene.classes[0].properties.push_back(block("ADD",{value("Name","Backup")},"Weapon"));scene.classes[0].properties.push_back(block("ADD",{value("Name","OtherBullet"),value("Count","5"),value("Max","5")},"Ammo"));}
  ObjectPlacement player;player.id={0};player.classId={0};player.position={1000,1000};player.properties=scene.classes[0].properties;player.behaviours=scene.classes[0].behaviours;scene.objects={player};scene.player=ObjectId{0};level.mapW=level.mapH=100;level.hasTerrainCollision=true;
  need(vyruz_test::loadManagedFixture(world,scene,level,error),error);world.setDebugEnemyAI(false);world.setPlayerDirection(9);
  need(world.debugSetSurprise(SurpriseKind::Circular),"Circular accepted");for(int i=0;i<9;++i)world.tick(.0625f,{});need(world.surprise().applied,"Circular applied");
 }
 void restore(const WorldSnapshot& state){need(world.restoreSnapshot(state,error),error);}
 void stock(unsigned count){auto s=world.snapshot();s.actors[0].ammo.at("bullet").count=count;restore(s);}
 void tick(bool held=false){WorldInput in;in.fire=held;world.tick(1.f/60,in);}
 void finish(){for(int i=0;i<30&&world.surprise().circularRemaining;++i)tick();need(world.surprise().circularRemaining==0,"sweep terminates");}
};
std::vector<uint8_t> bytes(const Run& r){SaveGame s;s.currentLevel=r.scene.mapPath;s.levels[s.currentLevel].world=r.world.snapshot();std::vector<uint8_t> b;std::string e;need(encodeSave(s,b,e),e);return b;}
}
int main(){try{
 for(bool paired:{false,true})for(unsigned stock:{1u,2u,3u,50u}){
  Run r(paired);r.stock(stock);r.tick(true);
  if(stock==1){need(r.world.shotsFired==1,"last round still creates its first bullet");need(r.world.surprise().circularRemaining==0,"exhaustion clears pending count immediately");need(r.world.player().direction==9,"failed charged visit skips heading tail");}
  r.finish();const unsigned expected=stock==1?1:paired?(stock==2?47:48):24;
  need(r.world.shotsFired==expected,"native Circular low-stock projectile count");need(r.world.player().ammo.at("bullet").count==(stock<=2?0:stock-2),"only initial/final visits charge while active");
  need(r.world.player().direction==(stock==1?9:stock==2?8:9),"only successful emitter visits advance heading");
  for(const auto& shot:r.world.projectiles())need(shot.slopeRadians==r.world.player().weapons[0].config.initialSlope,"pending shots keep entry slope");
 }
 // Visible ammunition grows the actor/object arrays during emission. Stock
 // charging must reacquire the shooter rather than retaining vector references.
 {Run r(true,false,true);r.stock(2);r.tick(true);r.finish();need(r.world.shotsFired==47&&r.world.player().ammo.at("bullet").count==0&&r.world.player().direction==8&&r.world.actors().size()>1,"visual-body construction preserves low-stock charging and tail");}
 // A ready replacement makes the selection check succeed. The old stock pointer
 // is still empty, so the current volley ends before the second old barrel.
 {Run r(true,true);r.stock(1);r.tick(true);need(r.world.shotsFired==1&&r.world.player().selectedWeapon==1&&r.world.player().direction==10&&r.world.surprise().circularRemaining==23,"loaded weapon switch preserves successful tail after partial old volley");r.finish();need(r.world.shotsFired==24&&r.world.player().ammo.at("otherbullet").count==4&&r.world.player().direction==9,"pending visits use the newly selected weapon");}
 {Run r;auto s=r.world.snapshot();s.actors[0].ammo.slots[0].second.count=1;s.actors[0].ammo.append("bullet",{5,5});s.actors[0].weapons[0].ammoSlot=0;r.restore(s);r.tick(true);need(r.world.shotsFired==1&&r.world.player().weapons[0].ammoSlot==1&&r.world.surprise().circularRemaining==23,"compatible duplicate stock refresh succeeds without finishing old barrels");r.finish();need(r.world.shotsFired==47&&r.world.player().ammo.slots[1].second.count==4&&r.world.player().direction==9,"duplicate stock identity survives pending visits");}
 {Run r;r.stock(1);auto s=r.world.snapshot();s.actors[0].weapons[0].state.lastFireMs=uint64_t(s.elapsedMs);r.restore(s);r.tick(true);need(r.world.shotsFired==0&&r.world.surprise().circularRemaining==24,"input cooldown leaves an armed pending sweep");r.finish();need(r.world.shotsFired==47&&r.world.player().direction==8,"pending middle visits bypass cooldown and do not consume the last round");}
 // Expiry preserves pending count, but clears the shared-charge World flag.
 {Run r;r.stock(3);r.tick(true);while(r.world.surprise().circularRemaining>2)r.tick();auto s=r.world.snapshot();s.surprise.remainingMs=0;r.restore(s);r.tick();r.finish();need(r.world.surprise().kind==SurpriseKind::None&&r.world.shotsFired==48&&r.world.player().ammo.at("bullet").count==0&&r.world.player().direction==8,"expired final visit charges both products and aborts on exhaustion");}
 {Run r,resumed;r.stock(3);r.tick(true);for(int i=0;i<6;++i)r.tick();resumed.restore(r.world.snapshot());for(int i=0;i<25;++i){r.tick();resumed.tick();need(r.world.surprise().circularRemaining==resumed.world.surprise().circularRemaining&&r.world.player().direction==resumed.world.player().direction&&r.world.shotsFired==resumed.world.shotsFired&&bytes(r)==bytes(resumed),"memory continuation matches counters, heading and complete encoded state");}
  const auto dir=hostTempDirectory("vurus-circular-ammo-");const auto file=dir/"state.vzs";SaveGame save,read;save.currentLevel=r.scene.mapPath;save.levels[save.currentLevel].world=r.world.snapshot();need(writeSaveFile(file.string(),save,r.error)&&readSaveFile(file.string(),read,r.error),r.error);const auto& disk=read.levels.at(save.currentLevel).world;need(disk.surprise.kind==SurpriseKind::None&&disk.surprise.circularRemaining==0,"disk save retains existing Surprise normalization");resumed.restore(disk);resumed.tick(true);need(resumed.world.player().ammo.at("bullet").count==0,"ordinary post-load paired fire consumes remaining round");std::filesystem::remove_all(dir);
 }
 std::cout<<"PASS Circular single/paired stock edges, immediate abort, replacement weapon/ammo, cooldown, expiry, slope and memory/disk continuation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
