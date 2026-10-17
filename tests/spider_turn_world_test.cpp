#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/spider_turn_native.h"
#include "game/spider_contact_native.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace vyruz;
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
TextNode value(std::string key,std::string text){TextNode node;node.kind=TextKind::Assignment;node.key=key;node.values={text};return node;}
TextNode block(std::string key,std::vector<TextNode> children,std::string type={}){
    TextNode node;node.kind=TextKind::Block;node.key=key;node.children=std::move(children);
    if(!type.empty())node.arguments={type};return node;
}
// These actively simulated classes mirror authored Managed=1; omission means
// native unmanaged scenery and must not rely on the former all-actors loop.
SceneContent fixture(bool patrol=false){
    SceneContent scene;scene.mapPath="TEST/SPIDER-TURN.3DG";
    ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};
    player.properties={value("Managed","1"),block("Energy",{value("Initial","1000"),value("Max","1000")})};
    ObjectClass spider;spider.id={1};spider.name="Spider";spider.behaviours={patrol?"Patrol":"Fixed","Seeker"};
    spider.properties={value("Managed","1"),value("Sub-type","Spider"),value("Can-turn-on-place","1"),value("Turn-timeout","42"),
        value("Bounding-box-size","0"),value("Move-during-path-search",patrol?"1":"0"),value("Use-inertia","0"),
        value("Speed-max","100"),value("Acceleration-step","10"),value("Decceleration-step","0"),
        block("Thrust",{value("Initial","0"),value("Min","0"),value("Max","100")}),
        block("Weight",{value("Initial","100"),value("Min","100"),value("Max","100")}),
        block("Energy",{value("Initial","1000"),value("Max","1000")}),
        block("TYPE",{value("Target","Player"),value("Sensor-Range","1"),value("Scan-Delay","1000"),value("Vision-Angle","360")},"Seeker"),
        block("TYPE",{value("Path-ID","0"),value("Scan-Delay","250")},"Patrol")};
    scene.classes={player,spider};
    for(unsigned i=0;i<2;++i){ObjectPlacement object;object.id={i};object.classId={i};
        object.position=i?ContentPosition{2000,2000}:ContentPosition{500,500};
        object.properties=scene.classes[i].properties;object.behaviours=scene.classes[i].behaviours;
        scene.objects.push_back(object);}
    scene.player=ObjectId{0};return scene;
}
Level level(){Level result;result.mapW=result.mapH=100;result.hasTerrainCollision=true;return result;}
EnemyTurnConfig config(){return {0x1f,false,true,42,0};}
auto fields(const EnemyTurnState& s){return std::tuple{s.bodyPose,s.bodyDesiredPose,s.bodyPreviousPose,
    s.bodyPeriodMs,s.bodyRemainingMs,s.logicalHeading,s.orthogonalHeading,s.turnCommand,s.fastTurn};}
