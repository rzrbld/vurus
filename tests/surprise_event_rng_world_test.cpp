#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/event_random_native.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace vyruz {struct AmmoImpactNativeTestAccess {
    static void apply(GameWorld& world){world.applySurprise();}
    static void destroy(GameWorld& world,size_t actor){world.destroy(actor,world.player_);}
    static void damage(GameWorld& world,float amount,bool radial) {
        world.surprise_.shakeOffset={1.5f,2.5f};Projectile source;source.owner=SIZE_MAX;
        world.applyNativeAmmoDamage(world.player_,amount,source,radial,false,false);
    }
    static uint64_t addBare(GameWorld& world,ClassId cls,ContentPosition position) {
        Projectile shot;shot.classId=cls;shot.owner=world.player_;shot.position=position;
        shot.range=10000;shot.lifetimeMs=10000;shot.radius=0;
        world.initializeBareAmmoBody(shot);world.projectiles_.push_back(shot);
        world.synchronizeBareAmmoCell(world.projectiles_.back());world.rebuildBareAmmoIndex();
        return shot.nativeBodyIdentity;
    }
};}
namespace {
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
TextNode value(std::string key,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(v)};return n;}
TextNode block(std::string key,std::vector<TextNode> children){TextNode n;n.kind=TextKind::Block;n.key=std::move(key);n.children=std::move(children);return n;}
SceneContent fixture(){
    SceneContent scene;scene.mapPath="TEST/SURPRISE-EVENT-RNG.3DG";
    ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Sub-type","Vyruz"),value("Managed","1"),value("Bounding-box-size","20"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("Shield",{value("Initial","400"),value("Max","400")})};
    ObjectClass victim;victim.id={1};victim.name="Victim";victim.behaviours={"Fixed"};victim.properties={value("Sub-type","Thing"),value("Managed","0"),value("Bounding-box-size","5"),block("Energy",{value("Initial","100"),value("Max","100")})};
    ObjectClass effect;effect.id={2};effect.name="Explosion";effect.behaviours={"Fixed"};effect.properties={value("Managed","1"),value("Bounding-box-size","0"),block("CREATE",{value("Name","Final-Explosion-Part"),value("Delay","0"),value("Random-dir","1")})};
    ObjectClass shock=effect;shock.id={3};shock.name="Shock-Wave";shock.properties={value("Managed","1"),value("Bounding-box-size","0")};
    ObjectClass product=shock;product.id={4};product.name="Final-Explosion-Part";product.behaviours={"Straight"};product.properties={value("Sub-type","Thing"),value("Managed","1"),value("Bounding-box-size","0"),value("Launched","1"),value("Use-inertia","1"),value("Life-time","1000"),block("Thrust",{value("Initial","90"),value("Max","90")})};
    scene.classes={player,victim,effect,shock,product};
    for(unsigned id=0;id<2;++id){ObjectPlacement p;p.id={id};p.classId={id};p.position={1000.f+id*40,1000};p.properties=scene.classes[id].properties;p.behaviours=scene.classes[id].behaviours;scene.objects.push_back(p);}scene.player=ObjectId{0};return scene;
}
Level arena(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);return l;}
void load(GameWorld& world,SceneContent& scene,const Level& level){std::string error;need(world.load(scene,level,error),error.c_str());world.setDebugEnemyAI(false);}
bool same(NativeRandomState a,NativeRandomState b){return a.seed==b.seed&&a.counter==b.counter;}
WorldSnapshot disk(const GameWorld& world){SaveGame save;save.currentLevel="TEST/SURPRISE-EVENT-RNG.3DG";save.levels[save.currentLevel].world=world.snapshot();std::vector<uint8_t> bytes;std::string error;need(encodeSave(save,bytes,error),error.c_str());SaveGame copy;need(decodeSave(bytes,copy,error),error.c_str());return copy.levels.at(save.currentLevel).world;}
void randomSurprise(){
    for(unsigned flags:{0u,0x8000u,0x10000u,0x18000u}){
        auto scene=fixture();const auto level=arena();GameWorld world;load(world,scene,level);
        NativeEffectCamera camera;camera.modeFlags=flags;world.setEffectCamera(camera);
        const auto before=world.snapshot();auto expected=(flags&0x18000)?before.nativeReplayRandom:before.nativeGlobalRandom;
        const auto kind=nativeRandomSurprise(expected);need(world.debugSetSurprise(SurpriseKind::Random),"random surprise accepted");
        const auto after=world.snapshot();need(world.surprise().kind==kind,"native weighted surprise choice");
        need(same((flags&0x18000)?after.nativeReplayRandom:after.nativeGlobalRandom,expected),"selected RNG context advanced once");
        need(same((flags&0x18000)?after.nativeGlobalRandom:after.nativeReplayRandom,(flags&0x18000)?before.nativeGlobalRandom:before.nativeReplayRandom),"outer RNG context retained");
        need(same(after.nativeWorldRandom,before.nativeWorldRandom)&&after.randomState==before.randomState,"Random Surprise isolates World and MT streams");
        auto checkpoint=disk(world);auto resumedScene=fixture();GameWorld resumed;load(resumed,resumedScene,level);std::string error;need(resumed.restoreSnapshot(checkpoint,error),error.c_str());resumed.setEffectCamera(camera);
        const auto next=nativeRandomSurprise(expected);need(resumed.debugSetSurprise(SurpriseKind::Random),"resumed random accepted");need(resumed.surprise().kind==next,"second native surprise after disk continuation");
        const auto resumedSnapshot=resumed.snapshot();need(same((flags&0x18000)?resumedSnapshot.nativeReplayRandom:resumedSnapshot.nativeGlobalRandom,expected),"native surprise stream disk continuation");
    }
}
void shakeOrdering(){
    for(const auto kind:{SurpriseKind::Trap,SurpriseKind::Destroy}){
        auto scene=fixture();const auto level=arena();GameWorld world;load(world,scene,level);const auto before=world.snapshot();auto global=before.nativeGlobalRandom;auto outer=before.nativeWorldRandom;
        need(world.debugSetSurprise(kind),"explicit surprise accepted");AmmoImpactNativeTestAccess::apply(world);
        const auto expected=nativeRandomShake(global,kind==SurpriseKind::Trap?16:32);const auto after=world.snapshot();
        need(world.surprise().shakeOffset.x==expected[0]&&world.surprise().shakeOffset.y==expected[1],"native global two-axis shake");
        need(same(after.nativeGlobalRandom,global)&&after.randomState==before.randomState,"shake consumes exactly native global draws and no MT");
        if(kind==SurpriseKind::Trap){
            const int heading=outer.inclusive(0,23);need(scene.objects.size()==5,"Trap effects and immediate child created");
            need(scene.objects[2].classId.value==2&&scene.objects[3].classId.value==4&&scene.objects[4].classId.value==3,"Trap original effect/recursive child construction order");
            need(world.actors()[3].direction==heading&&same(after.nativeWorldRandom,outer),"Trap child uses isolated native World direction stream");
            need(world.player().vitals.energy==750&&world.player().vitals.shield==300,"Trap native75-percent vitals");
        }else{
            need(world.actors()[1].alive&&world.actors()[1].vitals.energy==0&&world.actors()[1].nativeDamagePending&&world.actors()[1].nativeAwake,"Destroy marks victim and defers death to own visit");
            need(scene.objects.size()==2&&same(after.nativeWorldRandom,outer),"Destroy marking has no inline death producer");
        }
    }
}
void radialShake(){
    for(float amount:{199.99f,200.f,299.99f,300.f,2399.99f,2400.f,10000.f})
    for(bool radial:{false,true})for(bool protectedPlayer:{false,true}) {
        auto scene=fixture();const auto level=arena();GameWorld world;load(world,scene,level);
        world.setDebugGodMode(protectedPlayer);const auto before=world.snapshot();
        auto outer=before.nativeWorldRandom;std::array<int,2> expected{};
        const bool draws=radial&&amount>=200;
        if(draws)expected=nativeRadialDamageShake(outer,amount,true);
        AmmoImpactNativeTestAccess::damage(world,amount,radial);const auto after=world.snapshot();
        if(draws)need(world.surprise().shakeOffset.x==expected[0]&&world.surprise().shakeOffset.y==expected[1],"admitted player radial shake matches native damage amplitude");
        else need(world.surprise().shakeOffset.x==1.5f&&world.surprise().shakeOffset.y==2.5f,"direct/subthreshold damage preserves fractional shake offsets");
        need(same(after.nativeWorldRandom,outer)&&same(after.nativeGlobalRandom,before.nativeGlobalRandom)&&after.randomState==before.randomState,"radial two-axis draws consume only World context");
        if(protectedPlayer)need(world.player().vitals.energy==1000&&world.player().vitals.shield==400,"radial shake remains independent of health-applied protection");
        const auto checkpoint=disk(world);need(same(checkpoint.nativeWorldRandom,outer),"radial shake World stream persists");
    }
}

