#include "snapshot_fixture.h"
#include "host_temp.h"
#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <array>
#include <cstdlib>
#include <iostream>
using namespace vyruz;
namespace {
using Bytes = std::vector<uint8_t>;
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
void put(Bytes& b, size_t off, uint32_t value) {
    for (int i = 0; i < 4; ++i) b[off + i] = static_cast<uint8_t>(value >> (8 * i));
}
void append(Bytes& b, const Bytes& tail) { b.insert(b.end(), tail.begin(), tail.end()); }
Bytes chunk(const char* tag, Bytes payload, uint32_t children = 0) {
    Bytes result(16);
    for (int i = 0; i < 4; ++i) result[i] = tag[i];
    put(result, 4, payload.size()); put(result, 12, children);
    append(result, payload); return result;
}
Bytes streams(const std::array<Bytes, 4>& components) {
    Bytes result(96);
    for (size_t i = 0; i < components.size(); ++i) {
        put(result, i * 24 + 16, components[i].size());
        append(result, components[i]);
        while (result.size() % 4) result.push_back(0);
    }
    return result;
}
Bytes fixture(uint32_t duration=2) {
    Bytes descriptor(44); put(descriptor, 0, 1); put(descriptor, 4, 3);
    put(descriptor, 8, 1); put(descriptor, 16, static_cast<uint32_t>(-2));
    put(descriptor, 20, 5);
    // Deliberately differ from the usual -origin-size formula: preserve disk values.
    put(descriptor, 28, static_cast<uint32_t>(-7)); put(descriptor, 32, 9);
    Bytes bitmap(76);
    bitmap[0] = 'B'; bitmap[1] = 'M'; bitmap[2] = 'P'; bitmap[3] = '_';
    put(bitmap, 4, 8); put(bitmap, 8, 0x40000322); put(bitmap, 12, 56);
    put(bitmap, 16, 0x40000322); put(bitmap, 20, 1); put(bitmap, 24, 16);
    put(bitmap, 28, 16); put(bitmap, 32, 3); put(bitmap, 36, 1);
    put(bitmap, 40, 8); put(bitmap, 60, 0);
    // RGB555 red, black, magenta, then padding (not a fourth pixel).
    bitmap[68] = 0; bitmap[69] = 0x7c;
    bitmap[72] = 0x1f; bitmap[73] = 0x7c;
    bitmap[74] = 0xaa; bitmap[75] = 0xbb;
    Bytes sequence(24*32);for(int h=0;h<24;++h){put(sequence,h*32,8);put(sequence,h*32+8,1);}
    Bytes poses(24*8*28);for(int i=0;i<24*8;++i){put(poses,i*28,1);put(poses,i*28+24,duration);}
    Bytes headG(36), headA(36); put(headG, 0, 1); put(headA, 0, 24);
    Bytes head; append(head, chunk("GOB_", headG)); append(head, chunk("ANM_", headA));
    Bytes data; append(data, chunk("GOB_", streams({descriptor, bitmap, {}, {}})));
    append(data, chunk("ANM_", streams({sequence, {}, poses, {}})));
    Bytes root; append(root, chunk("HEAD", head, 2)); append(root, chunk("DATA", data, 2));
    return chunk("GAF_", root, 2);
}

TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
TextNode block(std::string k,std::vector<TextNode> c,std::string t={}){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=c;if(!t.empty())n.arguments={t};return n;}
SceneContent content() {
 SceneContent s;s.mapPath="TEST/BODY.3DG";
 ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};p.properties={block("Energy",{value("Initial","10000")})};
 ObjectClass t;t.id={1};t.name="Thing";t.sprite={"TEST/BODY.GAF",{1}};
 t.properties={value("Sub-type","Thing"),value("Graphic-update-needed","0"),block("Energy",{value("Initial","10000")})};
 s.classes={p,t};s.player=ObjectId{0};
 for(uint32_t i=0;i<3;++i){ObjectPlacement o;o.id={i};o.classId={i?1u:0u};o.position={100.f+100*i,100};o.sprite=s.classes[o.classId.value].sprite;o.properties=s.classes[o.classId.value].properties;o.behaviours=s.classes[o.classId.value].behaviours;s.objects.push_back(o);}
 return s;
}
bool equalClocks(const GameWorld& a,const GameWorld& b){for(size_t i=1;i<3;++i)for(size_t h=0;h<24;++h){auto x=a.actors()[i].bodyGraphics->slots[h].animation,y=b.actors()[i].bodyGraphics->slots[h].animation;if(x.cursor!=y.cursor||x.flags!=y.flags||x.remaining!=y.remaining)return false;}return true;}
}
void projectileControls(bool finalActive,bool hidden=false,bool transitions=false) {
 auto scene=content();scene.objects.resize(1);scene.classes.resize(2);
 auto& ammo=scene.classes[1];ammo.name="Test-Ammo";ammo.behaviours={"Straight"};
 ammo.properties={value("Managed","1"),value("Sub-type","Ammo"),value("Graphic-update-needed","1"),value("Bounding-box-size","1"),value("Elasticity","0.6"),block("Energy",{value("Initial","1000")}),block("TYPE",{value("On-hit","0"),value("On-object-hit","1")},"Ammo")};
 if(!finalActive)ammo.properties.push_back(block("TYPE",{value("Damage","10"),value("Damage-zone","30"),value("Boom-fx","After")},"Explosion"));
 ammo.flags.invisible=hidden;
 ObjectClass after;after.id={2};after.name="After";after.sprite={"TEST/AFTER.GAF",{1}};after.properties={block("TYPE",{},"FX")};scene.classes.push_back(after);
 Level level;level.mapW=level.mapH=100;level.hasTerrainCollision=true;
 level.nativeNavigationDecor.push_back({1015,900,1015,1100,0,0,1,false,false,false});
 const auto bytes=fixture();ContentReader read=[&](const std::string&,Bytes& out){out=bytes;return true;};GameWorld world;world.setEffectReader(read);std::string error;
 check(vyruz_test::loadManagedFixture(world,scene,level,error),error.c_str());
 Projectile shot;shot.classId={1};shot.owner=0;const auto pos=spiderNativeToSurface(1000,1000);shot.position={pos.x,pos.y};
 shot.radius=1;shot.ageMs=1000;shot.lifetimeMs=10000;shot.directionX=1;shot.directionZ=0;
 shot.motionConfig.inertia=true;shot.motionConfig.launched=true;shot.motionConfig.frictionX=shot.motionConfig.frictionZ=1;
 shot.motion.thrust=20;shot.motion.velocityX=20;shot.motion.magnitude=20;shot.visual=world.spawn({1},shot.position);
 const auto visual=*shot.visual;auto state=world.snapshot();state.actors[visual].direction=6;state.objects[visual].owner=ObjectId{0};state.actors[visual].nativeCreatorGeneration=state.actors[0].nativeGeneration;state.actors[0].nativeAmmoOwner.activeAmmo=1;
 GafAnimation metadata;check(decodeGafAnimation(bytes,metadata,GafDecodeMode::Metadata),"fixture metadata decode");
 for(uint32_t h=0;h<24;++h)if(hidden || (finalActive?h==6:h!=6)){AnimationCursor stop;stop.holdLast(metadata.sequences[h]);state.actors[visual].bodyGraphics->slots[h].animation=stop.capture();}
 if(transitions){shot.phaseInitialized=true;shot.inactiveMs=1;shot.phaseHidden=true;}
 vyruz_test::appendProjectile(state,scene,shot);check(world.restoreSnapshot(state,error),error.c_str());
 const auto before=world.actorsDestroyed;world.tick(.014f,{});
 if(finalActive || (hidden && !transitions)) {
   check(!world.projectiles().empty() && world.actors()[visual].alive,"final active heading / initial hidden must survive");
   if(!hidden || transitions)check(world.actors()[visual].direction!=6,"actual terrain bounce changes heading before retirement");
 } else {
   check(world.projectiles().empty() && !world.actors()[visual].alive,"final stopped heading retires visual projectile");
   check(world.actorsDestroyed==before+1,"retirement invokes full destroy exactly once");
   check(world.actors()[visual].nativeGeneration==0 && world.actors()[visual].nativePoolSlot==SIZE_MAX && !world.actors()[visual].nativeAwake,"retirement releases manager/pool identity");
   size_t explosions=0;for(size_t i=0;i<world.actors().size();++i)if(const auto& actor=world.actors()[i];actor.explosion){++explosions;check(actor.explosion->creator==0 && scene.objects[i].owner==ObjectId{0},"Explosion preserves source creator ownership");}
   check(explosions==1,"retirement full destroy transfers Explosion to FX, no immediate radial shortcut");
   check(world.actors()[0].nativeAmmoOwner.activeAmmo==0,"full release decrements validated owner once");
 }
 world.discardBodyGraphicsAudio();
}
void activationControls(int mode) {
 auto scene=content();scene.objects.resize(1);scene.classes.resize(2);auto& ammo=scene.classes[1];ammo.name="Test-Ammo";ammo.behaviours={"Straight"};
 ammo.properties={value("Managed","1"),value("Sub-type","Ammo"),value("Graphic-update-needed","1"),block("Energy",{value("Initial","1000")}),block("TYPE",{},"Ammo")};ammo.flags.invisible=true;
 Level level;level.mapW=level.mapH=100;GameWorld world;const auto bytes=fixture();world.setEffectReader([&](const auto&,Bytes& out){out=bytes;return true;});std::string error;
 check(vyruz_test::loadManagedFixture(world,scene,level,error),error.c_str());Projectile shot;shot.classId={1};shot.owner=0;shot.position={600,600};shot.lifetimeMs=10000;shot.visual=world.spawn({1},shot.position);shot.phaseInitialized=true;
 if(mode==0)shot.inactiveMs=100;else if(mode==1)shot.onMs=1;else if(mode==2){shot.inactiveMs=1;shot.phaseHidden=true;}
 auto state=world.snapshot();const auto id=*shot.visual;const auto before=state.actors[id].bodyGraphics->slots[0].animation;vyruz_test::appendProjectile(state,scene,shot);check(world.restoreSnapshot(state,error),error.c_str());world.tick(.014f,{});
 const auto after=world.actors()[id].bodyGraphics->slots[0].animation;
 if(mode<2)check(before.cursor==after.cursor && before.remaining==after.remaining,"inactive/on-expiry visit must not advance graphics");
 else check(after.remaining==before.remaining-14,"admitted activation/initial-hidden visit advances graphics");
 check(world.actors()[id].bodyGraphicsHidden==(mode!=2),"runtime hidden only changes on activation transitions");world.discardBodyGraphicsAudio();
}
void arrayAndUnknownControls() {
 const auto data=fixture();Level level;level.mapW=level.mapH=100;std::string error;
 for(int mode=0;mode<3;++mode) {
  auto scene=content();scene.objects.resize(2);auto& o=scene.objects[1];
  if(mode==0){o.sprite.path.clear();o.shadow={"TEST/SHADOW.GAF",{1}};}
  if(mode==1){o.sprite.sequence={0};o.shadow.sequence={1};}
  if(mode==2){o.sprite.sequence={0};o.shadow.sequence={0};}
  GameWorld world;world.setEffectReader([&](const auto&,Bytes& out){out=data;return true;});check(vyruz_test::loadManagedFixture(world,scene,level,error),error.c_str());
  const auto& state=*world.actors()[1].bodyGraphics;
  check(state.binding==(mode==2?NativeBodyGraphicsBinding::MissingArray:NativeBodyGraphicsBinding::Present),"independent constructor bank/index fences");
  check(state.baseSequence==0 && state.slots[0].sequence==0,"shadow/cross-pair leaves primary ANMs unbound");check(world.restoreSnapshot(world.snapshot(),error),error.c_str());
  if(mode==2){world.tick(.014f,{});check(!world.actors()[1].alive,"configured missing array retires at native common tail");}
 }
 for(bool visual:{false,true}) {
  auto scene=content();scene.objects.resize(1);scene.classes.resize(2);auto& ammo=scene.classes[1];ammo.name="Test-Ammo";ammo.properties={value("Managed","1"),value("Sub-type","Ammo"),value("Graphic-update-needed","1"),block("Energy",{value("Initial","1000")}),block("TYPE",{},"Ammo")};
  GameWorld world;check(vyruz_test::loadManagedFixture(world,scene,level,error),error.c_str());Projectile shot;shot.classId={1};shot.owner=0;shot.position={600,600};shot.lifetimeMs=10000;if(visual)shot.visual=world.spawn({1},shot.position);
  auto state=world.snapshot();vyruz_test::appendProjectile(state,scene,shot);check(world.restoreSnapshot(state,error),error.c_str());world.tick(.014f,{});
  check(world.projectiles().size()==1,"Unknown visual/missing renderer never retires compact bare Ammo");
  if(visual)check(world.actors()[*shot.visual].bodyGraphics->binding==NativeBodyGraphicsBinding::Unknown,"unavailable metadata stays explicitly Unknown");
  else check(world.actors().size()==1 && world.projectiles()[0].nativeBareBody.has_value(),"ordinary bare Ammo remains compact");
 }
}
void invalidMetadataControls() {
 const auto valid=fixture();Level level;level.mapW=level.mapH=100;std::string error;
 ContentReader read=[&](const std::string& path,Bytes& out){out=path=="TEST/INVALID.GAF"?Bytes{1,2,3}:valid;return true;};
 auto scene=content();GameWorld world;world.setEffectReader(read);check(vyruz_test::loadManagedFixture(world,scene,level,error),error.c_str());
 auto bad=world.snapshot();bad.objects[1].sprite.path="TEST/INVALID.GAF";
 check(!world.restoreSnapshot(bad,error) && !error.empty(),"malformed bank fails bool restore with error, no exception escape");
 check(world.snapshot().objects[1].sprite.path=="TEST/BODY.GAF","resource validation failure leaves active world unchanged");
 auto missing=world.snapshot();missing.actors[1].bodyGraphics.reset();check(!world.restoreSnapshot(missing,error),"new nonlegacy body requires saved history");
 for(bool malformed:{false,true}) {
  auto invalid=content();invalid.objects[1].sprite=malformed?SpriteReference{"TEST/INVALID.GAF",{1}}:SpriteReference{"TEST/BODY.GAF",{25}};
  GameWorld candidate;candidate.setEffectReader(read);check(!vyruz_test::loadManagedFixture(candidate,invalid,level,error) && !error.empty(),"invalid bank/sequence rejected through bool load boundary");
 }
 auto childScene=content();childScene.objects.resize(1);childScene.classes[1].behaviours={"Fixed"};childScene.classes[1].sprite.path="TEST/INVALID.GAF";
 childScene.classes[0].properties.push_back(block("CREATE",{value("Name","Thing"),value("Delay","0")}));childScene.objects[0].properties=childScene.classes[0].properties;
 GameWorld childWorld;childWorld.setEffectReader(read);check(!vyruz_test::loadManagedFixture(childWorld,childScene,level,error) && !error.empty(),"recursive CREATE invalid metadata rejected through bool load boundary");
 auto corrupt=world.snapshot();corrupt.actors[1].bodyGraphics->binding=NativeBodyGraphicsBinding::Unknown;corrupt.actors[1].bodyGraphics->baseSequence=0;
 SaveGame save;save.currentLevel=scene.mapPath;save.levels[scene.mapPath].world=corrupt;Bytes output{42};check(!encodeSave(save,output,error) && output==Bytes{42},"unknown with noncanonical clocks rejected transactionally by schema");
}
void recycledTrailControls(bool graphicNeeded,bool visible,bool graphicsEnabled=true) {
 auto scene=content();scene.objects.resize(1);scene.objects[0].position={3000,1600};
 auto& trail=scene.classes[1];trail.name="Trail";trail.behaviours={"Fixed"};
 trail.properties={value("Managed","1"),value("Sub-type","Thing"),value("Graphic-update-needed",graphicNeeded?"1":"0"),value("Bounding-box-size","0"),value("Speed-max","0"),value("Life-time","42"),block("Energy",{value("Initial","1000")})};
 ObjectClass ammo;ammo.id={2};ammo.name="Bullet";ammo.behaviours={"Straight"};ammo.sprite={"TEST/AMMO.GAF",{1}};
 ammo.properties={value("Managed","1"),value("Sub-type","Ammo"),value("Bounding-box-size","0"),value("Speed-max","0"),value("Life-time","5000"),block("Energy",{value("Initial","1000")}),block("TYPE",{value("Ignore-owner","1"),value("Damage-after","5000")},"Ammo"),block("CREATE",{value("Name","Trail"),value("Delay","14"),value("Repeat-timeout","14"),value("At-current-position","1")})};
 ObjectClass gun;gun.id={3};gun.name="Gun";gun.properties={value("Managed","1"),block("TYPE",{value("Ammo","Bullet"),value("Delay","1000"),value("Start-distance","200"),value("Fire-altitude","24")},"Weapon")};
 scene.classes.push_back(ammo);scene.classes.push_back(gun);
 auto& player=scene.classes[0];player.properties.push_back(value("Managed","1"));
 player.properties.push_back(block("ADD",{value("Name","Gun")},"Weapon"));
 player.properties.push_back(block("ADD",{value("Name","Bullet"),value("Count","10"),value("Max","10")},"Ammo"));
 scene.objects[0].properties=player.properties;
 Level level;level.mapW=level.mapH=100;level.hasTerrainCollision=true;
 const auto bytes=fixture(),ammoBytes=fixture(1000);ContentReader read=[&](const std::string& path,Bytes& out){out=path=="TEST/AMMO.GAF"?ammoBytes:bytes;return true;};
 GameWorld world;if(graphicsEnabled)world.setEffectReader(read);std::string error;
 check(world.load(scene,level,error),error.c_str());const auto authored=scene;
 world.setDebugEnemyAI(false);world.setPlayerDirection(9);
 const BodyGraphicsView view=visible?BodyGraphicsView{-2500,-1200,1000,800}:BodyGraphicsView{0,0,640,480};
 const auto step=[&](GameWorld& w,bool fire=false){WorldInput in;in.fire=fire;w.tick(.014f,in);w.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::VisibleUpdate);w.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::Draw);w.takeAudioEvents();w.takeBodyGraphicsAudio();w.releaseBodyGraphicsAudio();};
 std::vector<uint64_t> revisions,identities;unsigned reuses=0;
 const auto inspect=[&]{
  revisions.resize(world.actors().size());identities.resize(world.actors().size());
  for(size_t id=0;id<world.actors().size();++id){const auto& a=world.actors()[id];
   if(!a.alive||scene.objects[id].classId.value!=1)continue;
   check(a.bodyGraphics.has_value(),"recycled trail owns mandatory body graphics before any camera visit");
   check(a.bodyGraphicsGraphicUpdate==graphicNeeded,"recycled trail restores authored graphics phase flag");
   const auto& state=*a.bodyGraphics;
   check(state.kind==NativeBodyGraphicsKind::Thing,"recycled trail keeps Thing graphics kind");
   check(state.binding==(graphicsEnabled?NativeBodyGraphicsBinding::Present:NativeBodyGraphicsBinding::Unknown),"recycled graphics binding follows reader availability");
   if(revisions[id]&&revisions[id]!=a.visualRevision){
    ++reuses;
    if(graphicsEnabled){
     check(state.slots[0].audioIdentity!=identities[id],"recycled birth allocates fresh audio identities");
     for(size_t h=1;h<24;++h)check(state.slots[h].sequence==1&&state.slots[h].animation.cursor==1&&state.slots[h].animation.remaining==28,"recycled Thing rebinds inactive headings to initial authored clocks");
    }
   }
   revisions[id]=a.visualRevision;identities[id]=state.slots[0].audioIdentity;
  }
 };
 for(int n=0;n<40;++n){step(world,n==0);inspect();}
 check(world.projectiles().size()==1&&reuses>=10,"real projectile CREATE repeatedly reuses expired trail slots");
 // Exercise real file encoding plus a separately loaded World, then keep
 // creating/recycling offscreen bodies across the checkpoint boundary.
 const auto directory=hostTempDirectory("trail-graphics");SaveGame save,decoded;
 save.currentLevel=scene.mapPath;save.levels[scene.mapPath].world=world.snapshot();
 check(writeSaveFile((directory/"trail.vzs").string(),save,error),error.c_str());
 check(readSaveFile((directory/"trail.vzs").string(),decoded,error),error.c_str());
 auto replayScene=authored;GameWorld replay;if(graphicsEnabled)replay.setEffectReader(read);
 check(replay.load(replayScene,level,error)&&replay.restoreSnapshot(decoded.levels.at(scene.mapPath).world,error),error.c_str());replay.setDebugEnemyAI(false);
 const auto encode=[&](const GameWorld& w){SaveGame v;v.currentLevel=scene.mapPath;v.levels[scene.mapPath].world=w.snapshot();Bytes out;check(encodeSave(v,out,error),error.c_str());return out;};
 for(int n=0;n<120;++n){step(world);step(replay);inspect();check(encode(world)==encode(replay),"recycled trail disk continuation retains full World state");}
 check(reuses>=50,"disk continuation includes repeated recycling");
 std::filesystem::remove_all(directory);
}

