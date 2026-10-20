#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace vyruz;
namespace {
void need(bool ok,const std::string& why) {
    if(!ok){std::cerr<<why<<'\n';std::exit(1);}
}
TextNode value(std::string key,std::string text) {
    TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={text};return n;
}
TextNode block(std::string key,std::vector<TextNode> children,std::string type={}) {
    TextNode n;n.kind=TextKind::Block;n.key=key;n.children=children;
    if(!type.empty())n.arguments={type};return n;
}
SceneContent fixture(bool generic=false) {
    SceneContent s;s.mapPath="TEST/LEGACY.3DG";
    ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};
    player.properties={value("Managed","1"),value("Sub-type","Vyruz"),
        value("Bounding-box-size","4"),block("Energy",{value("Initial","100"),value("Max","100")})};
    ObjectClass probe;probe.id={1};probe.name="Probe";
    probe.behaviours=generic?std::vector<std::string>{"Random","Seeker"}:std::vector<std::string>{"Fixed"};
    // Unknown subtype intentionally exercises the legacy adapter rather than
    // the separately preserved native ordinary-enemy action controllers.
    probe.properties={value("Managed","1"),value("Sub-type","Probe"),
        value("Bounding-box-size","4"),value("Speed-max","80"),
        block("Energy",{value("Initial","100")}),
        block("TYPE",{value("Target","Player"),value("Sensor-Range","0")},"Seeker"),
        block("TYPE",{value("Range","1024"),value("Delay","1")},"Random")};
    s.classes={player,probe};
    for(unsigned i=0;i<2;++i) {
        ObjectPlacement o;o.id={i};o.classId={i};o.properties=s.classes[i].properties;
        o.behaviours=s.classes[i].behaviours;
        o.position=i?ContentPosition{200,200}:ContentPosition{1100,800};s.objects.push_back(o);
    }
    s.player=ObjectId{0};return s;
}
std::vector<uint8_t> encoded(const GameWorld& world,const SceneContent& scene) {
    SaveGame save;save.currentLevel=scene.mapPath;
    save.levels[save.currentLevel].world=world.snapshot();
    std::vector<uint8_t> bytes;std::string error;need(encodeSave(save,bytes,error),error);return bytes;
}
}

int main() {
    Level level;level.mapW=level.mapH=32;level.hasTerrainCollision=true;
    level.collisionMask.assign(1024,0xff);level.terrain.assign(1024,TerrainGround);
    auto scene=fixture();GameWorld world;std::string error;
    need(world.load(scene,level,error),error);const auto count=world.actors().size();
    for(int i=0;i<60;++i)world.tick(1.f/60,{});
    need(world.snapshot().navigationCursor==60*count,"empty scan advances saved cursor once per row");
    world.setDebugEnemyAI(false);const auto cursor=world.snapshot().navigationCursor;
    for(int i=0;i<7;++i)world.tick(1.f/60,{});
    need(world.snapshot().navigationCursor==cursor,"AI-off does not advance cursor");
    world.setDebugEnemyAI(true);

    auto state=world.snapshot();state.surprise.kind=SurpriseKind::TimeWarp;
    state.surprise.applied=true;state.surprise.remainingMs=1000;state.surprise.timeWarpPhase=1;
    need(world.restoreSnapshot(state,error),error);
    for(int i=0;i<3;++i)world.tick(1.f/60,{});
    need(world.snapshot().navigationCursor==cursor,"Time Warp player-only passes do not advance cursor");
    world.tick(1.f/60,{});
    need(world.snapshot().navigationCursor==cursor+count,"Time Warp full pass advances cursor");

    state=world.snapshot();state.surprise={};
    state.navigationCursor=std::numeric_limits<size_t>::max()-1;
    need(world.restoreSnapshot(state,error),error);world.tick(1.f/60,{});
    need(world.snapshot().navigationCursor==0,"empty cursor addition preserves unsigned wrap");

    state=world.snapshot();
    state.actors[0].routeSearch.emplace(level,ContentPosition{64,64},ContentPosition{1800,900},4,2048);
    need(world.restoreSnapshot(state,error),error);
    GameWorld moved=std::move(world);moved.rebind(scene,level);
    auto copy=scene;GameWorld restored;need(restored.load(copy,level,error),error);
    need(restored.restoreSnapshot(state,error),error);
    moved.tick(1.f/60,{});restored.tick(1.f/60,{});
    need(!moved.player().routeSearch||moved.player().routeSearch->capture().result.expanded>0,
        "restored pending search advances after World move/rebind");
    for(int i=0;i<90;++i){moved.tick(1.f/60,{});restored.tick(1.f/60,{});}
    need(encoded(moved,scene)==encoded(restored,copy),"moved and restored worlds have exact save continuation");

    state=moved.snapshot();
    for(auto& actor:state.actors){actor.routeSearch.reset();actor.route.clear();actor.routePoint=0;}
    need(moved.restoreSnapshot(state,error),error);
    const auto clearedCursor=state.navigationCursor;
    for(int i=0;i<60;++i)moved.tick(1.f/60,{});
    need(moved.snapshot().navigationCursor==clearedCursor+60*count,"cleared-search restore resumes empty cursor advance");

    auto genericScene=fixture(true);Level blocked=level;
    blocked.collisionEdges={{600,0,600,1024,false}};GameWorld generic;
    need(generic.load(genericScene,blocked,error),error);
    auto seed=generic.snapshot();seed.actors[1].randomGoal=ContentPosition{800,200};
    need(generic.restoreSnapshot(seed,error),error);bool born=false,advanced=false;
    for(int i=0;i<600;++i) {
        generic.tick(1.f/60,{});const auto& actor=generic.actors()[1];
        born|=actor.routeSearch.has_value();
        advanced|=!actor.route.empty()||(actor.routeSearch&&actor.routeSearch->capture().result.expanded>0);
    }
    need(born&&advanced,"a runtime legacy search is admitted and advanced on subsequent ticks");
    need(!encoded(generic,genericScene).empty(),"runtime legacy state encodes");
    need(generic.load(genericScene,blocked,error),error);
    need(generic.snapshot().navigationCursor==0,"fresh load resets cursor after legacy activity");
    std::cout<<"Legacy navigation scheduler cursor, admission, restore and move checks passed\n";
}
