#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_state.h"
#include "game/spider_contact_native.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool v,const std::string& s){if(!v)throw std::runtime_error(s);}
static TextNode val(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
static TextNode block(std::string k,std::vector<TextNode> v,std::string arg={}){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=v;if(!arg.empty())n.arguments={arg};return n;}
struct Run {
 SceneContent scene;Level level;GameWorld world;std::string error;
 Run(){scene.mapPath="TEST/AUDIO.3DG";
 ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};p.properties={val("Sub-type","Vyruz"),val("Bounding-box-size","25"),block("Energy",{val("Initial","1000"),val("Max","1000")}),block("Shield",{val("Initial","100"),val("Max","100")})};
 ObjectClass item;item.id={1};item.name="Money";item.properties={val("Bounding-box-size","12"),block("TYPE",{val("Only-for-player","1"),val("Add-Money","100")},"Item")};
 ObjectClass enemy;enemy.id={2};enemy.name="Enemy";enemy.behaviours={"Fixed","Seeker"};enemy.properties={val("Sub-type","Cop"),val("Bounding-box-size","8"),block("Energy",{val("Initial","10"),val("Max","10")}),block("TYPE",{val("Target","Player"),val("Sensor-Range","0")},"Seeker")};
 ObjectClass ammo;ammo.id={3};ammo.name="Bullet";ammo.properties={block("TYPE",{},"Ammo")};scene.classes={p,item,enemy,ammo};
 for(int i=0;i<9;++i){ObjectClass gun;gun.id={unsigned(4+i)};gun.name="Gun"+std::to_string(i);gun.properties={block("TYPE",{val("Ammo","Bullet"),val("Delay","100")},"Weapon")};scene.classes.push_back(gun);scene.classes[0].properties.push_back(block("ADD",{val("Name",gun.name)},"Weapon"));}
 scene.classes[0].properties.push_back(block("ADD",{val("Name","Bullet"),val("Count","100"),val("Max","200")},"Ammo"));
 place(0,{200,200});place(2,{1000,1000});scene.player=ObjectId{0};level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;
 need(vyruz_test::loadManagedFixture(world,scene,level,error),error);world.setDebugEnemyAI(false);world.takeAudioEvents();}
 void place(unsigned c,ContentPosition p){ObjectPlacement o;o.id={unsigned(scene.objects.size())};o.classId={c};o.position=p;o.properties=scene.classes[c].properties;o.behaviours=scene.classes[c].behaviours;scene.objects.push_back(o);}
 void restore(WorldSnapshot s){need(world.restoreSnapshot(s,error),error);world.setDebugEnemyAI(false);world.takeAudioEvents();}
 std::vector<AudioEvent> voices(){auto all=world.takeAudioEvents();std::erase_if(all,[](const auto&e){return e.kind!=AudioEventKind::Voice;});return all;}
};
static void event(const std::vector<AudioEvent>& es,const std::string& name,unsigned delay,unsigned hold,unsigned tail){const auto it=std::find_if(es.begin(),es.end(),[&](const auto&e){return e.name==name;});need(it!=es.end(),("missing voice "+name).c_str());need(it->delayMs==delay&&it->holdMs==hold&&it->tailMs==tail,"native voice timing tuple");}
int main(){try{
 const char* money[]={"V-LAUGH-1.WAV","V-YEEPEE.WAV","V-YEAH.WAV","V-HEY.WAV","V-YO.WAV","V-GIVE-ME.WAV"};
 for(unsigned residue=0;residue<16;++residue){Run r;r.world.spawn(ClassId{1},r.world.playerPosition());auto s=r.world.snapshot();s.elapsedMs=1008+residue-62.5;r.restore(s);r.world.tick(.0625f,{});need(r.world.player().vitals.money==100,"money actually collected");auto v=r.voices();if(residue<6){need(v.size()==1,"money emits one selected commentary");event(v,money[residue],0,1000,0);}else need(v.empty(),"native money silent residues");}
 const char* weapons[]={"V-GUN.WAV","V-GRENADE.WAV","V-ROCKETS.WAV","V-FLAME.WAV","V-SAW.WAV","V-HOMING.WAV","V-LAZER.WAV","V-PULSAR.WAV","V-MINE.WAV"};
 {Run r;for(unsigned i=1;i<=9;++i){WorldInput in;in.weaponDelta=1;r.world.tick(.0625f,in);event(r.voices(),weapons[i%9],750,500,0);}}
 for(float shield:{1.01f,1.f,0.f}){Run r;auto s=r.world.snapshot();s.actors[0].vitals.shield=shield;r.restore(s);r.world.tick(.0625f,{});auto v=r.voices();if(shield<=1)event(v,"V-SHIELD-LOW.WAV",0,1000,15000);else need(v.empty(),"one percent threshold, not ten percent");}
 const char* locked[]={"V-WARNING.WAV","V-WATCH-OUT.WAV","V-TAKE-CARE.WAV","V-LOCKED.WAV"};
 for(unsigned residue=0;residue<4;++residue){Run r;
  const auto authored=[](SharedProperties& props){for(auto& n:props)if(n.key=="TYPE"&&!n.arguments.empty()&&n.arguments[0]=="Seeker"){for(auto& p:n.children)if(p.key=="Sensor-Range")p.values={"5000"};n.children.push_back(val("Scan-Delay","400"));}};
  authored(r.scene.classes[2].properties);authored(r.scene.objects[1].properties);need(vyruz_test::loadManagedFixture(r.world,r.scene,r.level,r.error),r.error);r.world.setDebugEnemyAI(true);
  // Native begin and completed-wave visits precede the first steering/lock.
  r.world.tick(.014f,{});need(r.voices().empty(),"begin-search visit cannot emit locked warning");r.world.tick(.014f,{});need(r.voices().empty(),"route-selection visit cannot emit locked warning");
  auto s=r.world.snapshot();s.elapsedMs=1008+residue-62.5;r.restore(s);r.world.setDebugEnemyAI(true);r.world.tick(.0625f,{});event(r.voices(),locked[residue],0,1000,10000);
 }
 {Run r;ObjectClass gate;gate.id={unsigned(r.scene.classes.size())};gate.name="Gate";gate.sprite.sequence={1};gate.properties={val("Bounding-box-size","32"),block("TYPE",{val("Active","1"),val("Sensor","1"),val("Id","9"),val("Delay","100")},"Gate")};r.scene.classes.push_back(gate);r.scene.objects.resize(1);r.place(gate.id.value,{300,200});r.scene.objects.back().sprite.sequence={1};need(vyruz_test::loadManagedFixture(r.world,r.scene,r.level,r.error),r.error);r.world.finishGateAnimation(1,1);r.world.updateGates(20);r.world.takeAudioEvents();auto key=r.world.snapshot();r.world.movePlayer(100,0);event(r.voices(),"V-NEED-KEY.WAV",0,1000,5000);key.actors[1].gateState.flags|=GateNeedsCode;r.restore(key);r.world.movePlayer(100,0);event(r.voices(),"V-NEED-CODE.WAV",0,1000,5000);}
 // Actual projectile fuse -> radial damage -> death, not a fabricated UI event.
 const char* kills[]={"V-YEAH.WAV","V-HEY.WAV","V-YO.WAV","V-GIVE-ME.WAV","V-THE-BEST.WAV","V-WANT-MORE.WAV","V-LAUGH-1.WAV"};
 for(unsigned residue=0;residue<16;++residue){Run r;auto s=r.world.snapshot();s.elapsedMs=1008+residue-62.5;Projectile p;p.classId={3};p.owner=0;p.position={1000,1000};p.damage=100;p.damageRadius=20;p.fuseMs=1;p.damageRemainingMs=1;p.phaseInitialized=true;p.ignoreOwner=true;nativeAmmoOwnerLaunched(s.actors[0].nativeAmmoOwner);
 vyruz_test::appendProjectile(s,r.scene,p);r.restore(s);r.world.tick(.0625f,{});need(r.world.actors()[1].alive&&r.world.actors()[1].nativeDamagePending&&r.world.actors()[1].vitals.energy<=0,"projectile records lethal health before later actor death");auto v=r.voices();if(residue<7)event(v,kills[residue],1000,1500,0);else need(v.empty(),"native kill silent residues");r.world.tick(.0625f,{});need(!r.world.actors()[1].alive&&!r.world.actors()[1].nativeDamagePending,"next actor visit consumes projectile death");need(r.voices().empty(),"deferred actor death does not repeat immediate Ammo commentary");r.world.damage(1,100,0,true);need(r.voices().empty(),"dead target cannot repeat commentary");}
 // Script/FX radial damage can qualify for FastKill but cannot impersonate Ammo kill speech.
 {Run r;r.world.damage(1,100,0,true);need(r.voices().empty(),"non-Ammo radial damage has no ordinary kill voice");}
 {Run r;auto s=r.world.snapshot();s.actors[1].hudEnemy=false;s.actors[1].enemy=false;s.actors[1].ordinaryAI.reset();r.restore(s);r.world.damage(1,100,0,true,true);event(r.voices(),"V-YEAH.WAV",1000,1500,0);}
 {Run r;r.world.damage(1,100,1,true,true);need(r.voices().empty(),"foreign Ammo owner has no player kill voice");}
 std::cout<<"PASS money residue selection, weapon slots/timing, shield threshold, locked warnings, gate access, actual projectile kill commentary\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
