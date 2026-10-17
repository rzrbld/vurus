#include "game/difficulty.h"
#include "game/world.h"
#include "game/save_state.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
static void check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
static TextNode number(const char* key,const char* value){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={value};return n;}
static TextNode block(const char* key,std::vector<TextNode> children,const char* arg=""){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=std::move(children);if(*arg)n.arguments={arg};return n;}
int main(){
    SceneContent scene;std::string error;TextDocument document;
    check(parseContentText(R"(difficulty-level=0
shield-up-item-value-ratio=.5
nrj-up-item-value-ratio=.5
player-shield-max=20000
player-nrj-max=20000
nmy-scan-delay-ratio=1
nmy-scan-range-ratio=0.5
nmy-fire-delay-ratio=2
difficulty-level=1
shield-up-item-value-ratio=1
nrj-up-item-value-ratio=1
player-shield-max=10000
player-nrj-max=10000
nmy-scan-range-ratio=1
nmy-fire-delay-ratio=1
difficulty-level=2
shield-up-item-value-ratio=2
nrj-up-item-value-ratio=2
player-shield-max=10000
player-nrj-max=10000
nmy-scan-range-ratio=2
nmy-fire-delay-ratio=0.5
difficulty-level=3
shield-up-item-value-ratio=3
nrj-up-item-value-ratio=3
player-shield-max=10000
player-nrj-max=10000
nmy-scan-range-ratio=3 # inline authored comment
nmy-fire-delay-ratio=0.33
# trailing default marker does not erase the earlier Normal block
difficulty-level=1
)","DIFFICULTY.3DC",document,error),"parse difficulty fixture");
    scene.classDocuments={document};
    DifficultyPolicy policy;check(loadDifficultyPolicy(scene,0,policy,error)&&policy.playerShieldMax==20000&&policy.enemyFireDelay==2,"easy policy");
    check(loadDifficultyPolicy(scene,1,policy,error)&&policy.playerShieldMax==10000,"trailing default preserves normal policy");
    check(!loadDifficultyPolicy(scene,4,policy,error)&&policy.level==1,"invalid selection rejected transactionally");
    auto malformed=scene;malformed.classDocuments[0].nodes[1].values={"NaN"};check(!loadDifficultyPolicy(malformed,0,policy,error),"nonfinite authored value rejected");
    ObjectClass player;player.id={0};player.name="Player";
    player.properties={block("Energy",{number("Initial","10000"),number("Max","10000")}),block("Shield",{number("Initial","10000"),number("Max","10000")}),block("ADD",{number("Name","Gun")},"Weapon")};
    ObjectClass enemy;enemy.id={1};enemy.name="Cop";enemy.behaviours={"Fixed","Seeker"};
    enemy.properties={block("Energy",{number("Initial","300"),number("Max","300")}),block("TYPE",{number("Sensor-Range","500"),number("Scan-Delay","400")},"Seeker"),block("ADD",{number("Name","Gun")},"Weapon")};
    ObjectClass gun;gun.id={2};gun.name="Gun";gun.properties={block("TYPE",{number("Delay","150")},"Weapon")};
    ObjectClass pickup;pickup.id={3};pickup.name="RepairShield";
    pickup.properties={number("Bounding-box-size","12"),block("TYPE",{number("Only-for-player","1"),number("Add-NRJ","500"),number("Add-Shield","500")},"Item")};
    scene.classes={player,enemy,gun,pickup};
    for(unsigned i=0;i<2;++i){ObjectPlacement o;o.id={i};o.classId={i};o.position={float(100+i*200),100};o.properties=scene.classes[i].properties;o.behaviours=scene.classes[i].behaviours;scene.objects.push_back(o);}
    scene.player=ObjectId{0};Level level;level.mapW=100;level.mapH=100;
    uint64_t hash=0;
    for(int difficulty=0;difficulty<4;++difficulty){
        auto fixture=scene;GameWorld world;check(world.load(fixture,level,error,difficulty),error.c_str());
        check(world.difficulty()==difficulty,"selected runtime difficulty");
        check(world.player().vitals.energy==10000&&world.player().vitals.shield==10000,"easy does not refill initial stats");
        check(world.player().vitals.energyMax==(difficulty==0?20000:10000),"authored max applied");
        const float ranges[]={250,500,1000,1500};const unsigned delays[]={300,150,75,49};
        check(world.actors()[1].brain.sensorRange==ranges[difficulty],"enemy detection scales");
        check(world.actors()[1].weapons[0].config.delayMs==delays[difficulty],"enemy fire delay truncates");
        check(world.player().weapons[0].config.delayMs==150,"player firing remains unchanged");
        DifficultyPolicy parsed;check(loadDifficultyPolicy(fixture,difficulty,parsed,error),"parse item ratios");
        const float ratios[]={.5f,1,2,3};check(parsed.energyItemValue==ratios[difficulty]&&parsed.shieldItemValue==ratios[difficulty],"preserve authored but unused item ratio fields");
        auto depleted=world.snapshot();depleted.actors[0].vitals.energy=100;depleted.actors[0].vitals.shield=100;
        check(world.restoreSnapshot(depleted,error),"depleted difficulty fixture");
        world.spawn({3},world.playerPosition());world.movePlayer(0,0);
        check(world.player().vitals.energy==600&&world.player().vitals.shield==600,"native direct item grants stay500 on every difficulty");
        auto saved=world.snapshot();if(difficulty==0)hash=saved.catalogHash;check(saved.catalogHash==hash,"difficulty never modifies content hash");
        auto restoredScene=scene;GameWorld restored;check(restored.load(restoredScene,level,error),"load normal before restore");
        check(restored.restoreSnapshot(saved,error),error.c_str());check(restored.difficulty()==difficulty,"saved difficulty overrides current preference");
        auto spawned=restored.spawn({1},{300,300});check(restored.actors()[spawned].weapons[0].config.delayMs==delays[difficulty],"future spawns retain saved difficulty");
    }
    // Native parser assignments describe current placement state, independent
    // of class maxima. The original CAVE demo authors both current values20000.
    auto authored=scene;
    authored.objects[0].properties[0]=number("Energy","20000");
    authored.objects[0].properties[1]=number("Shield","20000");
    GameWorld demo;check(demo.load(authored,level,error,1),error.c_str());
    check(demo.player().vitals.energy==20000&&demo.player().vitals.shield==20000,"explicit placement stats survive class max normalization");
    check(demo.player().vitals.energyMax==10000&&demo.player().vitals.shieldMax==10000,"placement override does not increase repair maxima");
    demo.damage(demo.playerIndex(),1000);
    check(demo.player().vitals.shield==19000&&demo.player().vitals.energy==20000,"above-max placement shield takes ordinary damage");
    auto savedDemo=demo.snapshot();check(demo.restoreSnapshot(savedDemo,error),error.c_str());
    check(demo.player().vitals.shield==19000,"above-max current stats survive snapshot continuation");
    std::cout<<"Difficulty policies, actor application and restore passed\n";
}
