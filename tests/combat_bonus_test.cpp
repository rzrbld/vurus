#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace vyruz;
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode val(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
static TextNode block(std::string k,std::vector<TextNode> v,std::string arg={}){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=v;if(!arg.empty())n.arguments={arg};return n;}
static void place(SceneContent& s,unsigned cls,ContentPosition p){ObjectPlacement o;o.id={unsigned(s.objects.size())};o.classId={cls};o.position=p;o.properties=s.classes[cls].properties;o.behaviours=s.classes[cls].behaviours;o.sprite=s.classes[cls].sprite;o.flags=s.classes[cls].flags;s.objects.push_back(o);}
static SceneContent fixture(int count=4){
 SceneContent s;s.mapPath="TEST/BONUS.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={val("Name","Pilot"),block("Energy",{val("Initial","1000"),val("Max","1000")})};
 ObjectClass enemy;enemy.id={1};enemy.name="Enemy";enemy.behaviours={"Fixed","Seeker"};enemy.properties={val("Sub-type","Cop"),val("Reward","17"),val("Bounding-box-size","8"),block("Energy",{val("Initial","10"),val("Max","10")}),block("TYPE",{val("Target","Player"),val("Sensor-Range","0")},"Seeker")};
 ObjectClass neutral=enemy;neutral.id={2};neutral.name="Neutral";neutral.behaviours={"Fixed"};
 ObjectClass ammo;ammo.id={3};ammo.name="AreaAmmo";ammo.properties={block("TYPE",{},"Ammo")};
 ObjectClass generator;generator.id={4};generator.name="Generator";generator.properties={val("Sub-type","StaticGen"),val("Bounding-box-size","20"),block("Energy",{val("Initial","100"),val("Max","100")}),block("TYPE",{val("Product","Enemy"),val("Max-births","1"),val("Birth-delay","1000"),val("Birth-duration","100"),val("Active","1")},"Generator")};
 s.classes={player,enemy,neutral,ammo,generator};place(s,0,{100,100});s.player=ObjectId{0};
 for(int i=0;i<count;++i)place(s,1,{500.f+30*i,500});place(s,2,{900,900});return s;
}
struct Run{SceneContent scene;Level level;GameWorld world;std::string error;
 explicit Run(int n=4):scene(fixture(n)){level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;need(vyruz_test::loadManagedFixture(world,scene,level,error),error);world.setDebugEnemyAI(false);}
 void ticks(int n){while(n--)world.tick(.0625f,{});}
 int64_t money()const{return world.player().vitals.money;}
 bool notice(const std::string& text)const{for(const auto& n:world.notifications().entries())if(n.text.find(text)!=std::string::npos)return true;return false;}
 void kill(size_t id,bool radial=true,std::optional<size_t> source=0){world.damage(id,100,source,radial);}
};
int main(){try{
 // Profile context, never the vehicle Name. Pending rewards use the current
 // name at delivery, and checkpoint rollback cannot overwrite that setting.
 {Run r(2);r.world.setPlayerName("First Pilot");r.kill(1);
  r.world.setPlayerName("Pilot %s #2");r.kill(2);
  need(r.notice("professional Pilot %s #2 !")&&!r.notice("professional Pilot !"),"last-enemy uses active profile");
  const auto pending=r.world.snapshot();r.world.setPlayerName("After Rename");
  need(r.world.restoreSnapshot(pending,r.error),r.error);r.ticks(8);
  need(r.notice("Well done After Rename !!!"),"pending Fast Kill reads renamed profile after restore");
  auto noCash=r.world.snapshot();noCash.actors[noCash.player].vitals.money=0;
  need(r.world.restoreSnapshot(noCash,r.error),r.error);r.world.toggleMap();need(r.notice("After Rename,\n"),"map warning uses active profile");
  need(r.scene.objects[0].properties[0].values[0]=="Pilot","profile does not rename the authored vehicle");}
 // Native accuracy measures contact results rather than launches or radial victims.
 for(int kind=0;kind<5;++kind){Run r;auto state=r.world.snapshot();
  if(kind>=2){state.objects[1].properties[0]=val("Sub-type","Item");state.actors[1].hudEnemy=false;state.actors[1].enemyTurn.reset();state.actors[1].ordinaryAI.reset();}
  if(kind==3)state.actors[1].indestructible=true;
  if(kind==4){state.objects[1].properties[0]=val("Sub-type","Decoration");}
  Projectile shot;shot.classId={3};shot.owner=kind==1?3:0;shot.position=state.objects[1].position;shot.position.x-=5;shot.motionConfig.inertia=true;shot.motion.velocityX=1;shot.motion.velocityZ=-1;shot.motion.magnitude=std::sqrt(2.f);shot.radius=2;shot.damage=0;shot.ignoreOwner=true;
  vyruz_test::appendProjectile(state,r.scene,shot);need(r.world.restoreSnapshot(state,r.error),r.error);r.ticks(1);
  const bool hit=kind==0||kind==2;const bool miss=kind==3||kind==4;
  need(r.world.ammoHits()==unsigned(hit)&&r.world.ammoMisses()==unsigned(miss),"contact classification and direct player ownership");
  const auto saved=r.world.snapshot();const auto carry=r.world.carry();need(carry.ammoHits==unsigned(hit)&&carry.ammoMisses==unsigned(miss),"contact totals carried across levels");
  Run next;next.world.restore(carry);need(next.world.ammoHits()==unsigned(hit)&&next.world.ammoMisses()==unsigned(miss),"carry restores contact totals");
  need(r.world.restoreSnapshot(saved,r.error)&&r.world.ammoHits()==unsigned(hit)&&r.world.ammoMisses()==unsigned(miss),"snapshot preserves contact totals");
 }
 {Run r;auto state=r.world.snapshot();Projectile shot;shot.classId={3};shot.owner=0;shot.position={1500,1500};shot.fuseMs=10;shot.ignoreOwner=true;vyruz_test::appendProjectile(state,r.scene,shot);need(r.world.restoreSnapshot(state,r.error),r.error);r.ticks(1);need(r.world.ammoHits()==0&&r.world.ammoMisses()==0,"fuse expiry does not count as a missed contact");}
 // A single kill gives its normal reward, with no combo bonus.
 {Run r;r.kill(1);r.ticks(8);need(r.money()==17&&!r.notice("Fast Kill"),"one kill does not earn Fast Kill");need(r.world.snapshot().fastKillCount==0,"single window expires");}
 // Window remains anchored to the FIRST kill; money and lifetime earnings both credit once.
 for(int count:{2,3}){Run r;r.kill(1);r.ticks(6);r.kill(2);if(count==3){r.ticks(1);r.kill(3);}else r.ticks(1);
  need(r.money()==count*17,"no premature combo payment");need(r.world.snapshot().fastKillMs==62.5,"later kills do not extend first-kill deadline");
  r.ticks(1);need(r.money()==count*(17+5000),"fixed-window combo pays5000 per kill");need(r.world.player().vitals.lifetimeMoney==r.money(),"bonus credits lifetime earnings too");
  need(r.notice("Fast Kill Bonus +"+std::to_string(count*5000)),"bonus amount notice");
  const Notification* bonus=nullptr;for(const auto& n:r.world.notifications().entries())if(n.text.find("Fast Kill")!=std::string::npos)bonus=&n;
  need(bonus&&bonus->lifetimeMs==8000&&bonus->x==100&&bonus->y==138&&bonus->font==6&&!bonus->reveal&&!bonus->worldSpace,"native Fast Kill presentation parameters");
  r.ticks(8);need(r.money()==count*(17+5000),"expired combo cannot pay twice");}
 // Non-radial shots, another owner's damage, neutral objects and repeated deaths cannot build it.
 {Run r;r.kill(1,false);r.kill(2,true,3);r.kill(5);r.kill(3);r.kill(3);r.ticks(8);need(r.money()==4*17&&!r.notice("Fast Kill"),"eligibility exclusions and repeated dead target");}
 // Ownership follows ancestors rather than requiring the immediate source to be the player.
 {Run r;r.kill(1);r.ticks(8);r.kill(2);r.kill(3);r.ticks(8);need(r.money()==10051,"expired single kill cannot join a later combo");}
 {Run r;r.kill(1);r.kill(2);r.world.damage(0,100000);r.ticks(8);need(r.money()==34&&!r.notice("Fast Kill"),"dead player receives no pending Fast Kill award");}
 {Run r;r.scene.objects[5].owner=ObjectId{0};r.kill(1,true,5);r.kill(2,true,5);r.ticks(8);need(r.money()==10034,"player-owned secondary source qualifies");}
 // Snapshot rollback resumes exact combo state; disk sessions intentionally reset it.
 {Run r;r.kill(1);r.ticks(3);r.kill(2);const auto saved=r.world.snapshot();need(saved.fastKillMs==312.5&&saved.fastKillCount==2,"snapshot captures partial window");
  r.ticks(5);need(r.money()==10034,"original continuation pays");need(r.world.restoreSnapshot(saved,r.error),r.error);r.ticks(4);need(r.money()==34,"rollback preserves deadline");r.ticks(1);need(r.money()==10034,"rollback pays exactly once on replay");
  SaveGame game;game.currentLevel=r.scene.mapPath;game.levels[game.currentLevel].world=saved;std::vector<uint8_t> bytes;SaveGame decoded;
  need(encodeSave(game,bytes,r.error)&&decodeSave(bytes,decoded,r.error),r.error);const auto& disk=decoded.levels.at(game.currentLevel).world;
  need(disk.fastKillMs==0&&disk.fastKillCount==0&&!disk.bonusEnemies,"disk omits native transient bonus state");need(r.world.restoreSnapshot(disk,r.error),r.error);r.ticks(8);need(r.money()==34&&!r.notice("Fast Kill"),"disk resume never replays pending combo");}
 // Exercise the actual delayed projectile's radial dispatch rather than only calling damage().
 {Run r;auto state=r.world.snapshot();state.objects[1].position={500,500};state.objects[2].position={510,500};
  vyruz_test::synchronizeActor(state,1);vyruz_test::synchronizeActor(state,2); // Deliberately relocated recipients retain valid cells/order.
  Projectile shot;shot.classId={3};shot.owner=0;shot.position={500,500};shot.radius=0;shot.damage=1000;shot.damageRadius=40;shot.fuseMs=125;shot.damageRemainingMs=125;shot.phaseInitialized=true;shot.ignoreOwner=true;
  vyruz_test::appendProjectile(state,r.scene,shot);need(r.world.restoreSnapshot(state,r.error),r.error);r.ticks(2);
  need(r.world.actors()[1].nativeDamagePending&&r.world.actors()[2].nativeDamagePending,"projectile area hit leaves both deaths for their next actor visit");
  need(r.world.snapshot().fastKillCount>=2,"projectile radial owner builds combo");r.ticks(1);
  need(!r.world.actors()[1].alive&&!r.world.actors()[2].alive,"later actor visit consumes both projectile deaths");r.ticks(8);need(r.notice("Fast Kill"),"projectile combo produces delayed award");}
 // User-requested fix pays the original message's advertised amount on its
 // existing native zero transition, separately from ordinary Reward/Fast Kill.
 {Run r(1);const auto before=r.world.snapshot();r.kill(1,false,std::nullopt);need(r.money()==100017,"last enemy pays advertised Real Killer plus ordinary reward");need(r.world.player().vitals.lifetimeMoney==100017,"Real Killer credits total earnings as well as spendable cash");need(r.notice("Real Killer Bonus : +100000 $"),"last enemy zero transition announced regardless owner");
  const auto& n=r.world.notifications().entries().front();need(n.lifetimeMs==10000&&n.x==70&&n.y==138&&n.font==6&&n.mode==7&&!n.reveal&&!n.worldSpace,"native last-enemy notice parameters");r.kill(1);r.ticks(8);need(r.money()==100017&&r.world.player().vitals.lifetimeMoney==100017,"dead repeat and notice updates cannot repay Real Killer");
  need(r.world.restoreSnapshot(before,r.error),r.error);r.kill(1,false,std::nullopt);need(r.money()==100017&&r.world.player().vitals.lifetimeMoney==100017,"memory rollback replays one clear from the rolled-back earnings");}
 {Run empty(0);empty.ticks(8);need(!empty.notice("Real Killer")&&empty.money()==0&&empty.world.player().vitals.lifetimeMoney==0,"initially empty level does not announce or pay a zero transition");}
 // A pending last death pays only when consumed; disk reconstruction neither
 // loses that future transition nor repays a level that was already cleared.
 {Run r(1);auto state=r.world.snapshot();Projectile shot;shot.classId={3};shot.owner=0;shot.position=state.objects[1].position;shot.damage=1000;shot.damageRadius=20;shot.fuseMs=1;shot.phaseInitialized=true;shot.damageRemainingMs=0;vyruz_test::appendProjectile(state,r.scene,shot);
  need(r.world.restoreSnapshot(state,r.error),r.error);r.ticks(1);need(r.world.actors()[1].nativeDamagePending&&r.money()==0&&r.world.player().vitals.lifetimeMoney==0,"pending lethal hit does not pay before actor death");
  const auto disk=[&](const WorldSnapshot& snapshot){SaveGame save,read;save.currentLevel=r.scene.mapPath;save.levels[save.currentLevel].world=snapshot;std::vector<uint8_t> bytes;need(encodeSave(save,bytes,r.error)&&decodeSave(bytes,read,r.error),r.error);return read.levels.at(save.currentLevel).world;};
  Run resumed(1);need(resumed.world.restoreSnapshot(disk(r.world.snapshot()),resumed.error),resumed.error);resumed.world.setDebugEnemyAI(false);r.ticks(1);resumed.ticks(1);
  need(r.money()==100017&&resumed.money()==100017&&r.world.player().vitals.lifetimeMoney==100017&&resumed.world.player().vitals.lifetimeMoney==100017,"live and disk pending-death continuation pay exactly one clear");
  Run cleared(1);need(cleared.world.restoreSnapshot(disk(r.world.snapshot()),cleared.error),cleared.error);cleared.world.setDebugEnemyAI(false);need(cleared.world.snapshot().bonusEnemies==0,"disk reconstructs an already-cleared hostile counter");cleared.kill(1);cleared.ticks(16);
  need(cleared.money()==100017&&cleared.world.player().vitals.lifetimeMoney==100017&&!cleared.notice("Real Killer"),"loading and revisiting cleared level never repays its bonus");}
 {Run r(1);auto state=r.world.snapshot();state.actors[0].vitals.money=std::numeric_limits<int64_t>::max()-50000;state.actors[0].vitals.lifetimeMoney=std::numeric_limits<int64_t>::max()-1;need(r.world.restoreSnapshot(state,r.error),r.error);r.kill(1,false,std::nullopt);
  need(r.money()==std::numeric_limits<int64_t>::max()&&r.world.player().vitals.lifetimeMoney==std::numeric_limits<int64_t>::max(),"Real Killer and ordinary reward saturate both earnings counters without overflow");}
 // Native generators count themselves AND successful hostile births.
 {Run r(0);r.scene.objects.resize(1);place(r.scene,4,{500,500});need(vyruz_test::loadManagedFixture(r.world,r.scene,r.level,r.error),r.error);r.world.setDebugEnemyAI(false);
  need(r.world.snapshot().bonusEnemies==1,"hostile-product generator initially counts");r.ticks(1);need(r.world.actors().size()==3&&r.world.snapshot().bonusEnemies==2,"hostile birth increments native counter");
  r.world.damage(1,1000,0,false);need(!r.notice("Real Killer")&&r.money()==0,"live spawned enemy prevents early last-enemy payment");r.kill(2,false);need(r.notice("Real Killer")&&r.money()==100017&&r.world.player().vitals.lifetimeMoney==100017,"last newborn death pays clear bonus plus ordinary reward");}
 std::cout<<"combat bonus fixtures passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
