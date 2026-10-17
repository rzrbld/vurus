#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_state.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode val(std::string key,std::string value){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={value};return n;}
static TextNode block(std::string key,std::vector<TextNode> fields,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=fields;if(!type.empty())n.arguments={type};return n;}
struct Run {
    SceneContent scene;Level level;GameWorld world;std::string error;
    Run(){
        scene.mapPath="TEST/AUDIO_PRECISION.3DG";
        ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};
        player.properties={val("Sub-type","Vyruz"),val("Bounding-box-size","25"),block("Energy",{val("Initial","1000"),val("Max","1000")}),block("Shield",{val("Initial","1000"),val("Max","1000")})};
        ObjectClass enemy;enemy.id={1};enemy.name="Enemy";enemy.behaviours={"Fixed"};enemy.properties={val("Sub-type","Cop"),val("Bounding-box-size","8"),block("Energy",{val("Initial","1000"),val("Max","1000")})};
        ObjectClass ammo;ammo.id={2};ammo.name="Bullet";ammo.properties={block("TYPE",{},"Ammo")};
        scene.classes={player,enemy,ammo};
        for(unsigned c=0;c<2;++c){ObjectPlacement o;o.id={c};o.classId={c};o.position=c?ContentPosition{1000,1000}:ContentPosition{200,200};o.properties=scene.classes[c].properties;o.behaviours=scene.classes[c].behaviours;scene.objects.push_back(o);}
        scene.player=ObjectId{0};level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;
        need(vyruz_test::loadManagedFixture(world,scene,level,error),error);world.setDebugEnemyAI(false);world.takeAudioEvents();
    }
    void restore(WorldSnapshot s){need(world.restoreSnapshot(s,error),error);world.setDebugEnemyAI(false);world.takeAudioEvents();}
    std::vector<AudioEvent> voices(){auto v=world.takeAudioEvents();std::erase_if(v,[](const auto& e){return e.kind!=AudioEventKind::Voice;});return v;}
};
static void injury(const std::vector<AudioEvent>& events,unsigned residue,bool expected){
    const char* names[]={"V-OOH.WAV","V-LOOSER.WAV","V-SAD-MAN.WAV"};
    auto voices=events;std::erase_if(voices,[](const auto& e){return e.name!="V-OOH.WAV"&&e.name!="V-LOOSER.WAV"&&e.name!="V-SAD-MAN.WAV";});
    need(voices.size()==unsigned(expected && residue<3),"injury selected/silent voice count");
    if(!voices.empty())need(voices[0].name==names[residue]&&voices[0].delayMs==500&&voices[0].holdMs==2000&&voices[0].tailMs==5000,"injury authored name/timing tuple");
}
int main(){try{
    for(unsigned residue=0;residue<8;++residue)for(bool projectile:{false,true}) {
        Run r;auto state=r.world.snapshot();state.elapsedMs=1008+residue-(projectile?62.5:0);
        if(projectile){
            Projectile p;p.classId={2};p.owner=1;p.position=r.world.playerPosition();p.damage=300;p.damageRadius=20;p.fuseMs=1;p.damageRemainingMs=1;p.phaseInitialized=true;p.ignoreOwner=true;
            nativeAmmoOwnerLaunched(state.actors[1].nativeAmmoOwner);vyruz_test::appendProjectile(state,r.scene,p);
        }
        r.restore(state);
        if(projectile)r.world.tick(.0625f,{});else r.world.damage(0,300,1,true,true);
        need(r.world.player().vitals.energy==1000&&std::abs(r.world.player().vitals.shield-700)<.01f,"shield absorption leaves player alive");
        injury(r.voices(),residue,true);
    }
    for(float amount:{299.f,300.f,1300.f,2000.f})for(bool radial:{false,true}){
        Run r;auto state=r.world.snapshot();state.elapsedMs=1008;r.restore(state);
        r.world.damage(0,amount,1,radial,true);
        injury(r.voices(),0,radial&&amount>=300&&amount<2000);
    }
    {Run r;r.world.setDebugGodMode(true);r.world.damage(0,300,1,true,true);injury(r.voices(),0,false);need(r.world.player().vitals.shield==1000,"god-mode health unchanged");}
    {Run r;auto state=r.world.snapshot();state.actors[0].indestructible=true;r.restore(state);r.world.damage(0,300,1,true,true);injury(r.voices(),0,false);}
    {Run r;r.world.damage(1,300,0,true,true);injury(r.voices(),0,false);}
    {Run r;
        need(!r.world.takeAudioInput(),"new World has no input audio signal");
        WorldInput input;input.movement.forward=1;r.world.tick(.014f,input);
        need(r.world.takeAudioInput()==std::optional<bool>{false},"movement-only is native quiet input");
        need(!r.world.takeAudioInput(),"input signal drains once");
        input.fire=true;r.world.tick(.014f,input);need(r.world.takeAudioInput()==std::optional<bool>{true},"held fire produces signal even without usable weapon");
        r.world.tick(.014f,input);auto state=r.world.snapshot();r.restore(state);need(!r.world.takeAudioInput(),"successful snapshot restore clears signal");
        r.world.tick(.014f,input);auto carry=r.world.carry();r.world.restore(carry);need(!r.world.takeAudioInput(),"carry restore clears signal");
        state=r.world.snapshot();state.surprise.kind=SurpriseKind::Motor;state.surprise.applied=true;state.surprise.remainingMs=1000;r.restore(state);
        r.world.tick(.014f,input);need(r.world.takeAudioInput()==std::optional<bool>{false},"motor block returns native quiet input");
        state=r.world.snapshot();state.surprise.kind=SurpriseKind::Circular;state.surprise.circularRemaining=2;r.restore(state);
        r.world.tick(.014f,input);need(!r.world.takeAudioInput(),"special repeated Circular Fire bypasses ordinary input");
        state=r.world.snapshot();state.surprise={};state.actors[0].vitals.energy=0;state.actors[0].alive=false;vyruz_test::synchronizeActor(state,0);r.restore(state);
        r.world.tick(.014f,input);need(!r.world.takeAudioInput(),"dead player has no ordinary input signal");
    }
    std::cout<<"PASS live radial injury speech, threshold/survival/immunity and ordinary input visit signals\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
