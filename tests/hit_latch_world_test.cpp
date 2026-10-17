#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <filesystem>
#include "host_temp.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace vyruz { struct AmmoImpactNativeTestAccess {
    static void health(GameWorld& w,size_t id,bool radial) {
        Projectile source;source.owner=0;source.classId={2};source.nativeOwnerGeneration=w.actors_[0].nativeGeneration;
        w.applyNativeAmmoDamage(id,10,source,radial,true);
    }
    static void contact(GameWorld& w,size_t id,const std::string& hitEffect={}) {
        Projectile source;source.owner=0;source.classId={2};source.damage=0;
        source.nativeOwnerGeneration=w.actors_[0].nativeGeneration;source.hitEffect=hitEffect;
        w.applyAmmoImpact(source,id,true,{},16);
    }
    static void bare(GameWorld& w) {
        Projectile target;target.classId={2};target.owner=0;target.position={200,200};
        w.initializeBareAmmoBody(target);w.projectiles_.push_back(target);w.rebuildBareAmmoIndex();
        w.synchronizeBareAmmoCell(w.projectiles_.back());
        Projectile source;source.owner=0;source.classId={2};source.nativeOwnerGeneration=w.actors_[0].nativeGeneration;
        w.applyNativeBareAmmoDamage(target.nativeBodyIdentity,1,source,true,false);
    }
    static void setFlash(GameWorld& w,float value) { w.actors_[0].hitFlashMs=value; }
    static void protect(GameWorld& w,size_t id) { w.actors_[id].timedProtection.active=true; }
    static void unprotect(GameWorld& w,size_t id) { w.actors_[id].timedProtection.active=false; }
    static void contactHealth(GameWorld& w,size_t id,float amount) { w.applyNativeContactDamage(id,amount); }
    static void queueOrderedContact(GameWorld& w,bool targetFirst) {
        w.awakeManager_.entries=targetFirst?std::vector<uint64_t>{1,0}:std::vector<uint64_t>{0,1};
        w.actors_[0].nativeAwakeIndex=targetFirst?1:0;w.actors_[1].nativeAwakeIndex=targetFirst?0:1;
        w.actors_[1].vitals.energy=5;w.actors_[1].vitals.shield=0;
        // Controlled contact-result injection: amount/geometry were selected
        //upstream. The actual World tick must drain it after the incoming body.
        w.contactDamage_.push_back({1,10,0,false});
    }
}; }
static void need(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
static TextNode value(const char* key,const char* val) {TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={val};return n;}
static TextNode energy() {TextNode n;n.kind=TextKind::Block;n.key="Energy";n.children={value("Initial","1000"),value("Max","1000")};return n;}
static SceneContent fixture() {
    SceneContent s;s.mapPath="TEST/HIT-LATCH.3DG";
    for(unsigned i=0;i<3;++i) {
        ObjectClass cls;cls.id={i};cls.name=i==0?"Player":i==1?"Target":"Ammo";
        cls.behaviours={i==0?"Player":"Fixed"};
        cls.properties={value("Sub-type",i==0?"Vyruz":i==1?"Staticgen":"Ammo"),value("Managed","1"),energy()};
        s.classes.push_back(cls);
        if(i<2){ObjectPlacement o;o.id={i};o.classId={i};o.position={100+float(i)*100,100};o.properties=cls.properties;o.behaviours=cls.behaviours;s.objects.push_back(o);}
    }
    s.player=ObjectId{0};return s;
}
int main() {try {
    Level level;level.mapW=level.mapH=32;level.hasTerrainCollision=true;
    auto scene=fixture();GameWorld world;std::string error;need(world.load(scene,level,error),error.c_str());
    AmmoImpactNativeTestAccess::health(world,1,false);
    need(!world.actors()[1].nativeHit,"direct health alone cannot set Hit");
    AmmoImpactNativeTestAccess::health(world,1,true);
    need(world.actors()[1].nativeHit&&world.actors()[1].hitFlashMs==0,"alive radial target latches without enemy flash");
    world.consumePlayerHitForRender();
    need(world.actors()[1].nativeHit,"rendering player leaves enemy latch intact");
    AmmoImpactNativeTestAccess::health(world,0,false);
    need(!world.player().nativeHit&&world.player().hitFlashMs==0,"player direct health is not Hit producer");
    AmmoImpactNativeTestAccess::contact(world,0);
    need(world.player().nativeHit&&world.player().hitFlashMs==0,"zero-damage selected contact sets pending Hit");
    world.consumePlayerHitForRender(true);
    need(world.player().nativeHit&&world.player().hitFlashMs==0,"animation-only pass does not consume");
    world.consumePlayerHitForRender(false,2);
    need(world.player().nativeHit,"late render pass does not consume");
    world.consumePlayerHitForRender();
    need(!world.player().nativeHit&&world.player().hitFlashMs==500,"actual normal player render consumes and starts flash");
    AmmoImpactNativeTestAccess::setFlash(world,250);world.consumePlayerHitForRender();
    need(world.player().hitFlashMs==250,"repeat rendering does not refresh flash");
    AmmoImpactNativeTestAccess::protect(world,0);AmmoImpactNativeTestAccess::contact(world,0);
    need(!world.player().nativeHit,"Invulnerable contact does not latch");
    AmmoImpactNativeTestAccess::bare(world);
    need(world.projectiles().back().nativeBareBody->nativeHit,"compact Ammo radial target retains Hit");
    AmmoImpactNativeTestAccess::unprotect(world,0);AmmoImpactNativeTestAccess::contact(world,0);
    const auto saved=world.snapshot();
    need(saved.actors[0].nativeHit&&saved.actors[1].nativeHit&&saved.projectiles.back().nativeBareBody->nativeHit,"snapshot retains simulation latches");
    SaveGame game;game.currentLevel=scene.mapPath;game.levels[scene.mapPath].world=saved;
    std::vector<uint8_t> bytes;SaveGame decoded;
    need(encodeSave(game,bytes,error),error.c_str());need(decodeSave(bytes,decoded,error),error.c_str());
    const auto& decodedWorld=decoded.levels.at(scene.mapPath).world;
    need(decodedWorld.actors[0].nativeHit&&decodedWorld.actors[1].nativeHit&&
         decodedWorld.projectiles.back().nativeBareBody->nativeHit,"encoded pending latches survive decoding");
    const auto disk=std::filesystem::temp_directory_path()/
        ("vurus-hit-latch-"+std::to_string(getpid())+".vzs");
    need(writeSaveFile(disk.string(),game,error),error.c_str());SaveGame diskGame;
    need(readSaveFile(disk.string(),diskGame,error),error.c_str());std::filesystem::remove(disk);
    auto resumedScene=fixture();GameWorld resumed;need(resumed.load(resumedScene,level,error),error.c_str());
    need(resumed.restoreSnapshot(diskGame.levels.at(scene.mapPath).world,error),error.c_str());
    need(resumed.player().nativeHit&&resumed.player().hitFlashMs==0&&
         resumed.actors()[1].nativeHit&&resumed.projectiles().back().nativeBareBody->nativeHit,
         "disk restore preserves simulation latches and clears transient flash");
    resumed.consumePlayerHitForRender();
    need(!resumed.player().nativeHit&&resumed.player().hitFlashMs==500&&
         resumed.actors()[1].nativeHit&&resumed.actors()[1].hitFlashMs==0&&
         resumed.projectiles().back().nativeBareBody->nativeHit,
         "disk continuation consumes only player at next actual render");
    AmmoImpactNativeTestAccess::setFlash(resumed,400);resumed.consumePlayerHitForRender();
    need(resumed.player().hitFlashMs==400,"disk repeated render does not refresh consumed Hit");
    auto dormantScene=fixture();
    for(auto& n:dormantScene.classes[1].properties)if(n.key=="Managed")n.values={"0"};
    dormantScene.objects[1].properties=dormantScene.classes[1].properties;
    for(auto& n:dormantScene.classes[2].properties)if(n.key=="Managed")n.values={"0"};
    GameWorld dormant;need(dormant.load(dormantScene,level,error),error.c_str());
    need(!dormant.actors()[1].nativeAwake,"unmanaged class starts asleep");
    AmmoImpactNativeTestAccess::protect(dormant,1);AmmoImpactNativeTestAccess::health(dormant,1,false);
    need(dormant.actors()[1].nativeAwake&&dormant.actors()[1].vitals.energy==1000&&
         !dormant.actors()[1].nativeHit,"health wakes unmanaged target before protection rejection without Hit");
    AmmoImpactNativeTestAccess::bare(dormant);
    need(dormant.projectiles().back().nativeBareBody->awake&&
         dormant.projectiles().back().nativeBareBody->nativeHit,
         "radial health wakes compact unmanaged Ammo and preserves Hit");
    auto rejectedScene=dormantScene;GameWorld rejected;
    need(rejected.load(rejectedScene,level,error),error.c_str());
    AmmoImpactNativeTestAccess::protect(rejected,1);
    AmmoImpactNativeTestAccess::contact(rejected,1,"Target"); //existing class has no TYPE FX.
    need(rejected.actors()[1].nativeAwake&&rejected.actors()[1].vitals.energy==1000&&
         !rejected.actors()[1].nativeHit,
         "selected unmanaged collider wakes even when protection/invalid HitFX reject later phases");
    auto contactScene=dormantScene;GameWorld contactHealth;
    need(contactHealth.load(contactScene,level,error),error.c_str());
    AmmoImpactNativeTestAccess::protect(contactHealth,1);
    AmmoImpactNativeTestAccess::contactHealth(contactHealth,1,10);
    need(contactHealth.actors()[1].nativeAwake&&contactHealth.actors()[1].vitals.energy==1000&&
         !contactHealth.actors()[1].nativeHit,"physical contact health wakes before protection without Hit");
    for(bool targetFirst:{false,true}) {
        auto orderedScene=fixture();GameWorld ordered;
        need(ordered.load(orderedScene,level,error),error.c_str());
        AmmoImpactNativeTestAccess::queueOrderedContact(ordered,targetFirst);
        ordered.tick(.016f,{});
        need(ordered.actors()[1].alive==targetFirst,"physical Energy death waits for target's own ordered visit");
        need(!ordered.actors()[1].nativeHit&&ordered.ammoHits()==0&&ordered.ammoMisses()==0&&
             ordered.player().nativeAmmoOwner.kills==0,"physical contact does not synthesize Hit or Ammo ownership statistics");
        if(targetFirst) {
            need(ordered.actors()[1].nativeDamagePending&&ordered.actors()[1].vitals.energy==-5,
                 "already-visited physical target remains pending at end of tick");
            SaveGame pending;pending.currentLevel=orderedScene.mapPath;
            pending.levels[orderedScene.mapPath].world=ordered.snapshot();
            need(writeSaveFile(disk.string(),pending,error),error.c_str());SaveGame loaded;
            need(readSaveFile(disk.string(),loaded,error),error.c_str());std::filesystem::remove(disk);
            auto continuedScene=fixture();GameWorld continued;
            need(continued.load(continuedScene,level,error)&&
                 continued.restoreSnapshot(loaded.levels.at(orderedScene.mapPath).world,error),error.c_str());
            ordered.tick(.016f,{});continued.tick(.016f,{});
            need(!ordered.actors()[1].alive&&!continued.actors()[1].alive&&
                 ordered.actorsDestroyed==continued.actorsDestroyed,
                 "pending physical Energy death continues at target visit after disk reload");
        }
    }
    std::cout<<"World Hit producers, compact bodies, player-only rendering and pending disk continuation pass\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
