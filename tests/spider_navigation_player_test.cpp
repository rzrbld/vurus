// Preinclude dependencies so the test-only access macro affects GameWorld only.
#include "game/effect_camera_native.h"
#include "game/indirect_optional.h"
#include "game/demo_replay.h"
#include "game/enemy_turn_native.h"
#include "game/native_random.h"
#include "game/native_awake_state.h"
#include "game/player_facing_native.h"
#include "game/native_actor_cells.h"
#include "game/generator_occupancy_native.h"
#include "game/native_ammo_cells.h"
#include "game/world_pool_state.h"
#include "game/tile_body_native.h"
#include "game/ammo_owner_counters_native.h"
#include "game/ordinary_enemy_ai_native.h"
#include "game/grenade_contact_native.h"
#include "game/native_body_contacts.h"
#include "game/brain.h"
#include "game/final_object.h"
#include "game/ordered_inventory.h"
#include "game/difficulty.h"
#include "game/audio_events.h"
#include "game/map_view.h"
#include "game/notifications.h"
#include "game/combat.h"
#include "game/controls.h"
#include "game/motion_contact.h"
#include "game/enemy_motion.h"
#include "game/interactions.h"
#include "game/level.h"
#include "game/navigation.h"
#include "game/spider_ai_state.h"
#include "game/missile_guidance.h"
#include "game/gates.h"
#include "game/lifts.h"
#include "game/surprise.h"
#include "render/animation.h"
#include <random>
#include <map>
#include <set>
#include <unordered_map>
#define private public
#include "game/world.h"
#undef private
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
int main(){try{
    // Original40f9b0 query probe: actor type3/subtype0x18 is admitted;
    // graphflags1/2 suppress it.42c5a0 disables self and target, not Player.
    const auto terrain=level();std::string error;unsigned cases=0;
    auto scene=fixture(true);
    for(auto* p:{&scene.classes[0].properties,&scene.objects[0].properties})p->push_back(value("Bounding-box-size","20"));
    const auto originalScene=scene;
    GameWorld world;need(world.load(scene,terrain,error),"load Player obstacle fixture");
    auto state=seeded(world);routeAt(state,160,80,false);
    auto& actor=state.actors[1];actor.brain.seeker=false;actor.enemyMotionConfig.acceleration=0;
    const auto start=actor.spiderAI->motion->center;
    targetAt(state,{start.x+80,start.y},{start.x+80,start.y});
    restore(world,state);world.updateSpiderNavigation(0);
    const NativeNavPoint end{start.x+160,start.y};
    need(world.spiderNavigationObstacle(1,{},start,end,{20,20},false).has_value(),"Patrol sees Player obstacle");++cases;
    need(!world.spiderNavigationObstacle(1,0,start,end,{20,20},false),"Seeker excludes its Player target");++cases;
    need(!world.spiderNavigationObstacle(0,{},start,end,{20,20},false),"query self remains excluded");++cases;
    auto hidden=world.snapshot();hidden.objects[0].flags.invisible=true;
    restore(world,hidden);
    need(!world.spiderNavigationObstacle(1,{},start,end,{20,20},false),"hidden Player without Keep-bb remains excluded");++cases;
    restore(world,state);world.updateSpiderNavigation(0);
    // Run the actual World Spider callback rather than substituting a helper
    // filter: its in-flight Patrol search must route around the Player BBOX.
    auto searching=world.snapshot();auto& patrol=searching.actors[1].spiderAI->patrolRoute;
    patrol.action={true,false,100000,0};patrol.points.clear();patrol.cursor=0;patrol.saved.reset();
    patrol.search=beginNativeNavigation(start,end,{20,20},0,[&](NativeNavPoint p){return insertNativeNavigationPoint(*searching.spiderNavigation,p);});
    restore(world,searching);
    for(int tick=0;tick<90&&!world.actors()[1].spiderAI->patrolRoute.action.active;++tick)world.tick(.016f,{});
    const auto& route=world.actors()[1].spiderAI->patrolRoute;
    need(route.action.active&&route.points.size()>2,"live World Patrol builds detour around Player");
    need(std::any_of(route.points.begin(),route.points.end(),[&](auto p){return p.y!=start.y;}),"detour leaves direct line");++cases;
    // Continuation covers the graph-point ownership and route state after the
    // newly admitted obstacle has participated in a real World search.
    SaveGame decoded;need(decodeSave(disk(world.snapshot()),decoded,error),"decode Player detour checkpoint");
    auto replayScene=originalScene;GameWorld replay;need(replay.load(replayScene,terrain,error),"load detour replay");
    restore(replay,decoded.levels.at(decoded.currentLevel).world);
    for(int tick=0;tick<60;++tick){world.tick(.016f,{});replay.tick(.016f,{});
        need(disk(world.snapshot())==disk(replay.snapshot()),"Player detour disk continuation");}
    // Actual Seeker search retains its targeted-Player exclusion and produces
    // the direct two-point cardinal route admitted by the native Spider rule.
    auto seek=searching;auto& seekerAI=*seek.actors[1].spiderAI;
    seek.actors[1].brain.seeker=true;seek.actors[1].brain.base=BrainBase::Fixed;
    seekerAI.patrolRoute={};seekerAI.seekerRoute={};auto& seeker=seekerAI.seekerRoute;
    seeker.target=0;seeker.sampledTarget=seek.objects[0].position;
    seeker.action={true,false,100000,0};
    seeker.search=beginNativeNavigation(start,{start.x+80,start.y},{20,20},0,[&](NativeNavPoint p){return insertNativeNavigationPoint(*seek.spiderNavigation,p);});
    auto seekerScene=originalScene;GameWorld seekerWorld;need(seekerWorld.load(seekerScene,terrain,error),"load Seeker control");
    restore(seekerWorld,seek);
    for(int tick=0;tick<90&&!seekerWorld.actors()[1].spiderAI->seekerRoute.action.active;++tick)seekerWorld.tick(.016f,{});
    const auto& direct=seekerWorld.actors()[1].spiderAI->seekerRoute;
    need(direct.action.active&&direct.points.size()==2,"live World Seeker keeps direct route to Player target");++cases;
    std::cout<<"PASS "<<cases<<" Player obstacle World controls;60 byte-exact disk continuation ticks\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
