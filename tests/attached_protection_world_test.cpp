#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <stdexcept>
#include <iostream>
using namespace vyruz;
static void need(bool b,const std::string& m){if(!b)throw std::runtime_error(m);}
static TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
static SceneContent scene(){
 SceneContent s;s.mapPath="TEST/PROTECTION.3DG";s.player=ObjectId{0};
 ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Fixed"};player.properties={value("Managed","1"),value("Sub-type","Vyruz"),value("Bounding-box-size","0")};
 ObjectClass pc;pc.id={1};pc.name="PC";pc.behaviours={"Fixed"};pc.properties={value("Managed","0"),value("Sub-type","Thing"),value("Bounding-box-size","0")};
 TextNode shield;shield.kind=TextKind::Block;shield.key="Shield";shield.children={value("Initial","20000"),value("Max","20000")};pc.properties.push_back(shield);
 TextNode add;add.kind=TextKind::Block;add.key="ADD";add.arguments={"FX"};add.children={value("Name","ShieldFX")};pc.properties.push_back(add);
 ObjectClass fx;fx.id={2};fx.name="ShieldFX";fx.properties={value("Managed","1"),value("Sub-type","FX")};
 TextNode config;config.kind=TextKind::Block;config.key="TYPE";config.arguments={"FX"};config.children={value("check-event","bonus-invulnerability"),value("on-timeout","10000"),value("off-timeout","2000")};fx.properties.push_back(config);
 s.classes={player,pc,fx};
 for(unsigned id:{0u,1u}){ObjectPlacement p;p.id={id};p.classId={id};p.position={1000.f+id*400,1000};p.properties=s.classes[id].properties;p.behaviours=s.classes[id].behaviours;s.objects.push_back(p);}return s;
}
static WorldSnapshot disk(const GameWorld& w){SaveGame save;save.currentLevel="TEST/PROTECTION.3DG";save.levels[save.currentLevel].world=w.snapshot();std::vector<uint8_t> bytes;std::string error;SaveGame restored;need(encodeSave(save,bytes,error)&&decodeSave(bytes,restored,error),error);return restored.levels.begin()->second.world;}
int main(){try{
 auto s=scene();Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);GameWorld w;std::string error;need(w.load(s,l,error),error);
 need(!w.actors()[1].nativeAwake&&w.actors()[1].timedProtection.config.onMs==10000,"unmanaged sleeping parent with managed shield FX");
 for(int ms=20;ms<=24000;ms+=20){
  w.tick(.02f,{});const bool expected=ms%12000<10000;need(w.actors()[1].timedProtection.active==expected,"sleeping owner timer must keep cycling at "+std::to_string(ms));
  if(ms==20||ms==10000||ms==12000){const auto shield=w.actors()[1].vitals.shield;w.damage(1,25,0);need(w.actors()[1].vitals.shield==shield-(expected?0:25),"actual immunity follows independent timer");}
  if(ms==9980||ms==11980){const auto saved=disk(w);need(w.restoreSnapshot(saved,error),error);}
 }
 need(!w.actors()[1].nativeAwake,"parent remains asleep independently of FX");
 need(w.debugSetSurprise(SurpriseKind::TimeWarp),"Time Warp");
 for(int n=0;n<6;++n)w.tick(.1f,{});
 need(w.snapshot().surprise.active(SurpriseKind::TimeWarp),"Time Warp has passed its activation delay");
 for(int n=0;n<20;++n){const auto before=w.snapshot();const bool full=!before.surprise.active(SurpriseKind::TimeWarp)||before.surprise.timeWarpPhase==0;w.tick(.02f,{});
  need(w.actors()[1].timedProtection.onRemainingMs==before.actors[1].timedProtection.onRemainingMs-(full?20:0),"attached FX respects full versus player-only manager pass");}
 need(w.debugSetSurprise(SurpriseKind::None),"release Time Warp");
 while(w.actors()[1].timedProtection.active)w.tick(.02f,{});
 w.damage(1,1e9f,0);need(!w.actors()[1].alive,"lethal damage removes unprotected parent");
 const auto timer=w.actors()[1].timedProtection;w.tick(.02f,{});
 need(w.actors()[1].timedProtection.onRemainingMs==timer.onRemainingMs&&w.actors()[1].timedProtection.offRemainingMs==timer.offRemainingMs,"removed parent stops protection");
 std::cout<<"PASS independent attached protection: sleeping owner, damage,24s cycles, disk, Time Warp and dead owner\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