void deathProducerOrder(){
    for(unsigned flags:{0u,0x10000u}) {
        auto scene=fixture();
        ObjectClass drop=scene.classes[2];drop.id={5};drop.name="Drop";scene.classes.push_back(drop);
        const auto random=[&](const char* numerator,const char* denominator){TextNode n;n.kind=TextKind::Assignment;n.key="RANDOM";n.arguments={"Drop",numerator,denominator};return n;};
        scene.objects[1].properties.push_back(random("1","2"));
        scene.objects[1].properties.push_back(random("10","1"));
        scene.objects[1].properties.push_back(random("0","5"));
        const auto blueprint=scene;const auto level=arena();GameWorld world;load(world,scene,level);
        NativeEffectCamera camera;camera.modeFlags=flags;world.setEffectCamera(camera);
        const auto before=world.snapshot();auto global=before.nativeGlobalRandom;auto outer=before.nativeWorldRandom;
        unsigned drops=0;std::vector<int> headings;
        if(!flags)for(const auto bounds:{std::pair{0,5},std::pair{10,1},std::pair{1,2}})
            if(nativeDeathRandomDrop(global,bounds.first,bounds.second)){++drops;headings.push_back(outer.inclusive(0,23));}
        AmmoImpactNativeTestAccess::destroy(world,1);const auto after=world.snapshot();
        need(same(after.nativeGlobalRandom,global)&&same(after.nativeWorldRandom,outer),"death RANDOM rolls interleave with child World context and restore global");
        need(after.randomState==before.randomState,"death RANDOM no longer consumes MT");
        unsigned actualDrops=0,children=0;
        for(size_t id=0;id<scene.objects.size();++id){if(scene.objects[id].classId.value==5&&world.actors()[id].alive)++actualDrops;if(scene.objects[id].classId.value==4&&world.actors()[id].alive){need(children<headings.size()&&world.actors()[id].direction==headings[children],"each selected drop constructs child before next death roll");++children;}}
        need(actualDrops==drops&&children==drops,"normal/demo death RANDOM factory count");
        const auto checkpoint=disk(world);auto resumedScene=blueprint;GameWorld resumed;load(resumed,resumedScene,level);std::string error;need(resumed.restoreSnapshot(checkpoint,error),error.c_str());
        need(same(resumed.snapshot().nativeGlobalRandom,global)&&same(resumed.snapshot().nativeWorldRandom,outer),"combined death/CREATE streams persist");
    }
}

