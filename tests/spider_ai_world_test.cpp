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

namespace {
void restore(GameWorld& world,const WorldSnapshot& state){
    std::string error;if(!world.restoreSnapshot(state,error))throw std::runtime_error(error);
}
void targetAt(WorldSnapshot& state,NativeNavPoint center,NativeNavPoint surfaceCenter){
    const auto surface=spiderNativeToSurface(surfaceCenter.x,surfaceCenter.y);
    state.objects[0].position={surface.x,surface.y};
    state.actors[0].nativeMotion=SpiderMotionHistory{center,state.objects[0].position};
    state.actorCells->relocate(0,state.actorCells->coordinate(surface.x,surface.y),state.nextNativeCellOrder++);
}
void routeAt(WorldSnapshot& state,float endpoint,float target,bool seeker=true){
    auto& a=state.actors[1];auto& ai=*a.spiderAI;const auto start=ai.motion->center;
    a.brain.minimumDistance=400;a.brain.sensorRange=1000;
    a.brain.seeker=seeker;a.brain.base=seeker?BrainBase::Fixed:BrainBase::Patrol;
    ai.turn=initializeSpiderTurn(config(),6);ai.turn->bodyRemainingMs=100;a.direction=6;a.turnMs=100;
    targetAt(state,{start.x+target,start.y},{start.x+target,start.y});
    auto& r=seeker?ai.seekerRoute:ai.patrolRoute;
    r.action={false,true,1000,0};if(seeker)r.target=0;r.sampledTarget=state.objects[0].position;
    r.points={insertNativeNavigationPoint(*state.spiderNavigation,start),
        insertNativeNavigationPoint(*state.spiderNavigation,{start.x+endpoint,start.y})};
    r.cursor=1;r.segmentStart=r.points[0];r.segmentEnd=r.points[1];ai.actorFlags=seeker?1:3;
}
std::vector<uint8_t> disk(const WorldSnapshot& state){
    SaveGame save;save.currentLevel="TEST/SPIDER-TURN.3DG";save.levels[save.currentLevel].world=state;
    std::vector<uint8_t> bytes;std::string error;if(!encodeSave(save,bytes,error))throw std::runtime_error(error);return bytes;
}
}
int main(){try{
    // Original42cd30/42ce40 instruction results, reproduced with Min-distance400.
    const auto terrain=level();std::string error;unsigned cases=0;
    struct Case{float endpoint,target;int status;size_t cursor;};
    for(const auto test:{Case{100,800,3,2},Case{350,800,1,2},Case{401,800,0,1},
                         Case{100,150,7,1},Case{100,350,5,1}}){
        auto scene=fixture();GameWorld world;need(world.load(scene,terrain,error),"load arrival fixture");
        auto state=seeded(world);routeAt(state,test.endpoint,test.target);restore(world,state);world.tick(.016f,{});
        const auto& after=world.actors()[1].spiderAI->seekerRoute;
        need(after.status==test.status,"World arrival status matches unchanged original instructions");
        need(after.cursor==test.cursor,"World endpoint consumption matches original status");++cases;
    }
    // Patrol keeps its own4-unit threshold, independent of Seeker Min-distance.
    auto patrolScene=fixture(true);GameWorld patrol;need(patrol.load(patrolScene,terrain,error),"load patrol");
    auto patrolState=seeded(patrol);routeAt(patrolState,100,800,false);restore(patrol,patrolState);patrol.tick(.016f,{});
    need(patrol.actors()[1].spiderAI->patrolRoute.status==0,"Patrol retains radius4");++cases;
    //42d8f0 separates surface sample from authoritative native search destination.
    auto centerScene=fixture();GameWorld center;need(center.load(centerScene,terrain,error),"load center fixture");
    auto centerState=seeded(center);const auto origin=centerState.actors[1].spiderAI->motion->center;
    centerState.actors[1].brain.sensorRange=1000;
    targetAt(centerState,{origin.x+400,origin.y},{origin.x+100,origin.y});
    const auto expectedCenter=centerState.actors[0].nativeMotion->center;
    const auto expectedSurface=centerState.objects[0].position;
    restore(center,centerState);center.tick(.016f,{});
    const auto& search=center.actors()[1].spiderAI->seekerRoute;
    need(search.search.has_value()&&!search.search->candidates.empty(),"target search begins");
    const auto destination=search.search->candidates.front().points.back();
    need(destination.x==expectedCenter.x&&destination.y==expectedCenter.y,"route uses target native center");
    need(search.sampledTarget.x==expectedSurface.x&&search.sampledTarget.y==expectedSurface.y,"rescan sample remains surface position");++cases;
    //42da80 exact circular gate also uses native center rather than surface sample.
    auto rangeScene=fixture();GameWorld range;need(range.load(rangeScene,terrain,error),"load range fixture");
    auto rangeState=seeded(range);const auto rangeOrigin=rangeState.actors[1].spiderAI->motion->center;
    rangeState.actors[1].brain.sensorRange=500;
    targetAt(rangeState,{rangeOrigin.x+800,rangeOrigin.y},{rangeOrigin.x+100,rangeOrigin.y});
    restore(range,rangeState);range.tick(.016f,{});
    need(!range.actors()[1].spiderAI->seekerRoute.action.searching,"native center beyond sensor range is rejected");++cases;
    //42ce40 near test and42caa0 tracking read the same retained native center.
    auto nearScene=fixture();GameWorld nearWorld;need(nearWorld.load(nearScene,terrain,error),"load near fixture");
    auto nearState=seeded(nearWorld);routeAt(nearState,100,800);
    const auto nearOrigin=nearState.actors[1].spiderAI->motion->center;
    targetAt(nearState,{nearOrigin.x+150,nearOrigin.y},{nearOrigin.x+800,nearOrigin.y});
    restore(nearWorld,nearState);nearWorld.tick(.016f,{});
    const auto& nearRoute=nearWorld.actors()[1].spiderAI->seekerRoute;
    need(nearRoute.status==7,"near decision reads retained center");
    need(nearRoute.segmentEnd.x==nearOrigin.x+150&&nearRoute.segmentEnd.y==nearOrigin.y,"tracking endpoint reads retained center");++cases;
    //42ce40 directly accepts Seeker mode2, but its Patrol fallback cannot
    //select the Spider's retained type7 Seeker. Do not inject near/brake into
    //Patrol while the player is between the four firing headings.
    for(const NativeNavPoint offset: {NativeNavPoint{150,0},NativeNavPoint{100,100},
            NativeNavPoint{0,150},NativeNavPoint{-100,100},NativeNavPoint{-150,0},
            NativeNavPoint{-100,-100},NativeNavPoint{0,-150},NativeNavPoint{100,-100}}){
        auto diagonalScene=fixture(true);GameWorld diagonal;need(diagonal.load(diagonalScene,terrain,error),"load diagonal AI fixture");
        auto st=seeded(diagonal);routeAt(st,100,150,true);routeAt(st,100,150,false);
        auto& ai=*st.actors[1].spiderAI;ai.seekerRoute.action.active=false;
        const auto origin=ai.motion->center;
        targetAt(st,{origin.x+offset.x,origin.y+offset.y},{origin.x+offset.x,origin.y+offset.y});
        restore(diagonal,st);diagonal.tick(.016f,{});
        need(diagonal.actors()[1].spiderAI->patrolRoute.status==0,"Patrol does not borrow type7 Seeker near status");
        need(diagonal.actors()[1].enemyMoveCommand==200,"Patrol retains reposition command for close off-axis player");++cases;
        SaveGame saved;need(decodeSave(disk(diagonal.snapshot()),saved,error),"decode close-target checkpoint");
        auto replayScene=fixture(true);GameWorld replay;need(replay.load(replayScene,terrain,error),"load close-target replay");
        restore(replay,saved.levels.at(saved.currentLevel).world);
        for(int tick=0;tick<30;++tick){diagonal.tick(.016f,{});replay.tick(.016f,{});
            need(disk(diagonal.snapshot())==disk(replay.snapshot()),"close-target Patrol disk continuation");}
    }
    // Live Spider search must opt into40f310's original corner arithmetic.
    auto cornerTerrain=level();
    constexpr float minX=2.434098482131958f;
    cornerTerrain.nativeNavigationDecor.push_back({minX,0,10,10,0,0,0,false,false,false});
    auto cornerScene=fixture();GameWorld corners;need(corners.load(cornerScene,cornerTerrain,error),"load corner fixture");
    auto cornerState=seeded(corners);auto& cornerActor=cornerState.actors[1];
    auto& cornerAI=*cornerActor.spiderAI;
    const NativeNavPoint cornerStart{minX,0};
    const auto cornerSurface=spiderNativeToSurface(cornerStart.x,cornerStart.y);
    cornerState.objects[1].position={cornerSurface.x,cornerSurface.y};
    cornerAI.motion=SpiderMotionHistory{cornerStart,cornerState.objects[1].position};
    cornerState.actorCells->relocate(1,cornerState.actorCells->coordinate(cornerSurface.x,cornerSurface.y),cornerState.nextNativeCellOrder++);
    cornerAI.seekerRoute.action={true,false,1000,0};
    cornerAI.seekerRoute.search=beginNativeNavigation(cornerStart,{100,100},{16,16},0,
        [&](NativeNavPoint p){return insertNativeNavigationPoint(*cornerState.spiderNavigation,p);});
    restore(corners,cornerState);corners.tick(.016f,{});
    const auto& cornerSearch=corners.actors()[1].spiderAI->seekerRoute.search;
    need(cornerSearch.has_value()&&cornerSearch->seen.size()==3,"live search inserts one obstacle corner");
    need(cornerSearch->seen.back().x==-21.565900802612305f&&cornerSearch->seen.back().y==-24,
        "live Spider corner matches original x87 subtraction order");++cases;
    // Save mid-search, decode, and continue both worlds byte-for-byte.
    auto bytes=disk(center.snapshot());SaveGame decoded;
    if(!decodeSave(bytes,decoded,error))throw std::runtime_error(error);
    auto resumedScene=fixture();GameWorld resumed;need(resumed.load(resumedScene,terrain,error),"load resumed fixture");
    restore(resumed,decoded.levels.at(decoded.currentLevel).world);
    for(int tick=0;tick<180;++tick){center.tick(.016f,{});resumed.tick(.016f,{});
        need(disk(center.snapshot())==disk(resumed.snapshot()),"mid-search disk continuation is deterministic");}
    std::cout<<"PASS "<<cases<<" original-derived World cases,180 search and240 close-target byte-exact disk continuation ticks\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
