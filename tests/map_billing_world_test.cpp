#include "game/world.h"
#include "game/save_state.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={v};return n;}
struct Run {
 SceneContent scene;Level level;GameWorld world;std::string error;
 Run(){scene.mapPath="TEST/MAP.3DG";ObjectClass cls;cls.id={0};cls.name="Player";cls.behaviours={"Player"};cls.properties={value("Sub-type","Vyruz"),value("Managed","1"),value("Bounding-box-size","25")};scene.classes={cls};ObjectPlacement p;p.id={0};p.classId={0};p.position={1000,1000};p.properties=cls.properties;p.behaviours=cls.behaviours;scene.objects={p};scene.player=ObjectId{0};level.mapW=100;level.mapH=100;need(world.load(scene,level,error),error.c_str());world.setDebugGodMode(true);}
 void cash(int64_t money){auto carry=world.carry();carry.vitals.money=money;world.restore(carry);}
};
int main(){try{
 for(auto kind:{SurpriseKind::None,SurpriseKind::Circular,SurpriseKind::TimeWarp})for(int hz:{60,90}){
  Run r;if(kind!=SurpriseKind::None){need(r.world.debugSetSurprise(kind),"effect admission");for(int i=0;i<hz;++i)r.world.tick(1.f/hz,{});need(r.world.surprise().active(kind),"effect applied");}
  r.cash(25000);r.world.toggleMap();r.world.takeAudioEvents();for(int i=0;i<hz;++i)r.world.tick(1.f/hz,{});
  need(r.world.player().vitals.money==23200&&r.world.mapOpen(),"map bills1800/sec independently of player visit");
  auto events=r.world.takeAudioEvents();need(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind==AudioEventKind::MapCash;})==1,"one explicit cash boundary sound");
 }
 {Run r;r.cash(21000);r.world.toggleMap();r.cash(30);r.world.tick(1.f/60,{});need(r.world.mapOpen()&&r.world.player().vitals.money==0,"final affordable quantum stays open");r.world.tick(1.f/60,{});need(!r.world.mapOpen(),"next visit closes");auto events=r.world.takeAudioEvents();need(std::any_of(events.begin(),events.end(),[](const auto& e){return e.kind==AudioEventKind::Voice&&e.name=="V-NEED-CASH.WAV"&&e.tailMs==5000;}),"exhaustion speech");need(r.world.notifications().entries().back().kind==NoticeKind::MapDenied,"exhaustion notice");}
 {Run r;r.cash(20999);r.world.setHudLanguage(2);r.world.toggleMap();need(!r.world.mapOpen()&&r.world.player().vitals.money==20999,"opening denial has no fee");need(r.world.notifications().entries().back().text.find("pour voir le Plan")!=std::string::npos,"native French denial");}
 std::cout<<"PASS stable60/90Hz map billing under ordinary/Circular/TimeWarp,5000 sound, final-quantum/next-visit close and French denial\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
