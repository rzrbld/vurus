#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const std::string& m){if(!b)throw std::runtime_error(m);}
static TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
static TextNode create(std::string name,int delay){TextNode n;n.kind=TextKind::Block;n.key="CREATE";n.children={value("Name",name),value("Delay",std::to_string(delay))};return n;}
static SceneContent scene(){
 SceneContent s;s.mapPath="TEST/AWAKE.3DG";s.player=ObjectId{0};
 for(const auto* name:{"Player","Emitter","Child","Grandchild","Dormant"}) {
  ObjectClass c;c.id={uint32_t(s.classes.size())};c.name=name;c.behaviours={"Fixed"};
  c.properties={value("Managed",c.name=="Dormant"?"0":"1"),value("Bounding-box-size","0"),value("Sub-type",c.name=="Player"?"Vyruz":"Thing")};
  s.classes.push_back(c);
 }
 s.classes[1].properties.push_back(create("Child",16));
 s.classes[2].properties.push_back(create("Grandchild",16));
 for(unsigned cls:{0u,1u,4u}){ObjectPlacement p;p.id={uint32_t(s.objects.size())};p.classId={cls};p.position={1000.f+100.f*p.id.value,1000};p.properties=s.classes[cls].properties;p.behaviours=s.classes[cls].behaviours;s.objects.push_back(p);}
 return s;
}
static Level level(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);return l;}
static WorldSnapshot disk(const GameWorld& w){SaveGame s;s.currentLevel="TEST/AWAKE.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t> data;std::string error;SaveGame out;need(encodeSave(s,data,error)&&decodeSave(data,out,error),error);return out.levels.at(s.currentLevel).world;}
int main(){try{
 auto s=scene();const auto l=level();GameWorld w;std::string error;need(w.load(s,l,error),error);
 auto start=w.snapshot();need(start.nativeAwake&&start.nativeAwake->manager.entries==std::vector<uint64_t>{0,1},"only Managed factory bodies enroll");
 w.tick(.016f,{});
 need(s.objects.size()==5,"newborn Child visits and emits Grandchild in the same live pass");
 need(w.actors()[0].ageMs==16&&w.actors()[1].ageMs==16&&w.actors()[2].ageMs==0&&w.actors()[3].ageMs==16&&w.actors()[4].ageMs==16,"same-pass age follows mixed factory enrollment; dormant body does not age");
 auto checkpoint=disk(w);auto other=scene();GameWorld resumed;need(resumed.load(other,l,error)&&resumed.restoreSnapshot(checkpoint,error),error);
 need(resumed.snapshot().nativeAwake->manager.entries==checkpoint.nativeAwake->manager.entries,"disk preserves exact mixed sparse order");
 for(unsigned n=0;n<8;++n){w.tick(.017f,{});resumed.tick(.017f,{});need(w.snapshot().nativeAwake->manager.entries==resumed.snapshot().nativeAwake->manager.entries,"continued sparse manager order");for(size_t id=0;id<w.actors().size();++id)need(w.actors()[id].ageMs==resumed.actors()[id].ageMs,"continued per-body age");}
 //43f6d7 selects one full-manager OR player-only pass. Skipped bodies do not age.
 need(w.debugSetSurprise(SurpriseKind::TimeWarp),"activate Time Warp");
 for(unsigned n=0;n<6;++n)w.tick(.1f,{});
 for(unsigned n=0;n<12;++n){const auto before=w.snapshot();const bool full=before.surprise.timeWarpPhase==0;w.tick(.016f,{});
  need(w.actors()[0].ageMs==before.actors[0].ageMs+16,"Time Warp always visits controlled player");
  for(size_t id:{size_t(1),size_t(3),size_t(4)})need(w.actors()[id].ageMs==before.actors[id].ageMs+(full?16:0),"Time Warp chooses one pass; no duplicate/prebatched ages");
 }
 auto corrupt=w.snapshot();corrupt.nativeAwake->actors[0]=corrupt.nativeAwake->manager.entries.size();need(!w.restoreSnapshot(corrupt,error),"invalid cached index rejected");
 corrupt=w.snapshot();corrupt.nativeAwake.reset();corrupt.nativePlayerFacing->pose=49;need(!w.restoreSnapshot(corrupt,error),"facing validated even without awake state");
 //440a3f conversion reaches CREATE/fire/Generator tails on health and lifetime death.
 for(bool expired:{false,true}){
  auto towerScene=scene();auto& cls=towerScene.classes[1];
  cls.properties.push_back(value("Sub-type","Turret"));cls.properties.push_back(value("Two-pieces","1"));
  if(expired)cls.properties.push_back(value("Life-time","16"));
  TextNode generator;generator.kind=TextKind::Block;generator.key="TYPE";generator.arguments={"Generator"};
  generator.children={value("Product","Grandchild"),value("Max-births","1"),value("Active","1"),value("Birth-delay","1000")};
  cls.properties.push_back(generator);towerScene.objects[1].properties=cls.properties;
  GameWorld tower;need(tower.load(towerScene,l,error),error);auto dying=tower.snapshot();
  dying.actors[1].nativeFireRequested=true;if(!expired){dying.actors[1].nativeDamagePending=true;dying.actors[1].vitals.energy=0;}
  need(tower.restoreSnapshot(dying,error),error);tower.tick(.016f,{});
  need(tower.actors()[1].alive&&tower.actors()[1].turretOnlyBase,"upper tower converts rather than final release");
  need(tower.actors()[1].creates[0].done&&tower.actors()[1].generators[0].birthCount==1,"conversion runs due CREATE and Generator in its current visit");
  need(tower.actors()[1].nativeFireRequested,"conversion retains authored fire-request flag for its common tail");
 }
 // Parser setter ordering is established by original40adf4/40bb93.
 TextNode item;item.kind=TextKind::Block;item.key="TYPE";item.arguments={"Item"};item.children={value("Timeout","1")};
 need(nativeClassManaged({value("Managed","0"),item}),"nonzero Item Timeout sets Managed");
 need(!nativeClassManaged({item,value("Managed","0")}),"later explicit Managed clears implicit setter");
 std::cout<<"PASS live awake factory births, ages, dormant admission, disk and validation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