int main(){
 {
  auto headless=content();headless.classes[1].sprite={};
  for(size_t i=1;i<headless.objects.size();++i)headless.objects[i].sprite={};
  GameWorld geometry;Level arena;arena.mapW=arena.mapH=64;std::string error;
  check(vyruz_test::loadManagedFixture(geometry,headless,arena,error),error.c_str());
  check(geometry.actors()[1].bodyGraphics->binding==NativeBodyGraphicsBinding::Unknown,"graphics-disabled World has unknown array binding");
  geometry.tick(.014f,{});
  check(geometry.actors()[1].alive && geometry.actors()[2].alive,"headless stationary bodies are not retired as missing graphics");
 }
 for(bool needed:{false,true})for(bool visible:{false,true})recycledTrailControls(needed,visible);
 recycledTrailControls(true,false,false);
 invalidMetadataControls();arrayAndUnknownControls();
 for(int mode=0;mode<4;++mode)activationControls(mode);
 projectileControls(true);projectileControls(false);projectileControls(false,true);projectileControls(false,true,true);
 auto scene=content();Level level;level.mapW=level.mapH=64;std::string error;GameWorld world;
 unsigned reads=0;const auto bytes=fixture();ContentReader read=[&](const std::string&,Bytes& out){++reads;out=bytes;return true;};world.setEffectReader(read);
 check(vyruz_test::loadManagedFixture(world,scene,level,error),error.c_str());
 check(world.actors()[1].bodyGraphics && world.actors()[2].bodyGraphics,"placed Thing owns slots");check(reads==1,"whole metadata bank decoded once for two bodies");
 const auto initial=world.actors()[1].bodyGraphics->slots[0].animation;
 world.tick(.014f,{});BodyGraphicsView view{0,0,640,480};
 world.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::VisibleUpdate);world.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::Draw);
 check(world.actors()[1].bodyGraphics->slots[0].animation.cursor==2,"nonGraphic body receives two visible phases");
 check(world.actors()[1].bodyGraphics->slots[1].animation.remaining==initial.remaining,"inactive Thing heading paused");
 const auto clock=world.actors()[1].bodyGraphics->slots[0].animation;
 view.suspended=true;world.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::Draw);
 check(world.actors()[1].bodyGraphics->slots[0].animation.remaining==clock.remaining,"modal adapter freezes clock");view.suspended=false;
 world.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::Draw);
 check(world.actors()[1].bodyGraphics->slots[0].animation.remaining==14,"active redraw reuses stored World integer elapsed");
 SaveGame save;save.currentLevel=scene.mapPath;save.levels[save.currentLevel].world=world.snapshot();Bytes encoded;SaveGame decoded;
 check(encodeSave(save,encoded,error)&&decodeSave(encoded,decoded,error),error.c_str());
 auto other=content();GameWorld resumed;resumed.setEffectReader(read);check(vyruz_test::loadManagedFixture(resumed,other,level,error),error.c_str());
 check(resumed.restoreSnapshot(decoded.levels.at(scene.mapPath).world,error),error.c_str());check(equalClocks(world,resumed),"disk retains all24 clocks");
 world.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::Draw);resumed.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::Draw);
 check(equalClocks(world,resumed),"immediate postload draw retains101c delta");
 for(int n=0;n<4;++n){world.tick(.014f,{});resumed.tick(.014f,{});world.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::VisibleUpdate);resumed.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::VisibleUpdate);check(equalClocks(world,resumed),"deterministic disk continuation");}
 auto bad=world.snapshot();bad.actors[1].bodyGraphics->slots[2].animation.remaining=-1;
 check(!world.restoreSnapshot(bad,error),"invalid new body clock rejected");check(equalClocks(world,resumed),"failed restore transactional");
 auto legacy=world.snapshot();legacy.actors[1].bodyGraphics.reset();legacy.actors[1].bodyGraphicsLegacy=true;
 check(world.restoreSnapshot(legacy,error),error.c_str());world.advanceBodyGraphicsPhase(view,BodyGraphicsPhase::Draw);
 check(!world.actors()[1].bodyGraphics && world.actors()[1].bodyGraphicsLegacy,"explicit legacy policy does not invent clocks");
 std::cout<<"private body World integration controls passed\n";
}
