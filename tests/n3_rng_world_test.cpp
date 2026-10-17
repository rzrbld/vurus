#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace vyruz {struct AmmoImpactNativeTestAccess {
    static bool phase(GameWorld& world,Projectile& shot,float ms){return world.updateAmmoActivation(shot,ms);}
};}
namespace {
void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
TextNode value(std::string key,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(v)};return n;}
TextNode block(std::string key,std::vector<TextNode> children,std::string argument={}){TextNode n;n.kind=TextKind::Block;n.key=std::move(key);n.children=std::move(children);if(!argument.empty())n.arguments={std::move(argument)};return n;}
SceneContent fixture(){
    SceneContent scene;scene.mapPath="TEST/N3-RNG.3DG";
    ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Sub-type","Vyruz"),value("Managed","1"),value("Bounding-box-size","20"),block("Energy",{value("Initial","1000"),value("Max","1000")})};
    ObjectClass product;product.id={1};product.name="Final-Explosion-Part";product.behaviours={"Straight"};product.properties={value("Sub-type","Thing"),value("Managed","1"),value("Bounding-box-size","0"),value("Launched","1"),value("Use-inertia","1"),value("Life-time","1000"),block("Thrust",{value("Initial","90"),value("Max","90")})};
    const auto emitter=[&](uint32_t id,const char* name,const char* delay){ObjectClass c;c.id={id};c.name=name;c.behaviours={"Fixed"};c.properties={value("Managed","1"),value("Bounding-box-size","0"),block("CREATE",{value("Name",product.name),value("Delay",delay),value("Repeat-timeout","0"),value("Random-dir","1")})};return c;};
    scene.classes={player,product,emitter(2,"Immediate","0"),emitter(3,"Deferred","14")};
    ObjectPlacement p;p.id={0};p.classId={0};p.position={1000,1000};p.properties=player.properties;p.behaviours=player.behaviours;scene.objects={p};scene.player=ObjectId{0};return scene;
}
Level arena(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);return l;}
void load(GameWorld& world,SceneContent& scene,const Level& level){std::string e;need(world.load(scene,level,e),e.c_str());world.setDebugEnemyAI(false);world.setDebugGodMode(true);}
WorldSnapshot disk(const GameWorld& world){SaveGame save;save.currentLevel="TEST/N3-RNG.3DG";save.levels[save.currentLevel].world=world.snapshot();std::vector<uint8_t> bytes;std::string e;need(encodeSave(save,bytes,e),e.c_str());SaveGame copy;need(decodeSave(bytes,copy,e),e.c_str());return copy.levels.at(save.currentLevel).world;}
void phaseRng(){
    auto scene=fixture();const auto level=arena();GameWorld world;load(world,scene,level);const auto before=world.snapshot();
    Projectile shot;shot.phaseConfigReady=shot.phaseInitialized=true;shot.inactiveMs=1;shot.onMinimum=1000;shot.onMaximum=3000;shot.offMinimum=2000;shot.offMaximum=10000;
    // Captured whole4406e0 observations in n3-completion/order/phase-probe:
    // fresh bodyFEABCEFE/ABCDEFDA selects2375,7499,21245 for these intervals.
    need(AmmoImpactNativeTestAccess::phase(world,shot,14)&&shot.onMs==2361&&shot.inactiveMs==0,"native Mine on timeout uses body-seeded draw2375, then current14ms decrement");
    shot.onMs=1;need(!AmmoImpactNativeTestAccess::phase(world,shot,14)&&shot.inactiveMs==7499&&shot.phaseHidden,"native Mine off timeout uses body-seeded draw7499");
    shot.inactiveMs=1;need(AmmoImpactNativeTestAccess::phase(world,shot,14)&&shot.onMs==2361,"next activation repeats unchanged native body seeds");
    shot.onMs=1;shot.offMaximum=30000;need(!AmmoImpactNativeTestAccess::phase(world,shot,14)&&shot.inactiveMs==21245,"native infinite-mine off interval uses same unchanged body seeds");
    const auto after=world.snapshot();need(after.randomState==before.randomState,"Ammo phase does not consume legacy MT");need(after.nativeWorldRandom.seed==before.nativeWorldRandom.seed&&after.nativeWorldRandom.counter==before.nativeWorldRandom.counter,"Ammo phase restores world context");need(after.nativeGlobalRandom.seed==before.nativeGlobalRandom.seed&&after.nativeGlobalRandom.counter==before.nativeGlobalRandom.counter,"Ammo phase restores outer global context");
}
void createRng(){
    auto scene=fixture();const auto original=scene;const auto level=arena();GameWorld world;load(world,scene,level);const auto mt=world.snapshot().randomState;
    const int nativeHeadings[]={16,1,7,11};
    for(int n=0;n<3;++n){const auto first=world.actors().size();world.spawn({2},{2000,1000});need(world.actors().size()==first+2,"immediate CREATE emits exactly one product");need(world.actors().back().direction==nativeHeadings[n],"CREATE Random-dir follows captured native world259c stream");}
    auto checkpoint=disk(world);need(checkpoint.nativeWorldRandom.seed==0xf3bfbfaau&&checkpoint.nativeWorldRandom.counter==0xabcdefddu,"three CREATE draws retain captured native context words");need(checkpoint.randomState==mt,"CREATE Random-dir leaves MT unchanged");
    auto resumedScene=original;GameWorld resumed;load(resumed,resumedScene,level);std::string e;need(resumed.restoreSnapshot(checkpoint,e),e.c_str());resumed.setDebugEnemyAI(false);resumed.setDebugGodMode(true);
    for(auto* target:{&world,&resumed}){const auto first=target->actors().size();target->spawn({3},{2200,1000});need(target->actors().size()==first+1,"delayed CREATE does not draw during construction");target->tick(.014f,{});need(target->actors().size()==first+2&&target->actors().back().direction==nativeHeadings[3],"runtime CREATE consumes next captured native draw after disk continuation");need(target->snapshot().randomState==mt,"runtime CREATE also leaves MT unchanged");}
    const auto a=world.snapshot(),b=resumed.snapshot();need(a.nativeWorldRandom.seed==b.nativeWorldRandom.seed&&a.nativeWorldRandom.counter==b.nativeWorldRandom.counter,"native world stream resumes identically");
}
}
int main(){try{phaseRng();createRng();std::cout<<"PASS native body phase RNG, isolated CREATE world stream and disk continuation\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
