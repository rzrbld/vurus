#define main retainedOrdinaryWorldTests
#include "ordinary_enemy_ai_world_test.cpp"
#undef main
#include "game/enemy_idle_native.h"
#include <array>

namespace {
void replace(auto& properties,const std::string& key,const std::string& text){
    for(auto& node:properties)if(node.key==key){node.values={text};return;}
    properties.push_back(value(key,text));
}
SceneContent turnFixture(const std::string& family){
    auto scene=fixture();auto& cls=scene.classes[1];cls.name=family;
    replace(cls.properties,"Sub-type",family);replace(cls.properties,"Speed-max","0");
    replace(cls.properties,"Speed-min","0");
    scene.objects[1].properties=cls.properties;return scene;
}
Level testLevel(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);return l;}
WorldSnapshot following(GameWorld& world){
    world.tick(.017f,{});world.tick(.017f,{});auto state=world.snapshot();
    need(state.actors[1].ordinaryAI->targeting.clock.active,"native route admitted");
    auto& actor=state.actors[1];actor.ordinaryAI->targeting.clock.scanRemainingMs=1000;
    actor.enemyTurn=initializeEnemyTurn(actor.enemyTurnConfig,0,0);
    actor.enemyTurn->bodyRemainingMs=0;return state;
}
}
int main(){try{
    const auto l=testLevel();std::string e;unsigned checks=0,continuationTicks=0;
    // Current/previous SURFACE coordinates deliberately differ from the retained
    // native center. This catches native-space, >=, and post-motion substitutes.
    for(const std::string family:{"Burner","Jeep","Roller","Cop","Walker","Heli"}){
        // Original441367 family mask covers Burner, Jeep and Roller.
        const bool admitted=family=="Burner"||family=="Jeep"||family=="Roller";
        for(const auto delta:std::array<ContentPosition,6>{{{0,0},{.25f,.25f},{.4999f,0},{.5f,0},{.5001f,0},{.5f,.5f}}}){
            auto scene=turnFixture(family);const auto authored=scene;GameWorld world;
            need(world.load(scene,l,e),e);auto state=following(world);
            const auto position=state.objects[1].position;
            state.actors[1].nativeMotion->previousSurface={position.x-delta.x,position.y-delta.y};
            const auto previous=state.actors[1].nativeMotion->previousSurface;
            const long double dx=(long double)position.x-previous.x,dy=(long double)position.y-previous.y;
            auto expected=*state.actors[1].enemyTurn;
            advanceEnemyBodyTurn(state.actors[1].enemyTurnConfig,expected,
                state.actors[1].ordinaryAI->targeting.desiredHeading,false,17,0,0);
            need(expected.turnCommand!=0,"fixture requests a real body turn");
            const int nativeCommand=admitted&&dx*dx+dy*dy<.25L?0:expected.turnCommand;
            need(world.restoreSnapshot(state,e),e);world.tick(.017f,{});const auto& actor=world.actors()[1];
            need(actor.enemyMoveCommand==200,"turn reset preserves forward command");
            need(actor.enemyTurn->turnCommand==nativeCommand,family+" original post-AI surface-distance turn reset");
            need(actor.enemyTurn->bodyPose==expected.bodyPose&&actor.direction==expected.logicalHeading,
                "reset does not undo body pose or propulsion heading");
            need(actor.enemyTurn->bodyRemainingMs==expected.bodyRemainingMs,"countdown retained");
            if(family=="Burner"){
                auto lean=*actor.enemyTurn;lean.lean=-1;advanceEnemyLean(actor.enemyTurnConfig,lean);
                need(lean.lean==(nativeCommand==0?0:-1),"reset lets existing Burner lean relax");
            }
            continuation(world,authored,l,8);continuationTicks+=8;++checks;
        }
    }
    // The native branch covers reverse208 as well as forward200.
    for(const std::string family:{"Burner","Jeep","Roller"}){
        auto scene=turnFixture(family);scene.objects[0].position={1120,1000};const auto authored=scene;
        GameWorld world;need(world.load(scene,l,e),e);auto state=following(world);
        state.actors[1].nativeMotion->previousSurface=state.objects[1].position;
        need(world.restoreSnapshot(state,e),e);world.tick(.017f,{});
        need(world.actors()[1].enemyMoveCommand==208&&world.actors()[1].enemyTurn->turnCommand==0,
            "original reset includes close-target reverse without cancelling reverse");
        continuation(world,authored,l,8);continuationTicks+=8;++checks;
    }
    // A stationary idle turn must survive: native movement command0 goes down
    // the lateral-command branch, even though its previous displacement is zero.
    {
        auto scene=turnFixture("Burner");scene.objects[0].position={5000,2500};GameWorld world;
        need(world.load(scene,l,e),e);auto state=world.snapshot();auto& actor=state.actors[1];
        actor.nativeMotion->previousSurface=state.objects[1].position;
        actor.enemyTurn=initializeEnemyTurn(actor.enemyTurnConfig,0,0);actor.enemyTurn->bodyDesiredPose=5;
        need(world.restoreSnapshot(state,e),e);world.tick(.017f,{});
        need(world.actors()[1].enemyMoveCommand==0&&world.actors()[1].enemyTurn->turnCommand!=0,
            "zero-command stationary idle turning remains admitted");++checks;
    }
    // Nonzero propulsion reaches the gate through ordinary begin/select/steer,
    // without injected route, command or history. Slow startup isolates it.
    {
        auto scene=turnFixture("Burner");auto& p=scene.classes[1].properties;
        for(const auto& [key,text]:{std::pair{"Speed-max","20"},{"Speed-min","-10"},
            {"Use-inertia","1"},{"Grip","1"},{"Friction-factor","0.97"},
            {"Friction-threshold","0"},{"Acceleration-step","0.1"},{"Decceleration-step","0.5"}})
            replace(p,key,text);
        p.push_back(block("Weight",{value("Initial","10")}));
        p.push_back(block("Thrust",{value("Initial","1"),value("Min","-20"),value("Max","20")}));
        scene.objects[1].properties=p;const auto authored=scene;GameWorld world;
        need(world.load(scene,l,e),e);bool cleared=false,moved=false;
        for(int tick=0;tick<12;++tick){
            const auto before=world.snapshot();const auto& actor=before.actors[1];
            const auto position=before.objects[1].position,previous=actor.nativeMotion->previousSurface;
            const long double dx=(long double)position.x-previous.x,dy=(long double)position.y-previous.y;
            world.tick(.017f,{});const auto& after=world.actors()[1];
            moved|=scene.objects[1].position.x!=position.x||scene.objects[1].position.y!=position.y;
            if(actor.ordinaryAI->targeting.clock.active&&actor.ordinaryAI->targeting.clock.scanRemainingMs>0){
                auto expected=*actor.enemyTurn;expected.turnCommand=0;
                const int ms=int(std::floor(world.snapshot().elapsedMs)-std::floor(before.elapsedMs));
                advanceEnemyBodyTurn(actor.enemyTurnConfig,expected,actor.ordinaryAI->targeting.desiredHeading,
                    false,ms,actor.enemyMagnitude,actor.speed);
                if(expected.turnCommand!=0&&after.enemyMoveCommand!=0&&dx*dx+dy*dy<.25L){
                    need(after.enemyTurn->turnCommand==0,"powered Burner clears final low-displacement turn");
                    need(after.enemyTurn->bodyPose==expected.bodyPose&&after.direction==expected.logicalHeading&&
                         after.enemyTurn->bodyRemainingMs==expected.bodyRemainingMs,"powered gate retains body step");
                    cleared=true;
                }
            }
        }
        need(moved&&cleared,"powered startup observes actual movement and original command clear");
        continuation(world,authored,l,20);continuationTicks+=20;++checks;
    }
    std::cout<<"PASS "<<checks<<" live World turn/threshold/subtype/idle controls; "
             <<continuationTicks<<" byte-exact memory/disk continuation ticks\n";
    return 0;
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
