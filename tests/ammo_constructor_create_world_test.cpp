#include "game/world.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static TextNode value(std::string k,std::string v){TextNode n;n.key=std::move(k);n.kind=TextKind::Assignment;n.values={std::move(v)};return n;}
static TextNode block(std::string k,std::vector<TextNode> c,std::string type={}){TextNode n;n.key=std::move(k);n.kind=TextKind::Block;n.children=std::move(c);if(!type.empty())n.arguments={std::move(type)};return n;}
static SceneContent fixture(int delay,int repeat,bool current){
 SceneContent s;s.mapPath="TEST/AMMO_CONSTRUCTOR.3DG";s.player=ObjectId{0};
 for(const auto* name:{"Player","Gun","Bullet","Trail","Smoke"}){ObjectClass c;c.id={uint32_t(s.classes.size())};c.name=name;c.behaviours={"Fixed"};c.properties={value("Managed","1")};s.classes.push_back(c);}
 s.classes[0].behaviours={"Player"};s.classes[0].properties.insert(s.classes[0].properties.end(),{value("Bounding-box-size","25"),value("Altitude","24"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("ADD",{value("Name","Gun")},"Weapon"),block("ADD",{value("Name","Bullet"),value("Count","100"),value("Max","100")},"Ammo")});
 s.classes[1].properties.push_back(block("TYPE",{value("Ammo","Bullet"),value("Delay","1000"),value("Start-distance","250"),value("Fire-altitude","100"),value("Smoke-fx","Smoke"),value("Smoke-altitude","3")},"Weapon"));
 s.classes[2].behaviours={"Straight"};s.classes[2].properties.insert(s.classes[2].properties.end(),{value("Altitude","8"),value("Speed-max","0"),block("TYPE",{value("Ignore-owner","1"),value("Damage-after","5000"),value("On-hit","1")},"Ammo"),block("CREATE",{value("Name","Trail"),value("Delay",std::to_string(delay)),value("Repeat-timeout",std::to_string(repeat)),value("At-current-position",current?"1":"0"),value("Trail-step","4"),[]{auto n=value("Offset","1.25");n.values.push_back("-2.5");return n;}()})});
 for(size_t i:{3u,4u})s.classes[i].properties.insert(s.classes[i].properties.end(),{value("Altitude","7"),value("Bounding-box-size","0"),value("Life-time","5000"),block("TYPE",{},"FX")});
 ObjectPlacement p;p.id={0};p.classId={0};p.position={1000,1000};p.properties=s.classes[0].properties;p.behaviours=s.classes[0].behaviours;s.objects={p};return s;
}
static std::vector<uint8_t> disk(const GameWorld& w,const std::string& path){SaveGame s;s.currentLevel=path;s.levels[path].world=w.snapshot();std::vector<uint8_t>b;std::string e;need(encodeSave(s,b,e),e.c_str());return b;}
int main(){try{
 unsigned cases=0,ticks=0;
 for(int delay:{0,5})for(int repeat:{0,10,100})for(bool current:{false,true}){
 auto s=fixture(delay,repeat,current);const auto original=s;Level l;l.mapW=100;l.mapH=100;l.hasTerrainCollision=true;GameWorld w;std::string e;need(w.load(s,l,e),e.c_str());w.setDebugEnemyAI(false);w.setDebugGodMode(true);w.setPlayerDirection(9);
 WorldInput in;in.fire=true;w.tick(.014f,in);
 need(w.projectiles().size()==1,"one emitted Ammo");const auto& p=w.projectiles().front();need(p.visual.has_value(),"CREATE Ammo retains existing visual adapter");const auto visual=*p.visual;need(visual==1,"parent identity before constructor products");need(w.actors()[visual].creates.empty(),"constructor schedules transferred exactly once");need(p.creates.size()==1,"one runtime schedule transferred");
 const bool ctor=delay==0,first=delay>0||repeat==10;const size_t smoke=ctor?3:2;
 need(s.objects.size()==2+size_t(ctor)+1+size_t(first),"constructor plus same-pass due births, without duplicate one-shot");need(s.objects[smoke].classId.value==4,"Smoke follows parent constructor and its products");need(w.actors()[smoke].projectileAltitude==29,"Smoke final altitude follows emitter overwrite");
 const auto state=w.snapshot();need(state.nativeAwake&&state.actorCells,"native scheduler and cell provenance retained");const auto& awake=*state.nativeAwake;need(awake.manager.entries.size()>=s.objects.size(),"new bodies enrolled in live manager");
 for(size_t id=0;id<s.objects.size();++id){need(awake.actors[id].has_value(),"all managed births scheduled");need(*awake.actors[id]==id,"factory awake ordering parent then child then Smoke then runtime child");need(w.actors()[id].ageMs==14,"appended products receive same-pass own visit");if(id>visual)need(!s.objects[id].owner,"independent CREATE and Smoke products remain ownerless");}
 need(s.objects[visual].owner&&s.objects[visual].owner->value==0,"creator captured on emitted Ammo");need(p.nativeOwnerGeneration==w.actors()[0].nativeGeneration,"late creator generation retained");need(p.slopeAltitude==100,"parent FireAltitude retained");
 if(ctor){need(s.objects[2].classId.value==3,"immediate constructor child");need(w.actors()[2].projectileAltitude==8,"constructor child uses Ammo class altitude before FireAltitude");need(!w.actors()[2].trailSegment,"constructor has no native runtime trail history");need(s.objects[2].position.x==p.position.x+1.25f&&s.objects[2].position.y==p.position.y-2.5f,"constructor retains native floating CREATE offset");need(state.actorCells->arrivalOrder[2]<state.actorCells->arrivalOrder[visual],"constructor child cell precedes parent cell");}
 need(state.actorCells->arrivalOrder[visual]<state.actorCells->arrivalOrder[smoke],"parent cell precedes Smoke cell");
 if(first){const auto child=smoke+1;need(s.objects[child].classId.value==3,"due CREATE emitted on parent's same-pass visit");need(w.actors()[child].projectileAltitude==100,"runtime child uses final launch altitude");need(s.objects[child].position.x==p.position.x+1.25f&&s.objects[child].position.y==p.position.y-2.5f,"runtime retains native floating CREATE offset");need(state.actorCells->arrivalOrder[smoke]<state.actorCells->arrivalOrder[child],"runtime child cell follows Smoke");need(bool(w.actors()[child].trailSegment)==current,"preserve accepted current-position first-segment presentation adapter");}
 const auto& c=p.creates[0];need(c.done==(repeat==0),"constructor one-shot done transferred, delayed one-shot completes on own visit");need(c.nextMs==(delay+repeat*(int(ctor)+int(first))),"constructor and same-pass repeat timers advance once each");
 SaveGame decoded;need(decodeSave(disk(w,s.mapPath),decoded,e),e.c_str());auto clone=original;GameWorld resumed;need(resumed.load(clone,l,e)&&resumed.restoreSnapshot(decoded.levels.at(s.mapPath).world,e),e.c_str());resumed.setDebugEnemyAI(false);resumed.setDebugGodMode(true);
 for(int i=0;i<20;++i){w.tick(.014f,{});resumed.tick(.014f,{});need(disk(w,s.mapPath)==disk(resumed,s.mapPath),"constructor/runtime schedule deterministic disk continuation");++ticks;}
 ++cases;
 }
 std::cout<<"Ammo constructor + real same-pass World controls="<<cases<<" disk ticks="<<ticks<<" passed\n";
 return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
