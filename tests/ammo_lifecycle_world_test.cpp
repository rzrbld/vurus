#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/properties.h"
#include "game/menu.h"
#include "io/vfs.h"
#include "snapshot_fixture.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace vyruz {struct AmmoImpactNativeTestAccess {
 static void fire(GameWorld& w){w.fire(w.player_,true);}
 static void impact(GameWorld& w,Projectile& p,std::optional<size_t> target={}){
    const auto projected=spiderSurfaceToNative(p.position.x,p.position.y);
    const NativeNavPoint n=p.nativeBareBody?p.nativeBareBody->motion.center:NativeNavPoint{projected.x,projected.y};
    w.applyAmmoImpact(p,target,target.has_value(),{n.x,n.y},16);
 }
 static bool owned(GameWorld& w,Projectile& p){return w.nativeAmmoPlayerOwned(p);}
 static std::uint32_t allocate(GameWorld& w){return w.allocateNativePoolBody().generation;}
 static void release(GameWorld& w,Projectile& p){w.releaseNativeProjectile(p);}
 static void retire(GameWorld& w){w.projectiles_[0].alive=false;}
};}
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode value(std::string k,std::string v){TextNode n;n.key=std::move(k);n.kind=TextKind::Assignment;n.values={std::move(v)};return n;}
static TextNode block(std::string k,std::vector<TextNode> c,std::string type={}){TextNode n;n.key=std::move(k);n.kind=TextKind::Block;n.children=std::move(c);if(!type.empty())n.arguments={std::move(type)};return n;}
static void place(SceneContent& s,ClassId c,ContentPosition p){ObjectPlacement o;o.id={uint32_t(s.objects.size())};o.classId=c;o.position=p;o.properties=s.classes[c.value].properties;o.behaviours=s.classes[c.value].behaviours;o.flags=s.classes[c.value].flags;o.sprite=s.classes[c.value].sprite;s.objects.push_back(std::move(o));}
static SceneContent fixture(){
 SceneContent s;s.mapPath="TEST/AMMO-LIFECYCLE.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Sub-type","Vyruz"),value("Bounding-box-size","20"),block("Energy",{value("Initial","10000"),value("Max","10000")})};
 ObjectClass gun;gun.id={1};gun.name="Audit-Gun";gun.properties={block("TYPE",{value("Ammo","Bare-Ammo"),value("Start-distance","250"),value("Delay","1000")},"Weapon")};
 ObjectClass ammo;ammo.id={2};ammo.name="Bare-Ammo";ammo.flags.invisible=true;ammo.behaviours={"Straight"};ammo.properties={value("Sub-type","Ammo"),value("Bounding-box-size","1"),value("Keep-bb-when-invisible","1"),value("Speed-max","0"),value("Life-time","0"),block("Thrust",{value("Initial","0")}),block("Energy",{value("Initial","500"),value("Max","500")}),block("Shield",{value("Initial","5000"),value("Max","5000"),value("Min","0")}),block("TYPE",{value("Damage","100"),value("Damage-zone","1"),value("Ignore-owner","1"),value("On-hit","1")},"Ammo")};
 ObjectClass target;target.id={3};target.name="Owner-or-target";target.behaviours={"Fixed"};target.properties={value("Sub-type","Cop"),value("Bounding-box-size","1"),block("Energy",{value("Initial","10000"),value("Max","10000")})};
 player.properties.push_back(value("Managed","1"));ammo.properties.push_back(value("Managed","1"));target.properties.push_back(value("Managed","1"));
 s.classes={player,gun,ammo,target};place(s,{0},{1000,1000});place(s,{3},{4000,2000});place(s,{3},{5000,2500});s.player=ObjectId{0};
 s.objects[0].properties.push_back(block("ADD",{value("Name","Audit-Gun")},"Weapon"));
 s.objects[0].properties.push_back(block("ADD",{value("Name","Bare-Ammo"),value("Count","100"),value("Max","100")},"Ammo"));
 return s;
}
static Level arena(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;return l;}
static void load(GameWorld& w,SceneContent& s,const Level& l){std::string e;need(w.load(s,l,e),e);w.setDebugEnemyAI(false);w.setDebugGodMode(true);w.setPlayerDirection(9);}
static std::vector<uint8_t> bytes(const GameWorld& w){SaveGame s;s.currentLevel="TEST/AMMO-LIFECYCLE.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string e;need(encodeSave(s,b,e),e);return b;}
static WorldSnapshot disk(const GameWorld& w){SaveGame s;std::string e;need(decodeSave(bytes(w),s,e),e);return s.levels.begin()->second.world;}
static void checkAmmoGraph(const WorldSnapshot& s,const std::string& label){
 constexpr uint64_t projectileBit=uint64_t(1)<<62,disabledBit=uint64_t(1)<<61,identityMask=disabledBit-1;
 const bool graphValid=!s.spiderNavigation||validNativeNavigationGraph(*s.spiderNavigation);
 const bool checkpointValid=validSpiderNavigationSnapshot(s)&&validMissileNativeNavigationSnapshot(s);
 for(const auto& shot:s.projectiles){
  size_t count=0,matching=SIZE_MAX;
  if(s.spiderNavigation)for(size_t n=0;n<s.spiderNavigation->objects.size();++n){const auto& o=s.spiderNavigation->objects[n];
   if(o.alive&&!o.point&&(o.reference&projectileBit)&&(o.reference&identityMask)==shot.nativeBodyIdentity){++count;matching=n;}
  }
  const bool enrolled=s.spiderNavigation&&shot.alive&&!shot.visual&&shot.radius>0;
  need(count==(enrolled?1u:0u),label+": Ammo identity="+std::to_string(shot.nativeBodyIdentity)+" live bounds="+std::to_string(count)+" graphValid="+std::to_string(graphValid)+" checkpointValid="+std::to_string(checkpointValid));
  need(shot.spiderGraphNode==(enrolled?matching:SIZE_MAX),label+": stored Ammo handle matches its sole body");
  if(enrolled){const auto& o=s.spiderNavigation->objects[matching];const auto p=shot.nativeBareBody->motion.center;const float r=shot.radius<2?0:shot.radius;
   need(o.bounds.minX==p.x-r&&o.bounds.maxX==p.x+r&&o.bounds.minY==p.y-r&&o.bounds.maxY==p.y+r,label+": native compact-body bounds retained");
   need(bool(o.reference&disabledBit)==shot.phaseHidden,label+": hidden Ammo keeps disabled physical bounds");
  }
 }
 need(graphValid&&checkpointValid,label+": real graph, cell/lifecycle and route checkpoint validators");
}
static void checkSameGraph(const NativeNavGraph& a,const NativeNavGraph& b){
 need(a.objects.size()==b.objects.size()&&a.xEdges.size()==b.xEdges.size()&&a.yEdges.size()==b.yEdges.size(),"complete graph keeps every saved slot and edge");
 for(size_t i=0;i<a.objects.size();++i){const auto& x=a.objects[i];const auto& y=b.objects[i];
  need(x.reference==y.reference&&x.alive==y.alive&&x.point==y.point&&x.bounds.minX==y.bounds.minX&&x.bounds.minY==y.bounds.minY&&x.bounds.maxX==y.bounds.maxX&&x.bounds.maxY==y.bounds.maxY,"complete graph preserves references, route points and bounds");
 }
 for(bool x:{true,false}){const auto& ae=x?a.xEdges:a.yEdges;const auto& be=x?b.xEdges:b.yEdges;
  for(size_t i=0;i<ae.size();++i)need(ae[i].object==be[i].object&&ae[i].maximum==be[i].maximum,"complete graph preserves saved edge order");
 }
}
static void ammoRestoreContinuation(GameWorld& w,const SceneContent& blueprint,const Level& l,size_t survivors){
 auto copy=blueprint;GameWorld resumed;load(resumed,copy,l);std::string e;const auto checkpoint=disk(w);
 need(resumed.restoreSnapshot(checkpoint,e),"restored Ammo disk checkpoint: "+e);
 resumed.setDebugEnemyAI(w.debugEnemyAI());need(bytes(w)==bytes(resumed),"Ammo disk restore preserves its own checkpoint exactly");
 for(unsigned tick=0;tick<8;++tick){w.tick(.017f,{});resumed.tick(.017f,{});
  checkAmmoGraph(w.snapshot(),"live tick "+std::to_string(tick));checkAmmoGraph(resumed.snapshot(),"disk tick "+std::to_string(tick));
  need(w.projectiles().size()==survivors,"only expired Ammo is removed; eligible and disabled bodies survive");
  need(bytes(w)==bytes(resumed),"Ammo continuation is byte-identical to its own saved construction path");
 }
}
static void ammoRestoreGraph(){
 for(bool nativeEnemy:{true,false})for(bool legacy:{false,true}){
  auto s=fixture();s.classes[2].flags.invisible=false;
  for(auto& n:s.classes[2].properties)if(n.key=="Keep-bb-when-invisible")n.values={"0"};
  if(!nativeEnemy)s.objects.resize(1);
  const auto blueprint=s;const auto l=arena();GameWorld w;load(w,s,l);auto state=w.snapshot();
  for(unsigned kind=0;kind<5;++kind){Projectile shot;shot.classId={2};shot.owner=0;shot.position={2500+float(kind)*100,1500};shot.radius=kind==1?6:kind==3?0:1;shot.range=10000;shot.ageMs=1000;shot.lifetimeMs=kind==4?1005:10000;shot.alive=kind!=2;
   if(kind==1){shot.phaseHidden=true;shot.phaseInitialized=true;shot.inactiveMs=1000;}
   vyruz_test::appendProjectile(state,s,shot);
  }
  // Retain a stale captured owner UID, not a newly recaptured live creator.
  state.projectiles[1].nativeOwnerGeneration--;
  state.actors[0].nativeAmmoOwner={3,7,2,4};state.ammoHits=11;state.ammoMisses=13;
  state.spiderNavigation.reset(); // No simulation visit yet: no route owns points.
  if(legacy)vyruz_test::pre32AmmoLifecycle(state);
  need(validSpiderNavigationSnapshot(state)&&validMissileNativeNavigationSnapshot(state),"graph-absent fixture is a valid real checkpoint");
  std::string e;need(w.restoreSnapshot(state,e),"graph-absent Ammo restore: "+e);const auto restored=w.snapshot();
  need(restored.spiderNavigation.has_value()==nativeEnemy,"graph-absent restore retains no-native-enemy initializer early return");
  checkAmmoGraph(restored,legacy?"legacy graph-absent restore":"v32 graph-absent restore");
  if(!legacy){
   auto expected=state;expected.spiderNavigation=restored.spiderNavigation;
   for(size_t i=0;i<expected.projectiles.size();++i)expected.projectiles[i].spiderGraphNode=restored.projectiles[i].spiderGraphNode;
   SaveGame save;save.currentLevel=s.mapPath;save.levels[save.currentLevel].world=expected;std::vector<uint8_t> encoded;
   need(encodeSave(save,encoded,e),e);need(encoded==bytes(w),"graph creation changes only graph and handles, preserving identities, cells, counters and captured owner UIDs");
  }else{
   need(restored.projectiles[0].nativeBareBody&&restored.projectiles[0].nativeGeneration&&restored.projectiles[0].nativeBodyIdentity,"genuine pre32 fields migrate before graph enrollment");
   need(restored.projectiles[0].nativeOwnerGeneration==restored.actors[0].nativeGeneration&&restored.actors[0].nativeAmmoOwner.activeAmmo==4,"legacy live owner generation/count reconstructed once");
  }
  ammoRestoreContinuation(w,blueprint,l,3);
 }
 // A complete graph includes real route-owned points and a retired slot, so a
 // global rebuild or a caller which re-enrolls saved bounds cannot pass.
 auto s=fixture();s.classes[2].flags.invisible=false;
 s.objects[1].position={1500,1000};s.objects[1].behaviours.push_back("Seeker");
 s.objects[1].properties.push_back(block("TYPE",{value("Target","Player"),value("Sensor-Range","600"),value("Scan-Delay","1000"),value("Min-distance","300"),value("Vision-Angle","360")},"Seeker"));
 const auto blueprint=s;const auto l=arena();GameWorld w;load(w,s,l);w.setDebugEnemyAI(true);w.tick(.017f,{});auto complete=w.snapshot();
 need(complete.actors[1].ordinaryAI&&complete.actors[1].ordinaryAI->targeting.search&&complete.actors[1].ordinaryAI->targeting.search->seen.size()>=2,"complete-graph control has real route-owned search points");
 Projectile shot;shot.classId={2};shot.owner=0;shot.position={2500,1500};shot.radius=1;shot.range=10000;shot.ageMs=1000;shot.lifetimeMs=10000;vyruz_test::appendProjectile(complete,s,shot);
 auto& stored=complete.projectiles.back();const auto p=stored.nativeBareBody->motion.center;
 stored.spiderGraphNode=insertNativeNavigationObject(*complete.spiderNavigation,(uint64_t(1)<<62)|stored.nativeBodyIdentity,{p.x,p.y,p.x,p.y});
 const auto retired=insertNativeNavigationPoint(*complete.spiderNavigation,{123,456});removeNativeNavigationObject(*complete.spiderNavigation,retired.graphNode);
 checkAmmoGraph(complete,"complete-graph fixture");std::string e;need(w.restoreSnapshot(complete,e),e);checkSameGraph(*complete.spiderNavigation,*w.snapshot().spiderNavigation);
 SaveGame save;save.currentLevel=s.mapPath;save.levels[save.currentLevel].world=complete;std::vector<uint8_t> encoded;
 need(encodeSave(save,encoded,e),e);need(encoded==bytes(w),"complete-graph restore retains route ownership and all saved state exactly");
 checkAmmoGraph(w.snapshot(),"complete-graph restore");ammoRestoreContinuation(w,blueprint,l,1);
 std::cout<<"Ammo restore: graph-absent v32/pre32, native-enemy/projectile-only, disabled/expired/zero-radius, complete owned graph and disk continuation passed\n";
}
static Projectile shock(GameWorld& w,ContentPosition p,float damage=1000,float radius=64,size_t owner=0){Projectile s;s.classId={2};s.position=p;s.owner=owner;s.nativeOwnerGeneration=w.actors()[owner].nativeGeneration;s.ageMs=1000;s.damage=damage;s.damageRadius=radius;s.slopeAltitude=10;return s;}
static void units(){
 {auto s=fixture();const auto blueprint=s;const auto l=arena();GameWorld w;load(w,s,l);AmmoImpactNativeTestAccess::fire(w);
  need(w.projectiles().size()==1&&w.actors().size()==3,"bare launch has compact body without heavyweight Actor allocation");
  const auto& body=w.projectiles()[0];need(!body.visual&&body.nativeBareBody&&body.nativeGeneration&&body.nativeOwnerGeneration==w.player().nativeGeneration,"fresh generation and owner capture");
  need(body.nativeBareBody->energy==500&&body.nativeBareBody->shield==5000,"authored bare health initialization");
  need(w.snapshot().bareAmmoCells.links.size()==1&&w.player().nativeAmmoOwner.activeAmmo==1,"bare cell enrollment and successful launch counter");
  const auto beforeMalformed=bytes(w);std::string rejection;
  for(unsigned kind=0;kind<6;++kind){auto bad=w.snapshot();const auto identity=body.nativeBodyIdentity;
    if(kind==0)bad.projectiles[0].nativeBareBody->damagePending=true;
    if(kind==1)bad.projectiles.push_back(bad.projectiles[0]);
    if(kind==2)bad.bareAmmoCells.links.at(identity).next=identity;
    if(kind==3)bad.actorCells->arrivalOrder[0]=bad.bareAmmoCells.links.at(identity).arrivalOrder;
    if(kind==4)bad.nextNativeCellOrder=bad.bareAmmoCells.links.at(identity).arrivalOrder;
    if(kind==5)bad.nextNativeBodyIdentity=identity;
    need(!w.restoreSnapshot(bad,rejection),"malformed known Ammo lifecycle rejected transactionally kind="+std::to_string(kind));
    need(bytes(w)==beforeMalformed,"rejected Ammo lifecycle leaves current world intact");
  }
  {auto legacy=w.snapshot();vyruz_test::pre32AmmoLifecycle(legacy);
    auto oldCopy=blueprint;GameWorld oldWorld;load(oldWorld,oldCopy,l);need(oldWorld.restoreSnapshot(legacy,rejection),rejection);
    need(oldWorld.projectiles()[0].nativeBareBody&&oldWorld.projectiles()[0].nativeBareBody->energy==500&&oldWorld.snapshot().bareAmmoCells.links.size()==1,"legacy missing compact body/generation/order rebuilt from bound resources");
    need(oldWorld.player().nativeAmmoOwner.activeAmmo==1&&oldWorld.projectiles()[0].nativeOwnerGeneration==oldWorld.player().nativeGeneration,"legacy surviving-shot count and owner generation reconstructed");
  }
  auto p=shock(w,body.position);p.ageMs=body.ageMs+16;AmmoImpactNativeTestAccess::impact(w,p);
  need(w.projectiles()[0].nativeBareBody->shield==5000,"same-frame Ammo sibling is excluded at inclusive age boundary");
  p.ageMs=body.ageMs+17;AmmoImpactNativeTestAccess::impact(w,p);
  need(w.projectiles()[0].nativeBareBody->shield==4000&&w.projectiles()[0].nativeBareBody->energy==500,"older bare Ammo receives radial shield damage");
  p.ageMs=1000;p.damage=10000;AmmoImpactNativeTestAccess::impact(w,p);
  need(w.projectiles()[0].alive&&w.projectiles()[0].nativeBareBody->damagePending&&w.projectiles()[0].nativeBareBody->energy==-5500,"bare lethal health remains pending until its own visit");
  need(w.player().nativeAmmoOwner.kills==1&&w.playerKills()==1&&w.snapshot().fastKillCount==0,"radial Ammo victim credits owner but is not Fast Kill enemy");
  auto pending=w.snapshot();pending.projectiles[0].phaseInitialized=true;pending.projectiles[0].inactiveMs=100;pending.projectiles[0].slopeEnabled=true;pending.projectiles[0].slopeHeight=pending.projectiles[0].slopeAltitude=1;pending.projectiles[0].slopeRadians=-1;
  std::string e;need(w.restoreSnapshot(pending,e),e);w.setDebugEnemyAI(false);w.setDebugGodMode(true);
  for(int n=0;n<2;++n)w.tick(.02f,{});
  auto copy=blueprint;GameWorld resumed;load(resumed,copy,l);need(resumed.restoreSnapshot(disk(w),e),e);resumed.setDebugEnemyAI(false);resumed.setDebugGodMode(true);
  for(int n=0;n<2;++n){w.tick(.02f,{});resumed.tick(.02f,{});need(bytes(w)==bytes(resumed),"off-phase bare pending death disk continuation");need(w.projectiles().size()==1&&w.projectiles()[0].slopeAltitude==1,"inactive pending bare body does not move or ground-impact");}
  w.tick(.02f,{});resumed.tick(.02f,{});need(w.projectiles().empty()&&w.snapshot().bareAmmoCells.links.empty(),"activation consumes bare pending death and unlinks body");need(bytes(w)==bytes(resumed),"activated pending death exact disk continuation");
 }
 {auto s=fixture();const auto l=arena();GameWorld w;load(w,s,l);AmmoImpactNativeTestAccess::fire(w);auto state=w.snapshot();state.actors[0].nativeAmmoOwner.activeAmmo=3;std::string e;need(w.restoreSnapshot(state,e),e);
  auto consumed=w.projectiles()[0];consumed.damageRadius=0;consumed.damage=1;AmmoImpactNativeTestAccess::impact(w,consumed,2);
  need(w.player().nativeAmmoOwner.activeAmmo==2,"direct hit independently decrements active word");
  AmmoImpactNativeTestAccess::release(w,consumed);need(w.player().nativeAmmoOwner.activeAmmo==1,"immediate physical removal decrements independently of direct hit");
  AmmoImpactNativeTestAccess::release(w,consumed);need(w.player().nativeAmmoOwner.activeAmmo==1,"repeated physical cleanup does not decrement again");
  AmmoImpactNativeTestAccess::retire(w);w.tick(.014f,{});need(w.player().nativeAmmoOwner.activeAmmo==1&&w.projectiles().empty(),"end-of-pass cleanup preserves exactly direct-plus-one-release decrements");
 }
 {auto s=fixture();ObjectClass fx;fx.id={4};fx.name="Audit-HitFX";fx.properties={value("Sub-type","FX"),block("TYPE",{},"FX")};s.classes.push_back(fx);const auto l=arena();GameWorld w;load(w,s,l);auto p=shock(w,s.objects[2].position,1,0,1);p.nativeOwnerGeneration--;const auto captured=p.nativeOwnerGeneration;p.hitEffect=fx.name;
  AmmoImpactNativeTestAccess::impact(w,p,2);const auto id=w.actors().size()-1;
  need(s.objects[id].owner==ObjectId{1}&&w.actors()[id].nativeCreatorGeneration==captured,"HitFX copies stale pointer and captured UID without reviving owner");
  auto descendant=shock(w,s.objects[2].position);descendant.owner=id;descendant.nativeOwnerGeneration=w.actors()[id].nativeGeneration;
  need(!AmmoImpactNativeTestAccess::owned(w,descendant)&&!s.objects[id].owner&&w.actors()[id].nativeCreatorGeneration==captured,"ancestry clears stale FX pointer but preserves captured UID");
 }
 {auto s=fixture();ObjectClass fx;fx.id={4};fx.name="Audit-DeathFX";fx.properties={value("Sub-type","FX"),block("TYPE",{},"FX")};s.classes.push_back(fx);s.objects[1].properties.push_back(block("TYPE",{value("Boom-fx",fx.name),value("Damage","10"),value("Damage-zone","10")},"Explosion"));const auto l=arena();GameWorld w;load(w,s,l);auto state=w.snapshot();state.objects[1].owner=ObjectId{0};state.actors[1].nativeCreatorGeneration=state.actors[0].nativeGeneration-1;const auto captured=state.actors[1].nativeCreatorGeneration;std::string e;need(w.restoreSnapshot(state,e),e);w.damage(1,20000,std::nullopt);
  const auto id=w.actors().size()-1;need(w.actors()[id].explosion&&s.objects[id].owner==ObjectId{0}&&w.actors()[id].nativeCreatorGeneration==captured,"death products copy captured stale owner UID before source release");need(w.actors()[1].nativeGeneration==0,"ordinary released actor UID becomes zero");
 }
 for(bool radial:{false,true}){auto s=fixture();const auto l=arena();GameWorld w;load(w,s,l);auto p=shock(w,s.objects[2].position,1,radial?64:0,1);p.nativeOwnerGeneration--;const auto stale=p.owner;
  AmmoImpactNativeTestAccess::impact(w,p,2);need(w.actors()[1].nativeAmmoOwner.hits==0&&w.actors()[1].nativeAmmoOwner.kills==0,"stale immediate owner cannot receive stats");
  need(radial?p.owner==SIZE_MAX:p.owner==stale,"radial validates/clears ancestry; ordinary radius0 direct does not clear");
 }
 {auto s=fixture();const auto l=arena();GameWorld w;load(w,s,l);auto state=w.snapshot();state.objects[1].owner=ObjectId{0};state.actors[1].nativeCreatorGeneration=state.actors[0].nativeGeneration-1;std::string e;need(w.restoreSnapshot(state,e),e);auto p=shock(w,s.objects[2].position,1,64,1);need(!AmmoImpactNativeTestAccess::owned(w,p),"stale middle creator is not player-owned");need(p.owner==1&&!s.objects[1].owner,"only first stale middle link clears, immediate source remains valid");}
 {auto s=fixture();const auto l=arena();GameWorld w;load(w,s,l);auto state=w.snapshot();state.nextNativeGeneration=UINT32_MAX;state.nativePool->pool.nextGeneration=UINT32_MAX;std::string e;need(w.restoreSnapshot(state,e),e);need(AmmoImpactNativeTestAccess::allocate(w)==UINT32_MAX&&AmmoImpactNativeTestAccess::allocate(w)==0&&AmmoImpactNativeTestAccess::allocate(w)==1,"allocator raw UID wraps through zero without skipping");}
 std::cout<<"bare health/age/pending/off-phase/cells/disk and owner generation contracts passed\n";
}
static void resources(const char* data){Vfs v;need(v.init(data),v.error());ContentReader read=[&](const auto&p,auto&b){return v.read(p,b);};unsigned maps=0,cases=0;
 for(const auto& map:debugLevels()){SceneContent original;std::string e;need(loadSceneContent(read,map,original,e),e);++maps;
  for(const auto& cls:original.classes){if(!attribute(cls.properties,"ammo"))continue;const bool create=std::any_of(cls.properties.begin(),cls.properties.end(),[](const auto&n){return contentNameIs(n.key,"create");});if((cls.sprite.sequence.value&&!cls.flags.invisible.value_or(false))||attribute(cls.properties,"explosion")||create)continue;
   auto s=original;s.objects.clear();s.player=ObjectId{0};ObjectClass gun;gun.id={uint32_t(s.classes.size())};gun.name="Audit-Gun";gun.properties={block("TYPE",{value("Ammo",cls.name),value("Start-distance","250"),value("Delay","1000")},"Weapon")};s.classes.push_back(gun);
   auto player=original.objects[original.player->value];player.id={0};player.owner.reset();player.position={1000,1000};player.properties={block("Energy",{value("Initial","10000"),value("Max","10000")}),block("ADD",{value("Name","Audit-Gun")},"Weapon"),block("ADD",{value("Name",cls.name),value("Count","10"),value("Max","10")},"Ammo")};s.objects={player};const auto l=arena();GameWorld w;load(w,s,l);AmmoImpactNativeTestAccess::fire(w);
   need(w.projectiles().size()==1&&w.actors().size()==1,map+" "+cls.name+" compact original bare launch");const auto& body=w.projectiles()[0];need(body.nativeBareBody&&body.nativeBareBody->energy==500&&body.nativeBareBody->shield==5000,"original bare health census");auto p=shock(w,body.position);p.classId=cls.id;AmmoImpactNativeTestAccess::impact(w,p);need(w.projectiles()[0].nativeBareBody->shield==4000,"original bare radial shield damage");p.damage=10000;AmmoImpactNativeTestAccess::impact(w,p);need(w.projectiles()[0].nativeBareBody->energy==-5500&&w.projectiles()[0].nativeBareBody->damagePending,"original bare deferred lethal damage");w.tick(.014f,{});need(w.projectiles().empty()&&w.snapshot().bareAmmoCells.links.empty(),"original bare own-visit release");need(w.player().nativeAmmoOwner.kills==1&&w.snapshot().fastKillCount==0,"original bare owner kill with no Fast Kill");++cases;
  }
 }need(maps==25&&cases==100,"all original bare Ammo catalog blocks");std::cout<<"original bare Ammo resource maps="<<maps<<" cases="<<cases<<" passed\n";
}
int main(int argc,char**argv){try{ammoRestoreGraph();units();if(argc==2)resources(argv[1]);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
