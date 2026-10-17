#include "game/world.h"
#include "snapshot_fixture.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/ammo_collision_native.h"
#include "game/properties.h"
#include "game/menu.h"
#include "io/vfs.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "host_temp.h"
using namespace vyruz;
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string val){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={val};return n;}
static ContentPosition surface(ContactVelocity p){auto s=spiderNativeToSurface(p.x,p.z);return {s.x,s.y};}
static ObjectPlacement place(const ObjectClass& c,ContentPosition p){ObjectPlacement o;o.classId=c.id;o.id={0};o.position=p;o.properties=c.properties;o.flags=c.flags;o.sprite=c.sprite;o.behaviours=c.behaviours;return o;}
static ObjectClass targetClass(uint32_t id,const char* family,float radius,bool pickup=false){ObjectClass c;c.id={id};c.name=std::string("N2-Control-")+family;c.behaviours={"Fixed"};c.properties={value("Sub-type",family),value("Bounding-box-size",std::to_string(radius))};
 TextNode energy;energy.key="Energy";energy.kind=TextKind::Block;energy.children={value("Initial","100000"),value("Max","100000")};c.properties.push_back(energy);
 if(std::string(family)=="Gate")c.sprite.sequence={1};
 if(std::string(family)=="Item"||std::string(family)=="Gate"){TextNode t;t.kind=TextKind::Block;t.key="TYPE";t.arguments={family};t.children={value("Only-for-player",pickup?"1":"0")};c.properties.push_back(t);}return c;}
