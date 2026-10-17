#include "snapshot_fixture.h"
// Native 43cb20 excludes subtype 0x11 FX from radial damage.
#include "game/world.h"
#include "game/save_state.h"
#include "game/properties.h"
#include "io/vfs.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string text){TextNode n;n.key=key;n.kind=TextKind::Assignment;n.values={text};return n;}
static TextNode block(std::string key,std::vector<TextNode> children,std::string arg={}){TextNode n;n.key=key;n.kind=TextKind::Block;n.children=children;if(!arg.empty())n.arguments={arg};return n;}
static void place(SceneContent& s,ClassId cls,ContentPosition p){ObjectPlacement o;o.id={static_cast<uint32_t>(s.objects.size())};o.classId=cls;o.position=p;o.properties=s.classes[cls.value].properties;o.behaviours=s.classes[cls.value].behaviours;o.sprite=s.classes[cls.value].sprite;o.flags=s.classes[cls.value].flags;s.objects.push_back(o);}
static void verify(SceneContent s,ClassId target,ClassId ammo,ContentReader reader={}){
    s.objects.clear();const auto player=std::find_if(s.classes.begin(),s.classes.end(),[](const auto& c){return std::any_of(c.behaviours.begin(),c.behaviours.end(),[](const auto& b){return folded(b)=="player";});});
    need(player!=s.classes.end(),"player class exists");place(s,player->id,{100,100});place(s,target,{500,500});s.player=ObjectId{0};
    Level level;level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;
    GameWorld world;world.setEffectReader(reader);std::string error;need(vyruz_test::loadManagedFixture(world,s,level,error),error);world.setDebugEnemyAI(false);
    auto saved=world.snapshot();saved.actors[1].vitals.energy=1;saved.actors[1].vitals.shield=0;
    Projectile shot;shot.classId=ammo;shot.position={500,500};shot.damage=400;shot.damageRadius=30;shot.fuseMs=500;shot.phaseInitialized=true;shot.damageRemainingMs=0;shot.ignoreOwner=true;shot.hitEffect="Flame-Smoke";
    vyruz_test::appendProjectile(saved,s,shot);need(world.restoreSnapshot(saved,error),error);
    world.tick(1.f/60,{});need(world.actors()[1].alive&&world.actors()[1].nativeDamagePending&&world.actors()[1].vitals.energy<=0,"flame records lethal health until the target's next actor visit");
    world.tick(1.f/60,{});need(!world.actors()[1].alive&&!world.actors()[1].nativeDamagePending,"target's next actor visit consumes flame death");
    std::vector<size_t> deathFx;
    for(size_t i=2;i<world.actors().size();++i)if(world.actors()[i].explosion){need(world.actors()[i].alive,"death FX survives the killing flame's radial damage");deathFx.push_back(i);}
    need(!deathFx.empty(),"authored death FX spawned");
    saved=world.snapshot();vyruz_test::appendProjectile(saved,s,shot);need(world.restoreSnapshot(saved,error),error);world.tick(1.f/60,{});
    for(const auto id:deathFx)need(world.actors()[id].alive,"death FX survives subsequent flame area damage");
    std::cout<<s.classes[target.value].name<<": "<<deathFx.size()<<" death effects survive two flame impacts\n";
}
int main(int argc,char** argv){try{
    if(argc==2){Vfs vfs;need(vfs.init(argv[1]),vfs.error());ContentReader read=[&](const auto& p,auto& b){return vfs.read(p,b);};
        for(const auto& level:{"CAVE/T1C1.3DG","UNDERGROUND/T3U3.3DG"}){SceneContent s;std::string error;need(loadSceneContent(read,level,s,error),error);const auto* ammo=findClass(s,"Flame-Ammo");need(ammo,"original Flame-Ammo exists");const auto* a=attribute(ammo->properties,"ammo");need(a&&propertyNumber(a->children,"Damage")==400&&propertyNumber(a->children,"Damage-zone")==30,"original flame payload");
            for(const auto& name:{"Caisse-3","Cop","Box-Empty","tree1"})if(const auto* c=findClass(s,name))verify(s,c->id,ammo->id,read);
        }
    }else{
        SceneContent s;s.mapPath="TEST.3DG";
        ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={block("Energy",{value("Initial","1000")})};
        ObjectClass target;target.id={1};target.name="Crate";target.properties={value("Bounding-box-size","10"),block("Energy",{value("Initial","1")}),block("TYPE",{value("Boom-fx","Boom"),value("Damage","0"),value("Damage-zone","0")},"Explosion")};
        ObjectClass fx;fx.id={2};fx.name="Boom";fx.properties={block("Energy",{value("Initial","1")}),block("TYPE",{},"FX")};
        ObjectClass ammo;ammo.id={3};ammo.name="Flame-Ammo";ammo.properties={block("TYPE",{},"Ammo")};
        s.classes={player,target,fx,ammo};verify(s,target.id,ammo.id);
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