void mixedDestroyOrder(){
    auto scene=fixture();scene.objects[1].position={1040,1000};
    ObjectClass bare;bare.id={5};bare.name="Bare-Ammo";bare.behaviours={"Straight"};
    bare.properties={value("Sub-type","Ammo"),value("Managed","0"),value("Bounding-box-size","0"),block("Energy",{value("Initial","100"),value("Max","100")})};
    scene.classes.push_back(bare);const auto level=arena();GameWorld world;load(world,scene,level);
    const auto identity=AmmoImpactNativeTestAccess::addBare(world,{5},{1040,1000});
    const auto lastActor=world.spawn({1},{1040,1000});
    const auto earlierY=world.spawn({1},{1100,968});
    const auto earlierX=world.spawn({1},{1000,1000});
    const auto outside=world.spawn({1},{1000,1224});
    const auto before=world.snapshot();need(!world.actors()[1].nativeAwake&&!world.actors()[lastActor].nativeAwake&&!world.projectiles()[0].nativeBareBody->awake,"unmanaged Destroy recipients initially sleep");
    need(world.debugSetSurprise(SurpriseKind::Destroy),"mixed Destroy accepted");AmmoImpactNativeTestAccess::apply(world);
    const auto after=world.snapshot();need(after.nativeAwake.has_value(),"mixed awake order retained");
    const auto& entries=after.nativeAwake->manager.entries;
    need(entries.size()>=5&&entries[entries.size()-5]==earlierY&&entries[entries.size()-4]==earlierX&&entries[entries.size()-3]==lastActor&&entries[entries.size()-2]==(NativeAwakeAmmoBit|identity)&&entries.back()==1,"Destroy follows Y/X/mixed prepend order when waking sleeping bodies");
    need(world.actors()[outside].vitals.energy==100&&!world.actors()[outside].nativeDamagePending&&!world.actors()[outside].nativeAwake,"Destroy excludes seventh-cell offset beyond authored six-cell window");
    need(world.projectiles()[0].nativeBareBody->energy==0&&world.projectiles()[0].nativeBareBody->damagePending,"Destroy marks compact Ammo for own-visit detonation");
    auto rng=before.nativeGlobalRandom;const auto shake=nativeRandomShake(rng,32);
    need(same(after.nativeGlobalRandom,rng)&&world.surprise().shakeOffset.x==shake[0]&&world.surprise().shakeOffset.y==shake[1],"mixed marking consumes no death RNG before shake");
    const auto checkpoint=disk(world);auto resumedScene=fixture();resumedScene.classes.push_back(bare);GameWorld resumed;load(resumed,resumedScene,level);std::string error;need(resumed.restoreSnapshot(checkpoint,error),error.c_str());
    need(resumed.snapshot().nativeAwake->manager.entries==entries,"mixed Destroy wake order survives disk continuation");
}

}
int main(){try{randomSurprise();shakeOrdering();mixedDestroyOrder();deathProducerOrder();radialShake();std::cout<<"PASS Surprise normal/demo stream, disk continuation, Trap construction and Destroy deferred death/shake\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
