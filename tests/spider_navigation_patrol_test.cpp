#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/spider_turn_native.h"
#include "game/spider_contact_native.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "host_temp.h"
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
    scene.player=ObjectId{0};
    PatrolPath path;path.id={0};path.points={{2200,2000},{2400,2000}};scene.paths={path};return scene;
}
Level level(){Level result;result.mapW=result.mapH=100;result.hasTerrainCollision=true;return result;}
EnemyTurnConfig config(){return {0x1f,false,true,42,0};}
WorldSnapshot seeded(GameWorld& world){
    auto state=world.snapshot();auto& actor=state.actors[1];actor.spiderAI.emplace();
    actor.spiderAI->turn=initializeSpiderTurn(config(),0);
    const auto p=state.objects[1].position;const auto center=spiderSurfaceToNative(p.x,p.y);
    actor.spiderAI->motion=SpiderMotionHistory{{center.x,center.y},p};
    actor.direction=0;actor.turnMs=0;state.nativeWorldRandom={1,2118850534u};return state;
}
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

namespace {
void helperControls(){
    // Original42e400 branch at42e539, including42e030's sole write to+24.
    // The retained native probe exercises both visits with17ms/250ms.
    for(bool seeker:{false,true})for(float dt:{1.f,17.f,50.f})
    for(float scan:{-1.f,0.f,3.f,250.f})for(uint8_t initial:{uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(3)}){
        SpiderActionState action{true,true,scan,11};uint8_t flags=initial;
        int drops=0,begins=0,steers=0;
        SpiderActionCallbacks cb;cb.dropRoute=[&]{++drops;};cb.steer=[&]{++steers;};
        cb.beginSearch=[&]{++begins;return true;};
        const SpiderActionInput input{dt,250,seeker,false,true};
        advanceSpiderAction(action,flags,input,cb);
        need(!action.active&&!action.searching,"escape clears both action bits");
        need(flags==(initial&~uint8_t(1)),"escape preserves other actor bits");
        need(action.scanRemainingMs==(seeker?scan:0)&&action.searchRemainingMs==11,"only Patrol scan is reset");
        need(!drops&&!begins&&!steers,"escape does not drop or steer or search inline");
        advanceSpiderAction(action,flags,input,cb);
        if(!seeker || scan<=0)need(begins==1&&action.searching&&action.scanRemainingMs==(seeker?scan+250:250),"next visit retries without waiting");
        else need(!begins&&!action.searching&&action.scanRemainingMs==scan-dt,"Seeker keeps original deferred countdown");
        need(!drops,"neither escape controller releases the route");
    }
}
void sameRoute(const SpiderRouteCore& actual,const SpiderRouteCore& expected){
    need(actual.points.size()==expected.points.size()&&actual.cursor==expected.cursor,"retain route size/cursor");
    for(size_t i=0;i<actual.points.size();++i)need(actual.points[i]==expected.points[i]&&actual.points[i].graphNode==expected.points[i].graphNode,"retain route point and identity");
    need(actual.segmentStart==expected.segmentStart&&actual.segmentStart.graphNode==expected.segmentStart.graphNode&&
         actual.segmentEnd==expected.segmentEnd&&actual.segmentEnd.graphNode==expected.segmentEnd.graphNode,"retain segment and identity");
}
void liveWorld(){
    auto scene=fixture(true);auto terrain=level();GameWorld world;std::string error;
    need(world.load(scene,terrain,error),error.c_str());auto state=seeded(world);
    routeAt(state,100,800,false);auto& actor=state.actors[1];actor.brain.seeker=false;
    auto& route=actor.spiderAI->patrolRoute;route.action.scanRemainingMs=250;
    // Valid owned route nodes, deliberately outside its corridor after a contact
    // displacement. The real World action path, not a helper-only invocation.
    for(auto& point:route.points){
        removeNativeNavigationObject(*state.spiderNavigation,point.graphNode);point.x+=1000;
        point=insertNativeNavigationPoint(*state.spiderNavigation,point);
    }
    route.segmentStart=route.points[0];route.segmentEnd=route.points[1];
    actor.spiderAI->patrol={true,false,0};actor.brainState.initialized=true;actor.brainState.point=0;
    const auto original=static_cast<SpiderRouteCore>(route);restore(world,state);
    world.tick(.017f,{});
    const auto escaped=world.snapshot();const auto& after=escaped.actors[1].spiderAI->patrolRoute;
    need(!after.action.active&&!after.action.searching&&after.action.scanRemainingMs==0,"World escape clears flags and resets countdown");
    need(escaped.actors[1].spiderAI->actorFlags==2,"World escape preserves base-action actor flag");
    sameRoute(after,original);need(!after.saved&&!after.search,"escape retains route without prematurely searching");
    for(const auto& point:original.points)need(escaped.spiderNavigation->objects[point.graphNode].alive,"route point remains owned/live");
    // Save to an isolated real file exactly at the escaped boundary, then check
    // both the next-visit retry and deterministic subsequent continuation.
    const auto directory=hostTempDirectory("vurus-spider-patrol-trial-");
    SaveGame save;save.currentLevel=scene.mapPath;save.levels[scene.mapPath].world=escaped;
    need(writeSaveFile((directory/"escaped.vzs").string(),save,error),error.c_str());
    SaveGame decoded;need(readSaveFile((directory/"escaped.vzs").string(),decoded,error),error.c_str());
    auto scene2=fixture(true);auto terrain2=level();GameWorld resumed;
    need(resumed.load(scene2,terrain2,error),error.c_str());restore(resumed,decoded.levels.at(scene.mapPath).world);
    need(disk(world.snapshot())==disk(resumed.snapshot()),"disk restore preserves escaped boundary");
    for(int tick=0;tick<80;++tick){
        world.tick(.017f,{});resumed.tick(.017f,{});
        if(tick==0){const auto& retry=world.actors()[1].spiderAI->patrolRoute;
            need(!retry.action.active&&retry.action.searching&&retry.action.scanRemainingMs==250,"World retries on very next update");
            need(retry.search&&retry.saved,"World begins search with retained route snapshot");
            sameRoute(retry,original);sameRoute(*retry.saved,original);
            need(retry.saved->action.scanRemainingMs==250,"retry snapshots scan after original period addition");
            need(world.actors()[1].spiderAI->patrol.point==0,"escape does not advance authored waypoint");
        }
        need(disk(world.snapshot())==disk(resumed.snapshot()),"byte-exact disk continuation");
    }
    std::filesystem::remove_all(directory);
}
}
int main(){try{helperControls();liveWorld();std::cout<<"Spider Patrol escape:96 helper cases/192 visits, live next-visit retry,80 disk continuation ticks passed\n";
    return 0;}catch(const std::exception& error){std::cerr<<"Spider Patrol trial: "<<error.what()<<'\n';return 1;}}