WorldSnapshot seeded(GameWorld& world){
    auto state=world.snapshot();auto& actor=state.actors[1];actor.spiderAI.emplace();
    actor.spiderAI->turn=initializeSpiderTurn(config(),0);
    const auto p=state.objects[1].position;const auto center=spiderSurfaceToNative(p.x,p.y);
    actor.spiderAI->motion=SpiderMotionHistory{{center.x,center.y},p};
    actor.direction=0;actor.turnMs=0;state.nativeWorldRandom={1,2118850534u};return state;
}
bool near(ContentPosition a,ContentPosition b){return std::abs(a.x-b.x)<.0001f && std::abs(a.y-b.y)<.0001f;}
}
int main(){try{
    const auto terrain=level();std::string error;
    auto scene=fixture();GameWorld world;need(world.load(scene,terrain,error),error.c_str());
    auto initial=seeded(world);need(world.restoreSnapshot(initial,error),error.c_str());
    auto expected=*initial.actors[1].spiderAI->turn;auto random=initial.nativeWorldRandom;
    beginSpiderTurnUpdate(expected);advanceSpiderIdleTurn(config(),expected,16,[&](int low,int high){return random.inclusive(low,high);});
    world.tick(.016f,{});
    auto snapshot=world.snapshot();const auto& actor=world.actors()[1];
    need(actor.spiderAI && actor.spiderAI->turn,"world initializes retained Spider controller");
    need(fields(*actor.spiderAI->turn)==fields(expected),"world idle pass uses original shared pose/countdown controller");
    need(snapshot.nativeWorldRandom.seed==random.seed && snapshot.nativeWorldRandom.counter==random.counter,"world idle consumes both native trigger/target draws");
    need(actor.direction==23 && actor.weaponHeading()==0,"visible intermediate body pose retains independent logical weapon heading");
    need(near(scene.objects[1].position,initial.objects[1].position) && actor.enemyMoveCommand==0,"stationary idle turn does not invent propulsion");
    need(actor.turnMs==expected.bodyRemainingMs,"legacy countdown adapter follows native remaining milliseconds");

    beginSpiderTurnUpdate(expected);advanceSpiderIdleTurn(config(),expected,16,[&](int low,int high){return random.inclusive(low,high);});
    world.tick(.016f,{});snapshot=world.snapshot();
    need(fields(*world.actors()[1].spiderAI->turn)==fields(expected),"second idle frame retains pending desired pose");
    need(snapshot.nativeWorldRandom.counter==random.counter && random.counter==2118850536u,"mid-turn idle frames do not draw native RNG");

    SaveGame save;save.currentLevel=scene.mapPath;save.levels[scene.mapPath].world=snapshot;
    std::vector<uint8_t> bytes;SaveGame decoded;
    need(encodeSave(save,bytes,error) && decodeSave(bytes,decoded,error),error.c_str());
    need(bytes.size()>12 && bytes[8]==35 && bytes[9]==0,"disk checkpoint uses v32 shared native controller extension");
    auto restoredScene=fixture();GameWorld restored;need(restored.load(restoredScene,terrain,error),error.c_str());
    need(restored.restoreSnapshot(decoded.levels.at(scene.mapPath).world,error),error.c_str());
    need(fields(*restored.actors()[1].spiderAI->turn)==fields(*world.actors()[1].spiderAI->turn),"v29 restores mid-pose desired/logical/countdown state");
    for(int tick=0;tick<180;++tick){
        world.tick(.016f,{});restored.tick(.016f,{});const auto a=world.snapshot(),b=restored.snapshot();
        need(fields(*a.actors[1].spiderAI->turn)==fields(*b.actors[1].spiderAI->turn),"v29 turn continuation matches every update");
        need(a.nativeWorldRandom.seed==b.nativeWorldRandom.seed && a.nativeWorldRandom.counter==b.nativeWorldRandom.counter,"v29 continuation preserves native world RNG draws");
        need(near(a.objects[1].position,b.objects[1].position),"v29 continuation preserves Spider position");
    }
    auto bad=world.snapshot();bad.actors[1].spiderAI->turn->bodyPose=25;
    need(!world.restoreSnapshot(bad,error),"reject corrupt Spider body pose");
    bad=world.snapshot();bad.actors[1].spiderAI->turn->bodyPose=bad.actors[1].direction==0?2:1;
    need(!world.restoreSnapshot(bad,error),"reject Spider visible-pose/checkpoint inconsistency");

    // History compares actual positions, not magnitude or nominal idle state.
    auto historyScene=fixture();GameWorld history;need(history.load(historyScene,terrain,error),error.c_str());
    auto moved=seeded(history);moved.actors[1].spiderAI->motion->previousSurface.x+=1;
    need(history.restoreSnapshot(moved,error),error.c_str());history.tick(.016f,{});
    auto afterHistory=history.snapshot();
    need(afterHistory.nativeWorldRandom.seed==moved.nativeWorldRandom.seed && afterHistory.nativeWorldRandom.counter==moved.nativeWorldRandom.counter,"changed position history suppresses idle RNG");
    need(afterHistory.actors[1].direction==0,"changed position history suppresses idle pose turn");

    // Missing authored Patrol path retains existing forward-search policy. A
    // cardinal visible pose can propel along a different retained logical ring.
    auto patrolScene=fixture(true);GameWorld patrol;need(patrol.load(patrolScene,terrain,error),error.c_str());
    auto driving=seeded(patrol);auto& driver=driving.actors[1];auto& turn=*driver.spiderAI->turn;
    turn.bodyPose=turn.bodyDesiredPose=turn.bodyPreviousPose=7;turn.logicalHeading=12;turn.orthogonalHeading=15;
    turn.bodyRemainingMs=100;driver.direction=6;driver.turnMs=100;
    need(patrol.restoreSnapshot(driving,error),error.c_str());
    const auto motionConfig=patrol.actors()[1].enemyMotionConfig;
    EnemyMotionState motion{driver.contactVelocity,driver.enemyThrust,driver.enemyMagnitude,driver.contactMotion};
    EnemyMotionCommand command;command.heading=12;command.throttle=EnemyThrottle::Forward;
    const auto delta=advanceEnemyMotion(motion,command,.016f,motionConfig);
    need(delta.x!=0 || delta.y!=0,"logical-heading propulsion control is nondegenerate");
    patrol.tick(.016f,{});const auto driven=patrol.snapshot();
    need(driven.actors[1].enemyMoveCommand==200,"missing-path Patrol keeps accepted forward command");
    need(driven.nativeWorldRandom.counter==driving.nativeWorldRandom.counter,"forward command suppresses idle RNG");
    // move() keeps authoritative native coordinates and projects the absolute
    // endpoint with original float constants; adding a projected displacement
    // to surface coordinates differs by one ULP at this fixture's position.
    const auto start=driver.spiderAI->motion->center;
    const float factor=14.f/(.016f*1000.f);
    const auto expectedSurface=spiderNativeToSurface(start.x+motion.velocity.x/factor,start.y+motion.velocity.z/factor);
    need(near(driven.objects[1].position,{expectedSurface.x,expectedSurface.y}),"Spider propulsion retains native endpoint projection");
    const auto end=driven.actors[1].spiderAI->motion->center;
    need(spiderContactHeading({end.x-start.x,end.y-start.y})==12,"Spider propulsion uses logical ring rather than visible pose heading");
    need(driven.actors[1].direction==6 && driven.actors[1].weaponHeading()==12,"propulsion retains separate visible/logical Spider headings");
    std::cout<<"Spider World idle admission, native RNG draws, logical heading, v29 continuation and malformed-state checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
