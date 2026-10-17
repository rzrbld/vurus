#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={v};return n;}
static TextNode block(std::string key,std::vector<TextNode> ns,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=std::move(ns);if(!type.empty())n.arguments={type};return n;}
static SceneContent fixture(){
 SceneContent s;s.mapPath="TEST/TANK.3DG";
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={block("Energy",{value("Initial","1000"),value("Max","1000")})};
 ObjectClass tank;tank.id={1};tank.name="Tank";tank.behaviours={"Random","Tracker"};tank.properties={value("Sub-type","Tank"),value("Reward","17"),value("Bounding-box-size","30"),value("Two-pieces","1"),value("Can-turn-on-place","1"),value("Turn-timeout","150"),value("Turret-turn-timeout","50"),value("Speed-max","35"),value("Shield-up-step","3"),block("Weight",{value("Initial","100")}),block("Energy",{value("Initial","80"),value("Max","80")}),block("Shield",{value("Initial","200"),value("Max","200")}),block("TYPE",{value("Target","Player"),value("Sensor-Range","500"),value("Min-distance","400")},"Tracker"),block("ADD",{value("Name","Gun")},"Weapon"),block("ADD",{value("Name","Bullet"),value("Count","100"),value("Max","100")},"Ammo")};
 TextNode have;have.key="HAVE";have.arguments={"Drop"};tank.properties.push_back(have);
 ObjectClass drop;drop.id={2};drop.name="Drop";drop.properties={value("Sub-type","Item"),block("TYPE",{value("Only-for-player","1")},"Item")};
 ObjectClass gun;gun.id={3};gun.name="Gun";gun.properties={block("TYPE",{value("Ammo","Bullet"),value("Delay","10"),value("Fire-Range","1000")},"Weapon")};
 ObjectClass ammo;ammo.id={4};ammo.name="Bullet";ammo.properties={value("Speed-max","100"),block("TYPE",{value("damage","1")},"Ammo")};
 s.classes={player,tank,drop,gun,ammo};
 for(unsigned i=0;i<2;++i){ObjectPlacement o;o.id={i};o.classId={i};o.position={100.f+500*i,100};o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;s.objects.push_back(o);}s.player=ObjectId{0};
 s.objects[1].properties.push_back(value("Energy","5"));s.objects[1].properties.push_back(value("Shield","0"));return s;
}
int main(){try{
 auto s=fixture();Level level;level.mapW=level.mapH=100;level.hasTerrainCollision=true;std::string e;GameWorld world;need(world.load(s,level,e),e.c_str());
 auto moving=world.snapshot();auto&a=moving.actors[1];a.direction=6;a.enemyTurn=initializeEnemyTurn(a.enemyTurnConfig,6,18);a.contactVelocity={3.5f,-2.25f};a.enemyThrust=25;a.enemyMagnitude=4;a.enemyMoveCommand=200;a.route={{650,150}};a.routePoint=0;a.routeGoal=ContentPosition{650,150};
 need(world.restoreSnapshot(moving,e),e.c_str());world.damage(1,1000,0,true);
 const auto&base=world.actors()[1];need(base.alive&&base.solid&&base.turretOnlyBase&&propertyBool(s.objects[1].properties,"Only-base"),"Tank top death retains live solid Only-base hull");
 need(base.vitals.energy==80&&base.vitals.shield==200,"Tank hull restores class initial health/shield rather than placement override");
 need(base.brain.base==BrainBase::Fixed&&!base.brain.seeker&&!base.brain.tracker&&!base.brainState.acquired,"Tank hull becomes Fixed without target acquisition");
 need(base.route.empty()&&base.routePoint==0&&!base.routeGoal&&!base.routeSearch,"Tank top death cleans generic navigation action");
 need(base.contactVelocity.x==3.5f&&base.contactVelocity.z==-2.25f&&base.enemyThrust==25&&base.enemyMoveCommand==200,"Tank transition preserves native immediate momentum/thrust/command");
 need(base.direction==6&&base.enemyTurn->bodyPose==7&&base.enemyTurn->gunPose==19,"Tank transition retains independent hull/gun turn state");
 need(base.weapons.size()==1&&base.ammo.find("bullet")->second.count==100,"Tank transition retains weapon and ammunition objects");
 need(s.objects.size()==2&&world.enemiesRemaining()==0&&world.snapshot().bonusEnemies==0,"Tank enemy count drops at top death while drops stay held");
 bool last=false;for(const auto&n:world.notifications().entries())last|=n.text.find("Real Killer")!=std::string::npos;need(last,"last-enemy message occurs at Tank top death");
 need(world.playerKills()==1,"Tank top is one owner kill statistic");
 need(world.player().vitals.money==100017&&world.player().vitals.lifetimeMoney==100017&&world.snapshot().fastKillCount==1,"Tank top credits one clear bonus, ordinary reward and one radial Fast Kill victim");
 SaveGame save;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=world.snapshot();std::vector<uint8_t> bytes;SaveGame disk;need(encodeSave(save,bytes,e)&&decodeSave(bytes,disk,e),e.c_str());auto resumedScene=fixture();GameWorld resumed;need(resumed.load(resumedScene,level,e)&&resumed.restoreSnapshot(disk.levels.at(s.mapPath).world,e),e.c_str());
 need(resumed.actors()[1].turretOnlyBase&&resumed.actors()[1].alive&&resumed.actors()[1].solid&&!resumed.actors()[1].hudEnemy&&!resumed.actors()[1].brain.tracker,"checkpoint derives live Fixed Tank hull without restoring its gun");
 need(resumed.actors()[1].enemyTurn->bodyPose==7&&resumed.actors()[1].enemyTurn->gunPose==19,"checkpoint retains hidden gun and surviving hull pose");
 for(GameWorld*w:{&world,&resumed}){
  w->damage(1,50,0,true);need(w->actors()[1].alive&&w->actors()[1].vitals.shield==150,"Tank hull takes additional damage after top death");
  const auto shots=w->shotsFired;for(int n=0;n<20;++n)w->tick(.014f,{});need(w->shotsFired==shots&&w->actors()[1].alive&&w->actors()[1].vitals.shield==150,"Fixed Tank hull neither fires nor resumes authored shield regeneration");
  const auto combo=w->snapshot().fastKillCount;w->damage(1,1000,0,true);need(!w->actors()[1].alive&&!w->actors()[1].solid&&w->enemiesRemaining()==0,"second lethal Tank hull hit removes the actor");
  need(w->playerKills()==1,"Tank hull death is excluded from owner kill statistic");
  need(w->snapshot().fastKillCount==combo&&w->snapshot().bonusEnemies==0,"Tank hull death cannot count as another enemy/Fast Kill victim");
  need(w->player().vitals.money==100034&&w->player().vitals.lifetimeMoney==100034,"Tank hull death pays only second ordinary Reward, without repeating clear bonus");need(w->snapshot().objects.size()==3,"Tank held HAVE releases exactly once at second death");
 }
 auto authored=fixture();authored.objects[1].properties.push_back(value("Only-base","1"));GameWorld initialized;need(initialized.load(authored,level,e),e.c_str());need(!initialized.actors()[1].turretOnlyBase,"native Tank graphic initialization clears an authored Only-base request");
 auto ordinary=fixture();ordinary.objects[1].properties.push_back(value("Two-pieces","0"));GameWorld single;need(single.load(ordinary,level,e),e.c_str());single.damage(1,1000,0,true);need(!single.actors()[1].alive,"non Two-pieces Tank remains single phase");
 std::cout<<"Tank native two-phase hull lifetime, reset/cleanup/momentum, rewards/drops and checkpoint continuation passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
