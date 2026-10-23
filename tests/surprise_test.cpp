#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <array>
using namespace vyruz;
namespace {
void check(bool b,const char* reason){if(!b){std::cerr<<reason<<'\n';std::exit(1);}}
TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
TextNode block(std::string k,std::vector<TextNode> children,std::string arg={}){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=children;if(!arg.empty())n.arguments={arg};return n;}
constexpr const char* tokens[]={"none","random","malus-reverse-commands","malus-trap","malus-weapon-jam","malus-darken","malus-extasy","malus-low-speed","malus-pinball","malus-banana","malus-blocked-motor","bonus-destroy-around","bonus-touch-and-destroy","bonus-auto-shield-up","bonus-invisible","bonus-circular-fire","bonus-time-decceleration","bonus-reload-weapons","bonus-invulnerability","bonus-triple-fire","bonus-nop"};
struct Fixture {
 SceneContent scene;Level level;GameWorld world;
 Fixture(int kind,bool second=false,bool sloped=false) {
  level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;
  ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};
  p.properties={value("Bounding-box-size","12"),block("Energy",{value("Initial","100"),value("Max","100")}),block("Shield",{value("Initial","50"),value("Max","100")}),block("ADD",{value("Name","Gun")},"Weapon"),block("ADD",{value("Name","Bullet"),value("Count","50"),value("Max","200")},"Ammo")};
  if(sloped)p.properties.push_back(value("Altitude","24"));
  ObjectClass gun;gun.id={1};gun.name="Gun";gun.properties={block("TYPE",{value("Ammo","Bullet"),value("Delay","98"),value("Fire-Range","1000"),value("Visible-gun-fire","1")},"Weapon")};
  if(sloped)gun.properties={block("TYPE",{value("Ammo","Bullet"),value("Delay","98"),value("Initial-slope","-33"),value("Slope-step","0.5"),value("Weapons-gap","20"),value("Left-side","1"),value("Right-side","1")},"Weapon")};
  ObjectClass bullet;bullet.id={2};bullet.name="Bullet";bullet.properties={value("Speed-max","100"),block("TYPE",{value("Damage","1")},"Ammo")};
  if(sloped)bullet.properties.push_back(value("Altitude","24"));
  ObjectClass item;item.id={3};item.name="Surprise";item.properties={value("Carry","1"),value("Bounding-box-size","12"),value("Indestructible","1"),block("TYPE",{value("Only-for-player","1"),value("Surprise-type",tokens[kind]),value("Reactivate-after","-1")},"Item")};
  ObjectClass box;box.id={4};box.name="Box";box.behaviours={"Fixed"};box.properties={value("Bounding-box-size","10"),block("Energy",{value("Initial","100")}),block("Shield",{value("Initial","200")})};
  scene.classes={p,gun,bullet,item,box};place(0,1000,1000);place(3,1000,1000);place(4,1200,1000);
  if(second)place(3,1000,1000);
  scene.player=ObjectId{0};std::string error;check(vyruz_test::loadManagedFixture(world,scene,level,error),error.c_str());world.setPlayerDirection(9);
 }
 void place(size_t cls,float x,float y){ObjectPlacement o;o.id={uint32_t(scene.objects.size())};o.classId={uint32_t(cls)};o.position={x,y};o.properties=scene.classes[cls].properties;o.behaviours=scene.classes[cls].behaviours;scene.objects.push_back(o);}
 void tick(int n=1,WorldInput input={}){for(int i=0;i<n;++i)world.tick(1.f/60,input);}
 void activate(){tick();check(world.pickupsTaken==1,"Surprise collected");check(!world.surprise().applied,"Collection has pending delay");tick(29);check(!world.surprise().applied,"No premature activation");for(int i=0;i<5&&!world.surprise().applied;++i)tick();check(world.surprise().applied,"Effect activates after delay");}
};
}
int main(){
 std::array<int,21> counts{};for(unsigned i=0;i<136;++i)++counts[static_cast<unsigned>(surpriseRandom(i))];
 const std::array<int,21> expected={0,0,10,10,5,10,5,10,0,10,1,5,5,10,10,5,5,5,10,10,10};
 check(counts==expected,"Every random roll matches native weights, including zero-weight Pinball");
 for(int id=2;id<=20;++id){
  Fixture f(id,true);f.activate();check(f.world.surprise().kind==SurpriseKind(id),"All authored effect tokens map correctly");
  check(f.world.actors()[3].alive,"Second question mark refused during pending/active effect");
  // Remove duplicate from fixture before expiry, otherwise legitimate collection
  // of that item would start another effect on the same simulation tick.
  auto s=f.world.snapshot();s.actors[3].alive=false;
  if(s.actorCells)s.actorCells->relocate(3,NativeActorCells::Missing);
  std::string error;
  check(f.world.restoreSnapshot(s,error),"Exact memory snapshot restores active effect");
  check(f.world.surprise().kind==SurpriseKind(id)&&f.world.surprise().applied,"Memory rollback keeps effect state");
  f.tick((surpriseDuration(SurpriseKind(id))+100)/16+5);
  check(f.world.surprise().kind==SurpriseKind::None,"Every effect expires and releases held slot");
 }
 {Fixture f(3);f.activate();check(f.world.player().vitals.energy==75&&f.world.player().vitals.shield==37.5f,"Trap reduces current health and shield by quarter");f.tick(2);check(f.world.player().vitals.energy==75,"Trap resource loss persists after cleanup");}
 {Fixture f(11);f.activate();check(!f.world.actors()[2].alive,"Destroy Around kills nearby object through shield");}
 {Fixture f(13);f.activate();f.tick(15);check(f.world.player().vitals.shield==100,"Auto Shield Up regenerates and clamps");}
 {Fixture f(18);f.activate();f.world.damage(0,10000,2);check(f.world.player().alive&&f.world.player().vitals.energy==100,"Invulnerability blocks normal damage");f.world.cancelSurprise();f.world.damage(0,10000,2);check(!f.world.player().alive,"Protection expires");}
 for(int id:{4,10,14}){Fixture f(id);f.activate();WorldInput in;in.fire=true;f.tick(30,in);check(f.world.shotsFired==0,"Jam/motor/drone refuse ordinary gun fire");f.world.cancelSurprise();f.tick(1,in);check(f.world.shotsFired>0,"Fire resumes after restriction removed");}
 {Fixture f(6);f.world.takeAudioEvents();f.activate();
  const auto events=f.world.takeAudioEvents();unsigned voices=0;
  for(const auto& e:events)if(e.kind==AudioEventKind::Voice){++voices;check(e.name==((static_cast<uint64_t>(f.world.elapsedMilliseconds())&3)<2?"V-ARIBA.WAV":"V-YEEPEE.WAV")&&e.delayMs==3000&&e.holdMs==2000&&e.tailMs==0,"authored XTC pickup queues native delayed voice");}
  check(voices==1,"authored XTC pickup requests one apply voice");
  const auto p=f.world.playerPosition();f.tick(30);check(nativeDistanceSquared(p,f.world.playerPosition())>1,"XTC forces forward without input");
  for(const auto& e:f.world.takeAudioEvents())check(e.kind!=AudioEventKind::Voice,"active authored XTC does not repeat voice");
 }
 {Fixture f(10);f.activate();WorldInput input;input.movement.forward=1;const auto p=f.world.playerPosition();f.tick(30,input);check(nativeDistanceSquared(p,f.world.playerPosition())<.01,"Motor lock prevents acceleration from rest");}
 {Fixture normal(20),reverse(2);normal.activate();reverse.activate();WorldInput input;input.movement.strafe=1;normal.tick(20,input);reverse.tick(20,input);check(normal.world.playerPosition().y>1000&&reverse.world.playerPosition().y<1000,"Reverse swaps strafe direction");}
 {Fixture f(2);f.activate();f.world.setPlayerDirection(0);WorldInput aim;aim.movement.aimActive=true;const auto n=nativeHeading(5);const auto v=nativeToSurface(n.x,n.y);aim.movement.aimX=v.x;aim.movement.aimY=v.y;
  f.tick(100,aim);check(f.world.player().direction==17,"Reverse integration settles opposite native aim");f.tick(100,aim);check(f.world.player().direction==17,"Reverse held aim does not shake");f.world.cancelSurprise();f.tick(100,aim);check(f.world.player().direction==5,"Reverse cleanup restores normal aim");}
 {Fixture f(13);f.activate();auto state=f.world.snapshot();state.elapsedMs=125;state.actors[state.player].vitals.shield=100;std::string error;
  check(f.world.restoreSnapshot(state,error),"restore active shield pulse");check(f.world.player().shieldPulse>0,"full shield still has green pulse");
  check(std::abs(f.world.player().shieldPulse-35.f/255)<.00001f,"native clock125ms gives green byte35");
  state.elapsedMs=500;check(f.world.restoreSnapshot(state,error),"restore trough phase");check(f.world.player().shieldPulse==0,"shield pulse reaches trough at500ms");
  f.world.cancelSurprise();check(f.world.player().shieldPulse==0,"shield cleanup clears visual");}
 {Fixture normal(20),slow(7);normal.activate();slow.activate();WorldInput input;input.movement.forward=1;normal.tick(30,input);slow.tick(30,input);check(slow.world.playerSurfaceSpeed()<normal.world.playerSurfaceSpeed()*.6f,"Thrust Loss reduces movement speed");}
 {Fixture normal(20),slip(9);normal.activate();slip.activate();WorldInput input;input.movement.forward=1;normal.tick(25,input);slip.tick(25,input);normal.tick(30);slip.tick(30);check(slip.world.playerSurfaceSpeed()>normal.world.playerSurfaceSpeed()+10,"Banana retains coasting momentum");}
 {Fixture f(8);f.activate();f.level.collisionEdges.push_back({1040,800,1040,1200});rebuildTerrainIndex(f.level);auto s=f.world.snapshot();const auto v=surfaceToNative(2.8f,0);s.motion.velocityX=v.x;s.motion.velocityZ=v.y;s.motion.magnitude=std::hypot(v.x,v.y);std::string error;check(f.world.restoreSnapshot(s,error),"Pinball starting momentum");bool reflected=false;for(int i=0;i<30;++i){f.tick();const auto motion=f.world.snapshot().motion;reflected|=nativeToSurface(motion.velocityX,motion.velocityZ).x<0;}check(reflected,"Pinball reflects velocity at wall");}
 {Fixture f(12);f.activate();f.world.movePlayer(220,0);f.tick();check(!f.world.actors()[2].alive,"Touch Destroy applies through actor contact");}
 {Fixture f(17);f.activate();check(f.world.player().ammo.at("bullet").count==200,"Ammo bonus refills existing inventory");
  WorldInput in;in.fire=true;f.tick(15,in);check(f.world.player().ammo.at("bullet").count<200,"Temporary ammo is consumed normally");
  SaveGame save;save.currentLevel="TEST/LEVEL.3DG";save.levels[save.currentLevel].world=f.world.snapshot();
  std::vector<uint8_t> bytes;std::string error;SaveGame read;
  check(encodeSave(save,bytes,error)&&decodeSave(bytes,read,error),"Save active ammo bonus");
  check(read.levels.begin()->second.world.actors[0].ammo.at("bullet").count==50&&read.levels.begin()->second.world.surprise.kind==SurpriseKind::None,"Disk save restores baseline ammo and omits effect");
  check(f.world.surprise().active(SurpriseKind::Ammo)&&f.world.player().ammo.at("bullet").count>50,"Saving does not mutate live bonus");
  f.world.cancelSurprise();check(f.world.player().ammo.at("bullet").count==50,"Ammo expiry restores baseline");
 }
 {Fixture f(17);f.activate();
  ObjectClass ammoPickup;ammoPickup.id={uint32_t(f.scene.classes.size())};ammoPickup.name="Bullet-Pickup";
  ammoPickup.properties={value("Bounding-box-size","12"),value("Indestructible","1"),
   block("TYPE",{value("Only-for-player","1"),value("Ammo-Type","Bullet"),value("Add-Ammo","25"),value("Reactivate-after","0")},"Item")};
  f.scene.classes.push_back(ammoPickup);const auto pickup=f.world.spawn(ammoPickup.id,f.world.playerPosition());
  f.world.movePlayer(0,0);
  check(!f.world.actors()[pickup].alive,"Ammo pickup accepted while temporary stock is full");
  check(f.world.player().ammo.at("bullet").count==200&&f.world.surprise().savedAmmo.at("bullet").count==75,"Ammo pickup modifies original baseline, not temporary stock");
  WorldInput in;in.fire=true;f.tick(15,in);
  check(f.world.player().ammo.at("bullet").count<200&&f.world.surprise().savedAmmo.at("bullet").count==75,"Temporary firing preserves ammo collected into baseline");
  f.tick(920);
  check(f.world.surprise().kind==SurpriseKind::None&&f.world.player().ammo.at("bullet").count==75,"Collected ammo survives natural bonus expiry");
  check(!f.world.actors()[pickup].alive,"Zero reactivation does not duplicate collected ammo");
 }
 {Fixture f(17);
  auto start=f.world.snapshot();start.actors[0].ammo.append("bullet",{4,9});start.actors[0].weapons[0].ammoSlot=1;std::string error;
  check(f.world.restoreSnapshot(start,error),"duplicate bonus fixture");f.activate();
  check(f.world.surprise().savedAmmo.size()==2 && f.world.surprise().savedAmmo.slots[0].second.count==4 &&
        f.world.surprise().savedAmmo.slots[1].second.count==50,"native bonus baseline reverses duplicate list");
  check(f.world.player().weapons[0].ammoSlot==0,"bonus application refreshes first nonzero stock selection");
  ObjectClass pickup;pickup.id={uint32_t(f.scene.classes.size())};pickup.name="DuplicatePickup";
  pickup.properties={value("Bounding-box-size","12"),block("TYPE",{value("Only-for-player","1"),value("Ammo-Type","Bullet"),value("Add-Ammo","2")} ,"Item")};
  f.scene.classes.push_back(pickup);f.world.spawn(pickup.id,f.world.playerPosition());f.world.movePlayer(0,0);
  check(f.world.surprise().savedAmmo.slots[0].second.count==6 && f.world.surprise().savedAmmo.slots[1].second.count==50,"bonus pickup visits reversed saved stock first");
  auto memory=f.world.snapshot();check(f.world.restoreSnapshot(memory,error),"duplicate bonus memory checkpoint");
  auto carry=f.world.carry();auto disk=f.world.snapshot();normalizeSavedSurprise(disk);
  check(carry.ammo.slots[0].second.count==50 && carry.ammo.slots[1].second.count==9 &&
        disk.actors[0].ammo.slots[0].second.count==50 && disk.actors[0].ammo.slots[1].second.count==9,"carry and disk normalization match native duplicate cleanup");
  f.world.cancelSurprise();check(f.world.player().ammo.slots[0].second.count==50 && f.world.player().ammo.slots[1].second.count==9,"native first live duplicate consumes all saved namesakes, later stock retains temporary ammo");
 }
 // Native All Weapons expiry restores counts, not the previous weapon.
 for(int mode=0;mode<4;++mode) {
  Fixture f(17);std::string error;
  for(int n=1;n<=2;++n) {
   auto gun=f.scene.classes[1];gun.id={uint32_t(f.scene.classes.size())};gun.name="Gun"+std::to_string(n);
   gun.properties={block("TYPE",{value("Ammo","Bullet"+std::to_string(n)),value("Delay","98"),value("Fire-Range","1000")},"Weapon")};
   f.scene.classes.push_back(gun);
   auto ammo=f.scene.classes[2];ammo.id={uint32_t(f.scene.classes.size())};ammo.name="Bullet"+std::to_string(n);f.scene.classes.push_back(ammo);
   f.scene.objects[0].properties.push_back(block("ADD",{value("Name",gun.name)},"Weapon"));
   f.scene.objects[0].properties.push_back(block("ADD",{value("Name",ammo.name),value("Count",n==1&&mode==1?"7":n==2&&mode==3?"3":"0"),value("Max","200")},"Ammo"));
  }
  check(vyruz_test::loadManagedFixture(f.world,f.scene,f.level,error),"multiweapon bonus fixture");f.activate();
  check(f.world.selectPlayerWeaponSlot(1),"choose temporarily available weapon");
  if(mode==2) {
   ObjectClass pickup;pickup.id={uint32_t(f.scene.classes.size())};pickup.name="BonusAmmoPickup";
   pickup.properties={value("Bounding-box-size","12"),block("TYPE",{value("Only-for-player","1"),value("Ammo-Type","Bullet1"),value("Add-Ammo","5")},"Item")};
   f.scene.classes.push_back(pickup);f.world.spawn(pickup.id,f.world.playerPosition());f.world.movePlayer(0,0);
  }
  auto checkpoint=f.world.snapshot();check(f.world.restoreSnapshot(checkpoint,error),"bonus inventory memory continuation");
  f.tick(920);check(f.world.surprise().kind==SurpriseKind::None,"natural all weapons expiry");
  check(f.world.player().selectedWeapon==1,"expiry itself does not restore a remembered weapon");
  WorldInput fire;fire.fire=true;f.tick(1,fire);
  check(f.world.player().selectedWeapon==size_t(mode==0?0:mode==3?2:1),"firing chooses next loaded weapon or retains usable selection");
  check(f.world.shotsFired>0,"fallback can actually fire");
  auto state=f.world.snapshot();for(auto& stock:state.actors[0].ammo)stock.second.count=0;
  state.actors[0].ammo.at("bullet1").count=1;state.actors[0].ammo.at("bullet").count=5;state.actors[0].selectedWeapon=1;
  state.actors[0].weapons[1].state.lastFireMs.reset();check(f.world.restoreSnapshot(state,error),"last round fixture");
  f.tick(1,fire);check(f.world.player().selectedWeapon==0,"last shot immediately cycles to loaded weapon");
  state=f.world.snapshot();for(auto& stock:state.actors[0].ammo)stock.second.count=0;
  check(f.world.restoreSnapshot(state,error),"all empty fixture");f.tick(1,fire);
  check(f.world.player().selectedWeapon==0,"all empty inventory preserves selection without cycling forever");
 }
 {Fixture f(14);f.activate();auto origin=f.world.surprise().droneOrigin;f.world.setPlayerPosition(origin.x+300,origin.y+100);f.tick(900);f.tick(60);check(nativeDistanceSquared(origin,f.world.playerPosition())<1,"Drone returns to origin on expiry");check(!f.scene.objects[0].flags.invisible.value_or(false),"Drone visibility restored");}
 {Fixture f(14);f.activate();auto origin=f.world.surprise().droneOrigin;f.world.setPlayerPosition(origin.x+300,origin.y);auto s=f.world.snapshot();normalizeSavedSurprise(s);check(s.objects[0].position.x==origin.x&&!s.objects[0].flags.invisible.value_or(false)&&s.actors[0].solid,"Drone save restores body location and collision");check(f.world.playerPosition().x==origin.x+300&&f.world.surprise().applied,"Drone save leaves live excursion unchanged");}
 {Fixture f(19);f.activate();WorldInput input;input.fire=true;f.tick(1,input);check(f.world.shotsFired>=3,"Triple emits extra projectiles");const auto& shots=f.world.projectiles();check(shots.size()>=3,"Triple retains visible projectiles");check(std::abs(shots[0].directionX-shots[1].directionX)<.001&&std::abs(shots[0].directionZ-shots[1].directionZ)<.001,"Triple shots parallel, not angular spread");}
 {Fixture f(15);f.activate();WorldInput input;input.fire=true;f.tick(1,input);f.tick(25);check(f.world.shotsFired>=24,"Circular sweeps all24 directions after release");}
 // 440dd7 pending visits bypass438540 input/slope updates. Keep the angle
 // selected by the initiating input, including a previously warmed weapon.
 for(bool warm:{false,true})for(bool hold:{false,true})for(bool cooldown:{false,true}) {
  Fixture f(15,false,true),resumed(15,false,true);f.activate();
  auto start=f.world.snapshot();auto& weapon=start.actors[0].weapons[0];
  const auto config=weapon.config;
  weapon.state.slopeHeld=warm;weapon.state.slopeRadians=warm?config.initialSlope+10*config.slopeStep:config.initialSlope;
  if(cooldown)weapon.state.lastFireMs=uint64_t(start.elapsedMs);
  std::string error;check(f.world.restoreSnapshot(start,error),"Circular slope initial state");
  const float chosen=warm?weapon.state.slopeRadians+config.slopeStep:config.initialSlope;
  bool restored=false;int ticks=0;
  do {
   WorldInput input;input.fire=hold||ticks==0;f.tick(1,input);
   if(restored)resumed.tick(1,input);
   const auto& state=f.world.player().weapons[0].state;
   check(state.slopeHeld&&state.slopeRadians==chosen,"Pending Circular preserves initiating slope and held state");
   if(f.world.shotsFired)check(!f.world.projectiles().empty(),"Circular slope fixture retains airborne bullets");
   for(const auto& shot:f.world.projectiles())check(shot.slopeRadians==chosen,"Both Circular barrels retain chosen slope");
   if(restored) {
    check(resumed.world.surprise().circularRemaining==f.world.surprise().circularRemaining&&resumed.world.shotsFired==f.world.shotsFired,"Restored pending sweep counters and emissions");
    check(resumed.world.player().weapons[0].state.slopeRadians==chosen,"Restored pending sweep retains slope");
    SaveGame a,b;a.currentLevel=b.currentLevel="TEST/CIRCULAR.3DG";
    a.levels[a.currentLevel].world=f.world.snapshot();b.levels[b.currentLevel].world=resumed.world.snapshot();
    std::vector<uint8_t> x,y;check(encodeSave(a,x,error)&&encodeSave(b,y,error)&&x==y,"Pending sweep deterministic snapshot continuation");
   }
   if(ticks==7){check(resumed.world.restoreSnapshot(f.world.snapshot(),error),"Restore mid-sweep memory snapshot");restored=true;}
   ++ticks;check(ticks<=25,"Circular slope fixture completes bounded sweep");
  }while(f.world.surprise().circularRemaining);
  check(f.world.shotsFired==48&&f.world.player().direction==9,"One Circular ring retains24 directions and paired barrels");
  WorldInput held;held.fire=true;f.tick(1,hold?held:WorldInput{});
  const auto& after=f.world.player().weapons[0].state;
  check(hold?(after.slopeHeld&&after.slopeRadians==chosen+config.slopeStep):(!after.slopeHeld&&after.slopeRadians==config.initialSlope),"Ordinary rearm or release processes slope after ring");
  // Disk saves intentionally remove Surprise. Preserve weapon state and the
  // existing normalization contract rather than persisting a pending ring.
  SaveGame save,read;save.currentLevel="TEST/CIRCULAR.3DG";save.levels[save.currentLevel].world=f.world.snapshot();
  std::vector<uint8_t> bytes;check(encodeSave(save,bytes,error)&&decodeSave(bytes,read,error),"Circular slope disk roundtrip");
  const auto disk=read.levels.begin()->second.world;
  check(disk.surprise.kind==SurpriseKind::None&&disk.surprise.circularRemaining==0,"Disk normalization removes Circular as before");
  check(resumed.world.restoreSnapshot(disk,error),"Restore normalized Circular disk state");
  const float next=disk.actors[0].weapons[0].state.slopeHeld?disk.actors[0].weapons[0].state.slopeRadians+config.slopeStep:config.initialSlope;
  resumed.tick(1,held);check(resumed.world.player().weapons[0].state.slopeRadians==next,"Disk continuation resumes ordinary slope updates");
 }
 {Fixture f(16);f.activate();const auto playerAge=f.world.player().ageMs,otherAge=f.world.actors()[2].ageMs;f.tick(40);check(f.world.player().ageMs-playerAge>600&&f.world.actors()[2].ageMs-otherAge<200,"Time Warp updates player normally and world one in four ticks");}
 // Every effect's malformed transient state must reject transactionally.
 {Fixture f(5);f.activate();auto s=f.world.snapshot();s.surprise.remainingMs=INFINITY;std::string error;check(!f.world.restoreSnapshot(s,error)&&f.world.surprise().active(SurpriseKind::Darkness),"Malformed effect snapshot rejected without damaging live state");}
 std::cout<<"All19 Surprise lifecycle and gameplay checks passed\n";
}