// Asset-free regression metadata copies the resolved original core parameters;
// it does not try to manufacture original art or full resource/terrain coverage.
static TextNode block(std::string key,std::vector<TextNode> fields,std::string kind={}){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=fields;if(!kind.empty())n.arguments={kind};return n;}
static SceneContent coreScene(){SceneContent s;s.mapPath="CAVE/T1C1.3DG";
 ObjectClass player;player.id={0};player.name="Vyruz";player.behaviours={"Player"};
 player.properties={value("Sub-type","Vyruz"),value("Bounding-box-size","22"),block("Energy",{value("Initial","100000"),value("Max","100000")}),
 block("ADD",{value("Name","Grenade-Launcher")},"Weapon"),block("ADD",{value("Name","Grenade-Ammo"),value("Count","1000"),value("Max","1000")},"Ammo")};
 ObjectClass gun;gun.id={1};gun.name="Grenade-Launcher";gun.properties={block("TYPE",{value("Ammo","Grenade-Ammo"),value("Delay","500"),value("Fire-Range","300")},"Weapon")};
 ObjectClass ammo;ammo.id={2};ammo.name="Grenade-Ammo";ammo.behaviours={"Straight"};
 ammo.properties={value("Sub-type","Ammo"),value("Bounding-box-size","8"),value("Altitude","24"),value("Speed-max","90"),value("Acceleration-step","2"),value("Decceleration-step","1"),
 value("Use-inertia","1"),value("Launched","1"),value("Grip","1"),value("Friction-factor","0.97"),value("Friction-threshold","0.1"),value("Elasticity","0.6"),value("Vertical-Bounce","1"),value("Horizontal-Bounce","0"),
 block("Thrust",{value("Initial","90"),value("Min","0"),value("Max","90")}),block("TYPE",{value("damage","0"),value("damage-zone","0"),value("active-after","0"),value("damage-after","3000"),value("on-hit","0"),value("on-object-hit","1")},"Ammo"),
 block("TYPE",{},"Explosion")};
 player.properties.push_back(value("Managed","1"));ammo.properties.push_back(value("Managed","1"));
 s.classes={player,gun,ammo};auto o=place(player,{4000,3000});o.id={0};s.objects={o};s.player=ObjectId{0};return s;
}
struct Fixture {
 SceneContent scene,blueprint;Level level;GameWorld world;std::string error;size_t visual=0;
 Fixture(const SceneContent& original,std::vector<NativeNavigationDecor> decor={},int targets=0,bool pickup=false):scene(original){
  auto player=scene.objects[scene.player->value];player.id={0};player.position={4000,3000};scene.objects={player};scene.player=ObjectId{0};
  for(int i=0;i<targets;++i){auto c=targetClass(scene.classes.size(),i==0?"Cop":i==1?"Item":"Gate",i==0?8:i==1?12:16,pickup);scene.classes.push_back(c);auto o=place(c,{1000.f+i*100,1000});o.id={uint32_t(scene.objects.size())};scene.objects.push_back(o);}
  level.mapW=level.mapH=250;level.hasTerrainCollision=true;level.nativeNavigationDecor=std::move(decor);
  blueprint=scene;need(world.load(scene,level,error),error);auto state=world.snapshot();auto& p=state.actors[0];size_t slot=SIZE_MAX;
  for(size_t i=0;i<p.weapons.size();++i)if(contentNameEqual(p.weapons[i].ammo,"Grenade-Ammo"))slot=i;
  need(slot!=SIZE_MAX,"Grenade slot");p.selectedWeapon=slot;for(auto& stock:p.ammo.slots)stock.second.count=stock.second.maximum=1000;
  need(world.restoreSnapshot(state,error),error);world.setDebugEnemyAI(false);world.setDebugGodMode(true);world.tick(1.f/60,{{},true});
  need(world.projectiles().size()==1&&world.projectiles()[0].visual,"native original-resource Grenade launch");visual=*world.projectiles()[0].visual;
 }
 void reset(ContactVelocity center,ContactVelocity delta,float age=600,bool history=true){auto s=world.snapshot();auto& shot=s.projectiles[0];shot.position=surface(center);shot.ageMs=age;shot.damageRemainingMs=2500;shot.phaseInitialized=true;shot.inactiveMs=shot.onMs=0;
  const float factor=14.f/(1000.f/60);shot.motion.velocityX=delta.x*factor/.97f;shot.motion.velocityZ=delta.z*factor/.97f;shot.motion.magnitude=0;shot.motion.thrust=90;shot.directionX=delta.x?delta.x/std::hypot(delta.x,delta.z):0;shot.directionZ=delta.z?delta.z/std::hypot(delta.x,delta.z):0;if(delta.x==0&&delta.z==0)shot.directionX=1;
  s.objects[visual].position=shot.position;s.actors[visual].nativeMotion=SpiderMotionHistory{{center.x,center.z},surface({center.x-10,center.z})};s.actors[visual].direction=delta.x<0?18:delta.z?0:6;s.actors[visual].enemyMoveCommand=200;
  if(!history)s.actors[visual].nativeMotion.reset();
  // Original-terrain controls replace the synthetic arena dimensions after
  // launch. Rebuild that fixture's cells explicitly while retaining UID,
  // pool and awake history; this is not a legacy checkpoint conversion.
  if(s.actorCells->width!=level.mapW || s.actorCells->height!=level.mapH){
   need(s.bareAmmoCells.links.empty(),"dimension replacement fixture has only visual Ammo");
   s.actorCells->reset(level.mapW,level.mapH);s.bareAmmoCells.reset(level.mapW,level.mapH);
   s.actorCells->cell.assign(s.actors.size(),NativeActorCells::Missing);
   s.actorCells->next.assign(s.actors.size(),NativeActorCells::Missing);
   s.actorCells->arrivalOrder.assign(s.actors.size(),0);
   for(size_t id=0;id<s.actors.size();++id)vyruz_test::synchronizeActor(s,id);
  }
  vyruz_test::synchronizeActor(s,visual);s.spiderNavigation.reset();need(world.restoreSnapshot(s,error),error);world.setDebugEnemyAI(false);world.setDebugGodMode(true);
 }
 void setTarget(size_t id,ContactVelocity center){auto s=world.snapshot();s.objects[id].position=surface(center);s.actors[id].nativeMotion=SpiderMotionHistory{{center.x,center.z},s.objects[id].position};vyruz_test::synchronizeActor(s,id);s.spiderNavigation.reset();need(world.restoreSnapshot(s,error),error);world.setDebugEnemyAI(false);world.setDebugGodMode(true);}
 void tick(){world.tick(1.f/60,{});}
};
int main(int argc,char**argv){try{need(argc==1||argc==3,"usage: grenade_native_contacts_audit [DATA OUTPUT]");const std::filesystem::path out=argc==3?std::filesystem::path(argv[2]):std::filesystem::temp_directory_path()/("vyruz-n2-native-"+std::to_string(getpid()));std::filesystem::create_directories(out);
 Vfs v;if(argc==3)need(v.init(argv[1]),v.error());ContentReader read=[&](const auto&p,auto&b){return v.read(p,b);};std::string error;SceneContent original=coreScene();if(argc==3)need(loadSceneContent(read,"CAVE/T1C1.3DG",original,error),error);
 const NativeNavigationDecor line{4004,2900,4004,3100,0,0,1,false,false,false};
 Fixture approach(original,{line});approach.reset({4000,3000},{2,0});approach.tick();need(approach.world.projectiles().size()==1,"native line retains Grenade");
 auto a=approach.world.snapshot();auto shot=a.projectiles[0];auto h=*a.actors[approach.visual].nativeMotion;
 need(std::abs(shot.motion.velocityX+1.2f)<.001f&&std::abs(shot.motion.velocityZ)<.001f,"native terrain receives caller displacement and elasticity");
 need(h.center.x==4000&&h.center.y==3000&&shot.position.x==surface({4000,3000}).x,"full native rollback");need(shot.motion.magnitude>1.67f&&shot.motion.magnitude<1.69f,"bounce retains pre-response arc magnitude");
 Fixture separated(original,{line});separated.reset({4000,3000},{-2,0});separated.tick();a=separated.world.snapshot();shot=a.projectiles[0];h=*a.actors[separated.visual].nativeMotion;
 need(std::abs(h.center.x-3998)<.001f&&shot.position.x==surface({4000,3000}).x,"return2 commits native center while surface stays rolled back");
 need(shot.motion.velocityX<-1.99f&&shot.motion.velocityX>-2.01f,"return2 retains provisional displacement");
 SaveGame save;save.currentLevel="CAVE/T1C1.3DG";save.levels[save.currentLevel].world=a;need(writeSaveFile((out/"separated.vzs").string(),save,error),error);SaveGame decoded;need(readSaveFile((out/"separated.vzs").string(),decoded,error),error);
 auto copiedScene=separated.blueprint;GameWorld loaded;need(loaded.load(copiedScene,separated.level,error),error);need(loaded.restoreSnapshot(decoded.levels.at(save.currentLevel).world,error),error);loaded.setDebugEnemyAI(false);loaded.setDebugGodMode(true);
 for(unsigned i=0;i<60;++i){separated.tick();loaded.tick(1.f/60,{});need(separated.world.projectiles().size()==loaded.projectiles().size(),"disk projectile count continuation");if(loaded.projectiles().empty())break;
  const auto& x=separated.world.projectiles()[0];const auto& y=loaded.projectiles()[0];const auto& hx=*separated.world.actors()[separated.visual].nativeMotion;const auto& hy=*loaded.actors()[separated.visual].nativeMotion;
  need(x.position.x==y.position.x&&x.position.y==y.position.y&&x.motion.velocityX==y.motion.velocityX&&x.motion.velocityZ==y.motion.velocityZ&&x.ageMs==y.ageMs&&x.damageRemainingMs==y.damageRemainingMs&&hx.center==hy.center&&hx.previousSurface.x==hy.previousSurface.x&&hx.previousSurface.y==hy.previousSurface.y,"native center/surface/history/fuse disk continuation");
 }
 Fixture priority(original,{},2);priority.reset({4000,3000},{20,0});priority.setTarget(1,{4023,3000});priority.setTarget(2,{4027,3000});priority.tick();
 need(priority.world.projectiles().empty(),"native object contact detonates");const auto expected=surface({3999,3000});need(std::abs(priority.scene.objects[priority.visual].position.x-expected.x)<.001f&&std::abs(priority.scene.objects[priority.visual].position.y-expected.y)<.001f,"nonItem takes precedence over nearer Item impact coordinate");
 Fixture gate(original,{},3);gate.reset({4000,3000},{20,0});gate.setTarget(3,{4030,3000});gate.tick();need(gate.world.projectiles().empty(),"square Gate native narrowphase detonates Grenade");
 Fixture overlap(original,{},1);overlap.reset({4000,3000},{0,0});overlap.setTarget(1,{4000,3000});overlap.tick();need(overlap.world.projectiles().size()==1,"stationary grenade does not invent moving-contact traversal");
 Fixture grace(original);grace.reset({4000,3000},{2,0},480);grace.world.setPlayerPosition(surface({4028,3000}).x,surface({4028,3000}).y);grace.tick();need(grace.world.projectiles().size()==1,"owner grace through499ms");
 auto owner=grace.world.snapshot();owner.projectiles[0].ageMs=500;owner.projectiles[0].position=surface({4000,3000});owner.objects[grace.visual].position=owner.projectiles[0].position;owner.actors[grace.visual].nativeMotion=SpiderMotionHistory{{4000,3000},surface({3990,3000})};owner.projectiles[0].motion.velocityX=2*(14.f/(1000.f/60))/.97f;owner.projectiles[0].motion.velocityZ=0;vyruz_test::synchronizeActor(owner,grace.visual);owner.spiderNavigation.reset();need(grace.world.restoreSnapshot(owner,error),error);grace.tick();need(grace.world.projectiles().empty(),"owner grace ends at500ms");
 Fixture legacy(original);legacy.reset({4000,3000},{2,0},600,false);legacy.tick();need(legacy.world.actors()[legacy.visual].nativeMotion.has_value()&&legacy.world.actors()[legacy.visual].enemyMoveCommand==200,"legacy missing history is reconstructed without save layout changes");
 std::ofstream maps(out/"maps.tsv");maps<<"map\tdecor_count\tfixture_decor\tshape\tnative_center\n";unsigned mapCount=0;
 if(argc==3)for(const auto& path:debugLevels()){
  SceneContent content;need(loadSceneContent(read,path,content,error),error);std::vector<uint8_t> dg,di,dx;need(v.read(path,dg),"map");Level actual;need(loadLevel(dg,actual),"map parse");const auto prefix=path.substr(0,path.rfind('.'));need(v.read(prefix+".3DI",di)&&v.read(prefix+".3DX",dx)&&loadTerrain(di,dx,actual),"native terrain parse");
  size_t chosen=SIZE_MAX;ContactVelocity start{},step{};
  for(size_t i=0;i<actual.nativeNavigationDecor.size();++i){const auto& d=actual.nativeNavigationDecor[i];if(d.disabled||d.water)continue;ContactVelocity p;ContactVelocity move;
   if(d.rectangle||d.minX==d.maxX){p={d.minX-12,(d.minY+d.maxY)*.5f};move={8,0};}else{p={(d.minX+d.maxX)*.5f,d.minY-12};move={0,8};}
   const auto q=surface(p);if(q.x>64&&q.y>64&&q.x<actual.mapW*64-64&&q.y<actual.mapH*32-64){chosen=i;start=p;step=move;break;}}
  need(chosen!=SIZE_MAX,"safe original native terrain primitive: "+path);Fixture fixture(content);fixture.level=actual;fixture.reset(start,step,0);fixture.tick();
  need(fixture.world.projectiles().size()==1,"original-terrain grenade remains live: "+path);const auto& history=fixture.world.actors()[fixture.visual].nativeMotion;need(history.has_value(),"original map native bridge: "+path);
  need(history->center.x==start.x&&history->center.y==start.z,"original-terrain contact rolls back native center: "+path);
  const auto& rebound=fixture.world.projectiles()[0];need(rebound.motion.velocityX*step.x+rebound.motion.velocityZ*step.z<0,"original-terrain fixture actually rebounds: "+path);
  maps<<path<<'\t'<<actual.nativeNavigationDecor.size()<<'\t'<<chosen<<'\t'<<(actual.nativeNavigationDecor[chosen].rectangle?"rectangle":"segment")<<'\t'<<history->center.x<<','<<history->center.y<<'\n';++mapCount;
 }
 std::ofstream(out/"result.txt")<<"PASS native grenade bridge: original-resource launch, graph initialization, line bounce caller displacement, independent native/surface return2, v30 disk continuation, Item priority, square Gate, stationary overlap, owner499/500, legacy reconstruction and "<<mapCount<<" original-map terrain contact fixtures. Original whole-game graph producer/closest-noncontact insertion hint and actor-moving-into-Ammo remain outside scope.\n";
 std::cout<<"PASS native Grenade integration and "<<mapCount<<" original terrain maps\n";if(argc==1)std::filesystem::remove_all(out);return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
