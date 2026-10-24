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
SceneContent spiderGatlingFixture(){
 auto s=fixture("Spider");auto& gun=s.classes[2].properties[0].children;
 gun.push_back(value("Initial-slope","-45"));gun.push_back(value("Slope-step","0.5"));
 for(auto& n:gun)if(n.key=="Delay")n.values={"98"};
 return s;
}
WorldSnapshot admittedSpider(GameWorld& world){
 prepare(world);for(int tick=0;tick<2;++tick){world.tick(.014f,{});drain(world);}
 auto state=world.snapshot();auto& ai=*state.actors[1].spiderAI;
 need(ai.seekerRoute.action.active,"Spider admitted before slope comparison");
 ai.seekerRoute.action.scanRemainingMs=10000;
 return state;
}
void spiderGatlingRegression(){
 const auto terrain=arena();std::string error;
 // 42caa0 calls42c210 for admitted aim even when42d2d0 cannot set fire.
 // The current World must retain native readiness and update slope once,
 // including through deferred CREATE/emission, rather than once per bullet.
 auto scene=spiderGatlingFixture();GameWorld world;
 need(world.load(scene,terrain,error),error);auto initial=admittedSpider(world);
 need(world.restoreSnapshot(initial,error),error);
 const auto config=world.actors()[1].weapons[0].config;
 float expected=config.initialSlope;unsigned cooldownVisits=0,volleys=0;
 for(unsigned visit=0;visit<120;++visit){
  if(visit)expected=std::min(0.f,expected+config.slopeStep);
  const auto shots=world.shotsFired;world.tick(.014f,{});drain(world);
  const auto& actor=world.actors()[1];const auto& weapon=actor.weapons[0];
  need(weapon.state.slopeHeld&&weapon.state.slopeRadians==expected,"Spider slope matches original once-per-steering recurrence");
  if(world.shotsFired==shots)++cooldownVisits;
  else {++volleys;need(*weapon.state.lastFireMs>=98,"Slope aiming never bypasses initial readiness");
   need(world.projectiles().size()>0&&world.projectiles().back().slopeRadians==expected,"Emitted projectile captures already-updated slope");}
 }
 need(cooldownVisits>90&&volleys>15&&expected==0,"Sustained Spider fire levels out through cooldowns");
 // 42c5a0 scan-only visit does not call42caa0: hold the slope unchanged.
 auto rescan=initial;auto& r=rescan.actors[1];r.weapons[0].state.slopeHeld=true;r.weapons[0].state.slopeRadians=-.25f;
 r.spiderAI->seekerRoute.action.scanRemainingMs=0;
 need(world.restoreSnapshot(rescan,error),error);const auto count=world.shotsFired;world.tick(.014f,{});drain(world);
 need(world.actors()[1].weapons[0].state.slopeHeld&&world.actors()[1].weapons[0].state.slopeRadians==-.25f&&world.shotsFired==count,"Scan-only visit retains slope without emitting");
 // Target-change rescan clears active state without a slope helper visit.
 auto lost=rescan;lost.actors[1].spiderAI->seekerRoute.sampledTarget.x+=1;
 need(world.restoreSnapshot(lost,error),error);world.tick(.014f,{});drain(world);
 need(!world.actors()[1].spiderAI->seekerRoute.action.active&&world.actors()[1].weapons[0].state.slopeRadians==-.25f,"Target-change rescan preserves native retained slope");
 // Original42d2d0 admits range/logical heading before its visible-pose gate.
 auto turning=initial;auto& t=turning.actors[1];t.direction=7;
 auto& turn=*t.spiderAI->turn;turn.bodyPose=turn.bodyPreviousPose=8;turn.bodyDesiredPose=7;turn.bodyRemainingMs=1000;
 t.weapons[0].state.slopeHeld=true;t.weapons[0].state.slopeRadians=-.25f;
 need(world.restoreSnapshot(turning,error),error);const auto before=world.shotsFired;world.tick(.014f,{});drain(world);
 need(world.actors()[1].weapons[0].state.slopeRadians==-.25f+config.slopeStep&&world.shotsFired==before,"Incomplete visible pose advances admitted aim but cannot fire");
 // An actual steering range rejection invokes42c280, unlike a scan-only visit.
 auto shortScene=spiderGatlingFixture();for(auto& n:shortScene.classes[2].properties[0].children)if(n.key=="Fire-Range")n.values={"50"};
 GameWorld shortRange;need(shortRange.load(shortScene,terrain,error),error);auto rejected=admittedSpider(shortRange);
 rejected.actors[1].weapons[0].state.slopeHeld=true;rejected.actors[1].weapons[0].state.slopeRadians=-.25f;
 need(shortRange.restoreSnapshot(rejected,error),error);shortRange.tick(.014f,{});drain(shortRange);
 need(!shortRange.actors()[1].weapons[0].state.slopeHeld&&shortRange.actors()[1].weapons[0].state.slopeRadians==config.initialSlope&&!shortRange.shotsFired,"Rejected aim releases the original slope");
 // 42caa0's non-near branch retains slope for ordinary classes, but resets
 // it for Visible-when-firing classes. Do not turn every steering pass into
 // either an unconditional held input or an unconditional release.
 for(bool hiddenUntilFire:{false,true}){
  auto farScene=spiderGatlingFixture();
  for(auto* properties:{&farScene.classes[1].properties,&farScene.objects[1].properties})
   for(auto& n:*properties)if(n.key=="Visible-when-firing")n.values={hiddenUntilFire?"1":"0"};
  GameWorld farWorld;need(farWorld.load(farScene,terrain,error),error);auto far=admittedSpider(farWorld);
  far.actors[1].brain.minimumDistance=0;far.actors[1].weapons[0].state.slopeHeld=true;far.actors[1].weapons[0].state.slopeRadians=-.25f;
  need(farWorld.restoreSnapshot(far,error),error);farWorld.tick(.014f,{});drain(farWorld);
  const auto& state=farWorld.actors()[1].weapons[0].state;
  need(state.slopeHeld==!hiddenUntilFire&&state.slopeRadians==(hiddenUntilFire?config.initialSlope:-.25f),"Non-near steering preserves original class-dependent slope visit");
 }
 // Real disk save in mid-sweep, then cross multiple scan/cooldown boundaries.
 need(world.restoreSnapshot(initial,error),error);for(int tick=0;tick<30;++tick){world.tick(.014f,{});drain(world);}
 auto saved=world.snapshot();saved.actors[1].spiderAI->seekerRoute.action.scanRemainingMs=28;
 need(world.restoreSnapshot(saved,error),error);
 auto dir=hostTempDirectory("spider-slope");SaveGame save,read;save.currentLevel=scene.mapPath;save.levels[scene.mapPath].world=world.snapshot();
 need(writeSaveFile((dir/"slope.vzs").string(),save,error)&&readSaveFile((dir/"slope.vzs").string(),read,error),error);
 auto replayScene=spiderGatlingFixture();GameWorld replay;
 need(replay.load(replayScene,terrain,error)&&replay.restoreSnapshot(read.levels.at(scene.mapPath).world,error),error);drain(replay);
 for(int tick=0;tick<180;++tick){for(auto* w:{&world,&replay}){w->tick(.014f,{});drain(*w);}need(encoded(world)==encoded(replay),"Spider slope disk continuation across rescan and cooldown");}
 std::filesystem::remove_all(dir);
}
void ordinaryGatlingRegression(){
 const auto terrain=arena();std::string error;
 for(const std::string family:{"Cop","Walker","Burner","Jeep","Roller","Heli","Turret","Tank"}){
  auto scene=fixture(family);auto& gun=scene.classes[2].properties[0].children;
  gun.push_back(value("Initial-slope","-45"));gun.push_back(value("Slope-step","0.5"));
  const auto authored=scene;GameWorld world;need(world.load(scene,terrain,error),error);prepare(world);
  for(int tick=0;tick<2;++tick){world.tick(.014f,{});drain(world);}
  auto initial=world.snapshot();auto& actor=initial.actors[1];
  need(actor.ordinaryAI&&actor.ordinaryAI->targeting.clock.active,"Ordinary route admitted: "+family);
  actor.ordinaryAI->targeting.clock.scanRemainingMs=10000;
  actor.weapons[0].state.slopeHeld=false;
  need(world.restoreSnapshot(initial,error),error);
  const auto config=world.actors()[1].weapons[0].config;
  float expected=config.initialSlope;unsigned cooldowns=0,volleys=0;
  for(unsigned visit=0;visit<120;++visit){
   if(visit)expected=std::min(0.f,expected+config.slopeStep);
   const auto shots=world.shotsFired;world.tick(.014f,{});drain(world);
   const auto& weapon=world.actors()[1].weapons[0];
   need(weapon.state.slopeHeld&&weapon.state.slopeRadians==expected,"Ordinary once-per-steering slope: "+family);
   if(world.shotsFired==shots)++cooldowns;
   else {++volleys;need(world.projectiles().back().slopeRadians==expected,"Ordinary projectile captures slope: "+family);}
  }
  need(expected==0&&cooldowns>90&&volleys>8,"Ordinary sustained aim levels through cooldown: "+family);
  auto rescan=initial;auto& r=rescan.actors[1];r.ordinaryAI->targeting.clock.scanRemainingMs=0;
  r.weapons[0].state.slopeHeld=true;r.weapons[0].state.slopeRadians=-.25f;
  need(world.restoreSnapshot(rescan,error),error);const auto shots=world.shotsFired;world.tick(.014f,{});drain(world);
  need(world.actors()[1].weapons[0].state.slopeHeld&&world.actors()[1].weapons[0].state.slopeRadians==-.25f&&world.shotsFired==shots,"Ordinary scan-only slope retention: "+family);
  auto lost=rescan;lost.actors[1].ordinaryAI->targeting.sampledTarget.x+=1;
  need(world.restoreSnapshot(lost,error),error);world.tick(.014f,{});drain(world);
  need(!world.actors()[1].ordinaryAI->targeting.clock.active&&world.actors()[1].weapons[0].state.slopeHeld&&world.actors()[1].weapons[0].state.slopeRadians==-.25f,"Ordinary target-change scan retains slope: "+family);
  auto shortScene=authored;for(auto& n:shortScene.classes[2].properties[0].children)if(n.key=="Fire-Range")n.values={"50"};
  GameWorld shortRange;need(shortRange.load(shortScene,terrain,error),error);prepare(shortRange);
  for(int t=0;t<2;++t){shortRange.tick(.014f,{});drain(shortRange);}
  auto rejected=shortRange.snapshot();rejected.actors[1].ordinaryAI->targeting.clock.scanRemainingMs=10000;
  rejected.actors[1].weapons[0].state.slopeHeld=true;rejected.actors[1].weapons[0].state.slopeRadians=-.25f;
  need(shortRange.restoreSnapshot(rejected,error),error);shortRange.tick(.014f,{});drain(shortRange);
  need(!shortRange.actors()[1].weapons[0].state.slopeHeld&&shortRange.actors()[1].weapons[0].state.slopeRadians==config.initialSlope,"Ordinary rejected aim releases slope: "+family);
  for(bool hiddenUntilFire:{false,true}){
   auto farScene=authored;
   for(auto* props:{&farScene.classes[1].properties,&farScene.objects[1].properties}){
    for(auto& n:*props){
     if(n.key=="Visible-when-firing")n.values={hiddenUntilFire?"1":"0"};
     if(n.key=="TYPE"&&!n.arguments.empty()&&n.arguments[0]=="Seeker")
      for(auto& v:n.children)if(v.key=="Min-distance")v.values={"0"};
    }
   }
   GameWorld far;need(far.load(farScene,terrain,error),error);prepare(far);
   for(int t=0;t<2;++t){far.tick(.014f,{});drain(far);}
   auto state=far.snapshot();state.actors[1].ordinaryAI->targeting.clock.scanRemainingMs=10000;
   state.actors[1].weapons[0].state.slopeHeld=true;state.actors[1].weapons[0].state.slopeRadians=-.25f;
   need(far.restoreSnapshot(state,error),error);far.tick(.014f,{});drain(far);
   const auto& aim=far.actors()[1].weapons[0].state;
   need(aim.slopeHeld==!hiddenUntilFire&&aim.slopeRadians==(hiddenUntilFire?config.initialSlope:-.25f),"Ordinary non-near class branch: "+family);
  }
  if(family=="Cop"||family=="Turret"){
   need(world.restoreSnapshot(initial,error),error);for(int t=0;t<30;++t){world.tick(.014f,{});drain(world);}
   auto state=world.snapshot();state.actors[1].ordinaryAI->targeting.clock.scanRemainingMs=28;
   need(world.restoreSnapshot(state,error),error);
   const auto dir=hostTempDirectory("ordinary-slope");SaveGame save,read;save.currentLevel=scene.mapPath;save.levels[scene.mapPath].world=world.snapshot();
   need(writeSaveFile((dir/"slope.vzs").string(),save,error)&&readSaveFile((dir/"slope.vzs").string(),read,error),error);
   auto replayScene=authored;GameWorld replay;need(replay.load(replayScene,terrain,error)&&replay.restoreSnapshot(read.levels.at(scene.mapPath).world,error),error);drain(replay);
   for(int t=0;t<180;++t){for(auto* w:{&world,&replay}){w->tick(.014f,{});drain(*w);}need(encoded(world)==encoded(replay),"Ordinary slope disk continuation: "+family);}
   std::filesystem::remove_all(dir);
  }
 }
}

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
 spiderGatlingRegression();
 ordinaryGatlingRegression();
 std::cout<<"PASS"<<families<<"enemy families,54initial admissions,Player,latebirth,Spider/ordinary slope and scan/class/range branches,600disk ticks\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
