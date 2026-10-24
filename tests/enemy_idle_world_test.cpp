#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/enemy_idle_native.h"
#include "game/spider_contact_native.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace vyruz;
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
TextNode value(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={text};return n;}
TextNode block(std::string key,std::vector<TextNode> children,std::string type={}){
    TextNode n;n.kind=TextKind::Block;n.key=key;n.children=std::move(children);if(!type.empty())n.arguments={type};return n;
}
// These actively simulated classes mirror authored Managed=1; omission means
// native unmanaged scenery and must not rely on the former all-actors loop.
SceneContent fixture(std::string subtype){
    SceneContent s;s.mapPath="TEST/ENEMY-IDLE.3DG";
    ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};
    player.properties={value("Managed","1"),block("Energy",{value("Initial","1000"),value("Max","1000")})};
    ObjectClass enemy;enemy.id={1};enemy.name="Enemy";enemy.behaviours={"Fixed","Seeker"};
    enemy.properties={value("Managed","1"),value("Sub-type",subtype),value("Can-turn-on-place","1"),value("Turn-timeout","42"),
        value("Bounding-box-size","0"),value("Speed-max","0"),
        value("Two-pieces",(subtype=="Tank" || subtype=="Turret")?"1":"0"),value("Turret-turn-timeout","75"),
        block("Energy",{value("Initial","1000"),value("Max","1000")}),
        block("TYPE",{value("Target","Player"),value("Sensor-Range","1"),value("Scan-Delay","1000"),value("Vision-Angle","360")},"Seeker")};
    s.classes={player,enemy};
    for(unsigned i=0;i<2;++i){ObjectPlacement o;o.id={i};o.classId={i};o.position=i?ContentPosition{2000,2000}:ContentPosition{500,500};
        o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;s.objects.push_back(o);}
    s.player=ObjectId{0};return s;
}
Level level(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;return l;}
auto fields(const EnemyTurnState& s){return std::tuple{s.bodyPose,s.bodyDesiredPose,s.bodyPreviousPose,
    s.gunPose,s.gunDesiredPose,s.gunPreviousPose,s.bodyPeriodMs,s.bodyRemainingMs,s.gunPeriodMs,s.gunRemainingMs,
    s.logicalHeading,s.orthogonalHeading,s.turnCommand,s.fastTurn,s.lean,
    s.gunLogicalHeading,s.gunCommand,s.gunPreviousCommand,s.gunInRange,s.gunStateKnown};}
