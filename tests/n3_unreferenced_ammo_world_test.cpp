// Original-resource Saw-Ammo-Empty acceptance. Motion oracle is retained output
// from unchanged native438b20, not the port's propulsion helper.
#include "game/world.h"
#include "game/properties.h"
#include "game/save_file.h"
#include "game/menu.h"
#include "io/vfs.h"
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const std::string&s){if(!b)throw std::runtime_error(s);}
static TextNode value(std::string k,std::string v){TextNode n;n.key=std::move(k);n.kind=TextKind::Assignment;n.values={std::move(v)};return n;}
static TextNode block(std::string k,std::vector<TextNode> c,std::string type={}){TextNode n;n.key=std::move(k);n.kind=TextKind::Block;n.children=std::move(c);if(!type.empty())n.arguments={std::move(type)};return n;}
struct NativeRecord {uint32_t ammo,heading,moving,hz,step;float vx,vz,magnitude,thrust,dx,dz;};
static std::vector<uint8_t> disk(const GameWorld&w){SaveGame s;s.currentLevel="TEST/SAW-EMPTY.3DG";s.levels[s.currentLevel].world=w.snapshot();std::string e;std::vector<uint8_t>b;need(encodeSave(s,b,e),e);return b;}
static SceneContent fixture(const SceneContent& original){
 auto scene=original;const auto* empty=findClass(scene,"Saw-Ammo-Empty");need(empty,"original Saw-Ammo-Empty catalog class");
 ObjectClass gun;gun.id={uint32_t(scene.classes.size())};gun.name="Audit-Empty-Launcher";gun.properties={block("TYPE",{value("Ammo","Saw-Ammo-Empty"),value("Start-distance","250"),value("Delay","10000")},"Weapon")};scene.classes.push_back(gun);
 auto player=original.objects[original.player->value];player.id={0};player.owner.reset();player.position={6000,3000};player.properties={value("Sub-type","Vyruz"),block("Energy",{value("Initial","10000"),value("Max","10000")}),block("ADD",{value("Name",gun.name)},"Weapon"),block("ADD",{value("Name","Saw-Ammo-Empty"),value("Count","100"),value("Max","100")},"Ammo")};scene.objects={player};scene.player=ObjectId{0};return scene;
}
static void load(GameWorld&w,SceneContent&s,const Level&l,int heading){std::string e;need(w.load(s,l,e),e);w.setDebugEnemyAI(false);w.setDebugGodMode(true);w.setPlayerDirection(heading);size_t slot=0;while(slot<w.player().weapons.size()&&w.player().weapons[slot].name!="Audit-Empty-Launcher")++slot;need(w.selectPlayerWeaponSlot(slot),"select isolated fixture launcher");}
int main(int argc,char**argv){try{
 need(argc==3,"usage: n3_unreferenced_ammo_world_test DATA ORIGINAL_438B20_RESULT_BIN");
 std::ifstream records(argv[2],std::ios::binary);need(bool(records),"open retained original native propulsion records");NativeRecord r;std::map<std::pair<int,int>,std::vector<NativeRecord>> oracle;unsigned raw=0;while(records.read(reinterpret_cast<char*>(&r),sizeof(r))){++raw;if(r.ammo==4&&!r.moving)oracle[{r.heading,r.hz}].push_back(r);}need(raw==9600&&oracle.size()==4,"unchanged original native record inventory");for(const auto&[key,rows]:oracle)need(rows.size()==240,"240 original observed updates per motion fixture");
 Vfs v;need(v.init(argv[1]),v.error());ContentReader read=[&](const auto&p,auto&b){return v.read(p,b);};unsigned maps=0,motionCases=0,motionRows=0,expiries=0,saves=0,walls=0,actors=0;float maxError=0;
 Level arena;arena.mapW=arena.mapH=200;arena.hasTerrainCollision=true;
 for(const auto& path:debugLevels()){
  SceneContent original;std::string e;need(loadSceneContent(read,path,original,e),e);const auto* cls=findClass(original,"Saw-Ammo-Empty");need(cls,path+" catalog contains unreferenced Ammo");const auto* ammo=attribute(cls->properties,"ammo");need(ammo&&!propertyBool(ammo->children,"On-hit")&&propertyBool(ammo->children,"On-object-hit"),path+" original independent contact flags");need(propertyNumber(ammo->children,"Damage-after")==5000,path+" original5000ms fuse");need(propertyNumber(cls->properties,"Life-time")<=0,path+" fuse separate from class lifetime");
  for(const auto&[key,rows]:oracle){const auto [heading,hz]=key;auto s=fixture(original);const auto initial=s;GameWorld w;load(w,s,arena,heading);WorldInput input;input.fire=true;const float dt=1.f/hz;float remaining=5000;
   for(unsigned step=0;step<rows.size();++step){w.tick(dt,input);input.fire=false;remaining=std::max(0.f,remaining-dt*1000);need(w.projectiles().size()==1,path+" unreferenced Ammo actual launch/clear flight");const auto& p=w.projectiles()[0];need(!p.visual&&p.nativeBareBody&&s.classes[p.classId.value].name=="Saw-Ammo-Empty","original unreferenced class uses compact canonical body");need(p.terrainBounce&&!p.guidanceClass&&p.fuseMs==5000,"original Saw Straight/contact/fuse inputs retained");const auto& n=rows[step];const float error=std::max({std::abs(p.motion.velocityX-n.vx),std::abs(p.motion.velocityZ-n.vz),std::abs(p.motion.magnitude-n.magnitude),std::abs(p.motion.thrust-n.thrust)});maxError=std::max(maxError,error);need(error<.0001f,path+" Saw-Empty motion differs from unchanged native438b20 step="+std::to_string(step));need(std::abs(p.damageRemainingMs-remaining)<.02f,"5000ms fuse advances independently of movement");++motionRows;
   }
   if(heading==9&&hz==60){SaveGame saved;need(decodeSave(disk(w),saved,e),e);auto copy=initial;GameWorld resumed;load(resumed,copy,arena,heading);need(resumed.restoreSnapshot(saved.levels.begin()->second.world,e),e);resumed.setDebugGodMode(true);resumed.setDebugEnemyAI(false);need(disk(w)==disk(resumed),"unreferenced Ammo full disk checkpoint exact");for(unsigned tick=0;tick<20;++tick){w.tick(dt,{});resumed.tick(dt,{});remaining=std::max(0.f,remaining-dt*1000);}need(disk(w)==disk(resumed),"unreferenced Ammo disk continuation exact");++saves;}
   for(unsigned step=0;step<500;++step){remaining=std::max(0.f,remaining-dt*1000);w.tick(dt,{});need(!w.projectiles().empty()==(remaining>0),path+" original5000ms fuse exact own-visit disposal");if(w.projectiles().empty()){need(w.snapshot().bareAmmoCells.links.empty(),"fuse physically unlinks compact body");++expiries;break;}}
   ++motionCases;
  }
  // Same original Ammo and actual emitter, with a controlled wall ahead of
  //the spawn. Native441b41 chooses physical wall response for On-hit0/Object1.
  {auto s=fixture(original);auto wall=arena;wall.collisionEdges={{6500,0,6500,6400}};GameWorld w;load(w,s,wall,9);WorldInput input;input.fire=true;bool reflected=false;for(unsigned tick=0;tick<100;++tick){w.tick(1.f/60,input);input.fire=false;need(w.projectiles().size()==1,path+" On-hit0/Object1 Saw survives wall");const auto& p=w.projectiles()[0];if(p.motion.velocityX<0&&p.motion.velocityZ>0){reflected=true;break;}}need(reflected,"Saw Empty reflects against native wall contact policy");++walls;}
  // A kind3 actor is a terminal hit despite On-hit0: those flags choose
  //non-actor physical response, not permanent immunity to actor contacts.
  {auto s=fixture(original);ObjectClass target;target.id={uint32_t(s.classes.size())};target.name="Audit-Target";target.behaviours={"Fixed"};target.properties={value("Sub-type","Thing"),value("Bounding-box-size","20"),block("Energy",{value("Initial","10000")})};s.classes.push_back(target);ObjectPlacement p;p.id={1};p.classId=target.id;p.position={6500,3000};p.properties=target.properties;p.behaviours=target.behaviours;s.objects.push_back(p);GameWorld w;load(w,s,arena,9);WorldInput input;input.fire=true;bool hit=false;for(unsigned tick=0;tick<100;++tick){w.tick(1.f/60,input);input.fire=false;if(tick==0)need(w.projectiles().size()==1,"actor-contact control observes real launch before terminal hit");if(w.projectiles().empty()){need(tick>0,"terminal contact follows observed launch");hit=true;break;}}need(hit,"Saw Empty terminal animated actor contact before5000ms fuse");need(w.actors()[1].vitals.energy==10000,"original zero-damage SawEmpty contact preserves target health");++actors;}
  ++maps;
 }
 need(maps==25&&motionCases==100&&motionRows==24000&&expiries==100&&saves==25&&walls==25&&actors==25,"25-map complete unreferenced Ammo acceptance census");std::cout<<"Saw-Ammo-Empty maps="<<maps<<" native motion cases="<<motionCases<<" rows="<<motionRows<<" maxerror="<<maxError<<" fuse="<<expiries<<" saves="<<saves<<" walls="<<walls<<" actorcontacts="<<actors<<" passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