WorldSnapshot seeded(GameWorld& world,int pose,int desired,int logical,int remaining){
    auto snapshot=world.snapshot();auto& actor=snapshot.actors[1];need(actor.enemyTurn.has_value(),"ordinary actor has native turn state");
    auto& turn=*actor.enemyTurn;turn.bodyPose=turn.bodyPreviousPose=pose;turn.bodyDesiredPose=desired;
    turn.logicalHeading=logical;turn.orthogonalHeading=(logical+3)%24;turn.bodyRemainingMs=remaining;turn.turnCommand=0;
    actor.direction=logical;const auto p=snapshot.objects[1].position;const auto c=spiderSurfaceToNative(p.x,p.y);
    actor.nativeMotion=SpiderMotionHistory{{c.x,c.y},p};snapshot.nativeWorldRandom={1,2118850534u};return snapshot;
}
bool samePosition(ContentPosition a,ContentPosition b){return a.x==b.x && a.y==b.y;}
}
int main(){try{
    const auto terrain=level();std::string error;
    for(const std::string family:{"Cop","Burner","Walker","Jeep","Roller","Heli"}) {
        const bool banked=family=="Cop" || family=="Burner";const int ring=banked?48:24;
        const int seeds[][4]={{1,1,0,0},{ring,ring,23,0},{2,3,0,0},{14,14,0,42}};
        for(const auto& seed:seeds) {
            auto scene=fixture(family);GameWorld world;need(world.load(scene,terrain,error),error.c_str());
            auto initial=seeded(world,seed[0],seed[1],seed[2],seed[3]);
            need(world.restoreSnapshot(initial,error),"legal visible/logical idle divergence restores");
            auto expected=*initial.actors[1].enemyTurn;const auto config=world.actors()[1].enemyTurnConfig;auto random=initial.nativeWorldRandom;
            expected.turnCommand=0;advanceOnePieceEnemyIdleTurn(config,expected,16,[&](int low,int high){return random.inclusive(low,high);});
            world.tick(.016f,{});const auto after=world.snapshot();
            need(fields(*after.actors[1].enemyTurn)==fields(expected),"ordinary world idle follows native pose/countdown/command order");
            need(after.nativeWorldRandom.seed==random.seed && after.nativeWorldRandom.counter==random.counter,"ordinary world idle uses exact native RNG draw count");
            need(after.actors[1].direction==expected.logicalHeading && after.actors[1].weaponHeading()==expected.logicalHeading,"ordinary motion/weapon heading retains logical ring");
            need(samePosition(after.objects[1].position,initial.objects[1].position) && after.actors[1].enemyMoveCommand==0,"ordinary idle does not invent translation");
            if(seed[0]==48)need(after.actors[1].enemyTurn->bodyPose==1 && after.actors[1].direction==23,"48-pose visual wrap retains independent logical node23");
        }

        auto scene=fixture(family);GameWorld world;need(world.load(scene,terrain,error),error.c_str());
        auto initial=seeded(world,ring,ring,23,0);need(world.restoreSnapshot(initial,error),error.c_str());world.tick(.016f,{});
        const auto checkpoint=world.snapshot();
        auto memoryScene=fixture(family);GameWorld memory;need(memory.load(memoryScene,terrain,error)&&memory.restoreSnapshot(checkpoint,error),error.c_str());
        SaveGame save;save.currentLevel=scene.mapPath;save.levels[scene.mapPath].world=checkpoint;
        std::vector<uint8_t> bytes;SaveGame decoded;need(encodeSave(save,bytes,error)&&decodeSave(bytes,decoded,error),error.c_str());
        need(bytes.size()>12 && bytes[8]==38 && bytes[9]==0,"Current writer retains v32 shared controller/RNG fields");
        auto diskScene=fixture(family);GameWorld disk;need(disk.load(diskScene,terrain,error)&&disk.restoreSnapshot(decoded.levels.at(scene.mapPath).world,error),error.c_str());
        need(fields(*disk.actors()[1].enemyTurn)==fields(*checkpoint.actors[1].enemyTurn),"disk restore retains legal visible/logical pose divergence");
        for(int tick=0;tick<180;++tick) {
            world.tick(.016f,{});memory.tick(.016f,{});disk.tick(.016f,{});
            const auto actual=world.snapshot();
            for(const auto* resumed:{&memory,&disk}) {
                const auto next=resumed->snapshot();
                need(fields(*actual.actors[1].enemyTurn)==fields(*next.actors[1].enemyTurn),"memory/disk idle continuation preserves body goal, logical ring, command and countdown");
                need(actual.nativeWorldRandom.seed==next.nativeWorldRandom.seed && actual.nativeWorldRandom.counter==next.nativeWorldRandom.counter,"memory/disk continuation preserves native world RNG stream");
                need(samePosition(actual.objects[1].position,next.objects[1].position),"memory/disk idle continuation preserves actor position");
            }
        }
        auto malformed=world.snapshot();malformed.actors[1].direction=(malformed.actors[1].direction+1)%24;
        need(!world.restoreSnapshot(malformed,error),"actor logical heading/checkpoint mismatch is rejected");
        malformed=world.snapshot();malformed.actors[1].enemyTurn->bodyPose=49;
        need(!world.restoreSnapshot(malformed,error),"out-of-range ordinary visible pose is rejected");

        auto historyScene=fixture(family);GameWorld history;need(history.load(historyScene,terrain,error),error.c_str());
        auto moved=seeded(history,1,1,0,0);moved.actors[1].nativeMotion->previousSurface.x+=1;
        need(history.restoreSnapshot(moved,error),error.c_str());history.tick(.016f,{});const auto after=history.snapshot();
        need(after.nativeWorldRandom.seed==moved.nativeWorldRandom.seed && after.nativeWorldRandom.counter==moved.nativeWorldRandom.counter,"actual position history suppresses ordinary idle RNG");
        need(after.actors[1].enemyTurn->bodyPose==1 && after.actors[1].enemyTurn->bodyRemainingMs==0,"actual position history suppresses ordinary idle controller");
    }
    for(const std::string family:{"Tank","Turret"}) {
        auto scene=fixture(family);GameWorld world;need(world.load(scene,terrain,error),error.c_str());
        auto initial=seeded(world,1,7,0,37);auto& initialTurn=*initial.actors[1].enemyTurn;
        initialTurn.gunPose=initialTurn.gunPreviousPose=2;initialTurn.gunDesiredPose=14;
        initialTurn.gunLogicalHeading=1;initialTurn.gunRemainingMs=50;initialTurn.gunPreviousCommand=203;
        initialTurn.gunInRange=true;
        need(world.restoreSnapshot(initial,error),error.c_str());
        auto expected=initialTurn;auto random=initial.nativeWorldRandom;const auto config=world.actors()[1].enemyTurnConfig;
        expected.turnCommand=expected.gunCommand=0;
        advanceMultipartEnemyIdleTurn(config,expected,100,[&](int low,int high){return random.inclusive(low,high);});
        world.tick(.1f,{});auto checkpoint=world.snapshot();
        need(fields(*checkpoint.actors[1].enemyTurn)==fields(expected),"multipart World preserves original positive-to-negative gun countdown return");
        need(checkpoint.actors[1].enemyTurn->gunRemainingMs==-50,"negative multipart gun countdown is a legal saved result");
        need(checkpoint.actors[1].weaponHeading()==1 && checkpoint.actors[1].enemyTurn->gunInRange,"multipart idle retains logical gun heading and range flag with no terminal route call");
        auto memoryScene=fixture(family);GameWorld memory;need(memory.load(memoryScene,terrain,error)&&memory.restoreSnapshot(checkpoint,error),error.c_str());
        SaveGame save;save.currentLevel=scene.mapPath;save.levels[scene.mapPath].world=checkpoint;
        std::vector<uint8_t> bytes;SaveGame decoded;need(encodeSave(save,bytes,error)&&decodeSave(bytes,decoded,error),error.c_str());
        need(bytes.size()>12 && bytes[8]==38,"Current writer retains multipart controller fields");
        auto diskScene=fixture(family);GameWorld disk;need(disk.load(diskScene,terrain,error)&&disk.restoreSnapshot(decoded.levels.at(scene.mapPath).world,error),error.c_str());
        need(fields(*disk.actors()[1].enemyTurn)==fields(expected),"v30 round-trip preserves signed countdown, separate logical gun ring, commands and range flag");
        for(int tick=0;tick<240;++tick) {
            expected.turnCommand=expected.gunCommand=0;
            advanceMultipartEnemyIdleTurn(config,expected,16,[&](int low,int high){return random.inclusive(low,high);});
            world.tick(.016f,{});memory.tick(.016f,{});disk.tick(.016f,{});
            const auto actual=world.snapshot();
            need(fields(*actual.actors[1].enemyTurn)==fields(expected),"multipart shared idle clock follows production original-instruction contract across negative and due updates");
            need(actual.nativeWorldRandom.seed==random.seed && actual.nativeWorldRandom.counter==random.counter,"multipart idle retains original RNG draw count across continuation");
            for(const auto* resumed:{&memory,&disk}) {
                const auto next=resumed->snapshot();
                need(fields(*actual.actors[1].enemyTurn)==fields(*next.actors[1].enemyTurn),"multipart memory/disk continuation preserves separate body/gun poses and rings");
                need(actual.nativeWorldRandom.seed==next.nativeWorldRandom.seed && actual.nativeWorldRandom.counter==next.nativeWorldRandom.counter,"multipart memory/disk continuation preserves native world RNG");
                need(samePosition(actual.objects[1].position,next.objects[1].position),"multipart idle continuation does not invent translation");
            }
        }
        auto divergent=world.snapshot();auto& turn=*divergent.actors[1].enemyTurn;
        turn.bodyPose=turn.bodyPreviousPose=2;turn.logicalHeading=0;turn.orthogonalHeading=3;divergent.actors[1].direction=0;
        turn.gunPose=turn.gunPreviousPose=4;turn.gunLogicalHeading=1;
        need(world.restoreSnapshot(divergent,error),"multipart legal body/gun visual-logical divergence restores");
        need(world.actors()[1].weaponHeading()==1,"multipart weapon emission uses retained ring, not derived visual pose");
        auto malformed=world.snapshot();malformed.actors[1].enemyTurn->gunLogicalHeading=24;
        need(!world.restoreSnapshot(malformed,error),"invalid multipart gun logical ring is rejected");
        malformed=world.snapshot();malformed.actors[1].enemyTurn->gunPreviousCommand=200;
        need(!world.restoreSnapshot(malformed,error),"invalid multipart previous gun command is rejected");
        auto legacy=world.snapshot();legacy.actors[1].enemyTurn->gunStateKnown=false;
        need(world.restoreSnapshot(legacy,error),"legacy absent multipart gun state migrates at class-aware restore");
        need(world.actors()[1].weaponHeading()==3 && world.actors()[1].enemyTurn->gunStateKnown &&
             !world.actors()[1].enemyTurn->gunInRange && world.actors()[1].enemyTurn->gunCommand==0 &&
             world.actors()[1].enemyTurn->gunPreviousCommand==0,"legacy multipart restore preserves old visible gun heading and resets unavailable history explicitly");
    }
    std::cout<<"All ordinary families World idle/native RNG, visual-logical divergence, actual-history admission and v30 continuation passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
