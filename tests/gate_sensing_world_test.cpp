#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "snapshot_fixture.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace vyruz;
namespace {
void need(bool v,const std::string& s){if(!v)throw std::runtime_error(s);}
TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
TextNode block(std::string k,std::vector<TextNode> ns,std::string type){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=ns;n.arguments={type};return n;}
SceneContent fixture(){
 SceneContent s;s.mapPath="TEST/GATE-SENSING.3DG";
 for(unsigned c=0;c<3;++c){ObjectClass cls;cls.id={c};cls.name=c==0?"Player":c==1?"Door":"Body";cls.behaviours={c==0?"Player":"Fixed"};cls.sprite.sequence={1};cls.properties={value("Sub-type",c==0?"Vyruz":c==1?"Gate":"Thing"),value("Managed","1"),value("Bounding-box-size","32"),block("Energy",{value("Initial","100"),value("Max","100")},"")};if(c==1)cls.properties.push_back(block("TYPE",{value("Active","1"),value("Sensor","1"),value("Id","9"),value("Delay","100")},"Gate"));s.classes.push_back(cls);}
 for(unsigned id=0;id<5;++id){const unsigned c=id==0?0:id==1?1:2;ObjectPlacement o;o.id={id};o.classId={c};o.position={id==1?1024.f:3000.f,1024};o.properties=s.classes[c].properties;o.behaviours=s.classes[c].behaviours;o.sprite=s.classes[c].sprite;s.objects.push_back(o);}s.player=ObjectId{0};return s;
}
std::vector<uint8_t> encoded(GameWorld& w){SaveGame g;g.currentLevel="TEST/GATE-SENSING.3DG";g.levels[g.currentLevel].world=w.snapshot();std::string e;std::vector<uint8_t>b;need(encodeSave(g,b,e),e);return b;}
WorldSnapshot disk(GameWorld& w){SaveGame g;std::string e;need(decodeSave(encoded(w),g,e),e);return g.levels.at(g.currentLevel).world;}
struct Expected {GateState state;GateOutput output;std::optional<size_t> authorized;};
Expected original(const WorldSnapshot& s,float dt){
 const auto& gate=s.actors[1];const auto p=s.objects[1].position;std::optional<size_t>a;bool occupied=false;
 // Independent reference: the original eager combined actor scan.
 for(size_t j=0;j<s.actors.size();++j){const auto& actor=s.actors[j];if(j==1||!actor.alive)continue;const auto q=s.objects[j].position;
  if(actor.solid&&nativeDistanceSquared(p,q)<(gate.radius+actor.radius)*(gate.radius+actor.radius))occupied=true;
  if(j!=s.player&&!actor.enemy)continue;if(s.objects[j].flags.invisible.value_or(false))continue;
  if(std::abs(static_cast<int>(p.x/64)-static_cast<int>(q.x/64))>2||std::abs(static_cast<int>(p.y/32)-static_cast<int>(q.y/32))>2)continue;
  if(gate.gateState.keyId&&!hasKey(actor.heldKeys,gate.gateState.keyId)&&!actor.keys.contains(gate.gateState.keyId))continue;
  if(!a)a=j;
 }
 auto state=gate.gateState;auto output=advanceGate(*gate.gate,state,{dt,gate.gateAnimationFinished,a.has_value(),occupied});return{state,output,a};
}
void compare(GameWorld& w,const WorldSnapshot& before,float dt){
 const auto e=original(before,dt);w.updateGates(dt);const auto& g=w.actors()[1];
 need(g.gateState.flags==e.state.flags&&g.gateState.keyId==e.state.keyId&&g.gateState.remainingMs==e.state.remainingMs&&g.gateState.sequence.value==e.state.sequence.value,"complete eager gate state");
 need(g.solid==e.output.blocking&&g.gateRevision==before.actors[1].gateRevision+unsigned(e.output.restartAnimation)&&g.gateAnimationFinished==(e.output.restartAnimation?false:before.actors[1].gateAnimationFinished),"gate output tail/revision/animation");
 for(size_t j=0;j<before.actors.size();++j){auto keys=before.actors[j].keys;auto held=before.actors[j].heldKeys;if(e.output.consumeKeyId&&e.authorized==j){keys.erase(e.output.consumeKeyId);consumeKey(held,e.output.consumeKeyId);}need(w.actors()[j].keys==keys&&(w.actors()[j].heldKeys.size()==held.size()&&std::equal(held.begin(),held.end(),w.actors()[j].heldKeys.begin(),[](const auto&a,const auto&b){return a.id==b.id&&a.code==b.code&&a.part==b.part&&a.total==b.total&&a.text==b.text&&!a.held&&!b.held;})),"lowest actor-ID complete-key consumption");}
 const bool notice=e.output.consumeKeyId&&e.authorized==before.player;
 const auto& notes=w.notifications().entries();need(std::count_if(notes.begin(),notes.end(),[](const auto& n){return n.kind==NoticeKind::KeyUsed;})==int(notice),"player key-used notification only after consumption");
}
}
int main()try{
 auto scene=fixture();Level level;level.mapW=level.mapH=100;level.hasTerrainCollision=true;GameWorld w;std::string error;need(w.load(scene,level,error),error);const auto base=w.snapshot();uint64_t checks=0;
 for(uint32_t flags=0;flags<512;++flags)for(unsigned scenario=0;scenario<8;++scenario)for(float remaining:{0.f,16.f,100.f})for(bool finished:{false,true}){
  auto s=base;auto& g=s.actors[1];g.gateState={flags,remaining,scenario==7?0:9,{unsigned((flags&GateOpened)?1:2)}};g.gateAnimationFinished=finished;g.solid=(checks%2)==0;
  for(size_t j=0;j<s.actors.size();++j)if(j!=1){s.actors[j].solid=false;s.actors[j].enemy=j==2||j==3;}
  auto near=[&](size_t j,float x=1024){s.objects[j].position={x,1024};vyruz_test::synchronizeActor(s,j);};
  if(scenario==1){near(0);s.actors[0].heldKeys={{9,true,1,2},{9,true,2,2}};}
  if(scenario==2){near(2);near(3);s.actors[2].keys.insert(9);s.actors[3].keys.insert(9);}
  if(scenario==3){near(0);near(2);s.objects[0].flags.invisible=true;s.actors[0].solid=true;s.actors[0].keys.insert(9);s.actors[2].keys.insert(9);}
  if(scenario==4){near(4,1600);s.objects[4].flags.invisible=true;s.actors[4].solid=true;s.actors[4].radius=600;}
  if(scenario==5){near(4,1088);s.actors[4].solid=true;s.actors[4].radius=32;}
  if(scenario==6){near(4);s.actors[4].keys.insert(9);near(2);s.actors[2].heldKeys={{9,true,1,2}};}
  if(scenario==7){near(2);near(3);s.actors[2].alive=false;vyruz_test::releaseActor(s,2);}
  need(w.restoreSnapshot(s,error),error);compare(w,w.snapshot(),16);++checks;
 }
 for(float dt:{-1.f,0.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){need(w.restoreSnapshot(base,error),error);compare(w,w.snapshot(),dt);++checks;}
 // Real admitted World ticks, animation completion, memory and portable-disk
 // continuation across opening, occupancy and closing. No debug walk adapter.
 auto start=base;start.actors[1].gateState.flags|=GateDestroyKey;start.actors[1].gateAnimationFinished=true;start.objects[0].position={960,1024};vyruz_test::synchronizeActor(start,0);start.actors[0].heldKeys={{9,false,1,1}};
 need(w.restoreSnapshot(start,error),error);auto copy=fixture();GameWorld resumed;need(resumed.load(copy,level,error),error);need(resumed.restoreSnapshot(disk(w),error),error);
 for(unsigned tick=0;tick<40;++tick){
  if(tick==20){w.setPlayerPosition(3000,1024);resumed.setPlayerPosition(3000,1024);}
  w.finishGateAnimation(1,w.actors()[1].gateRevision);resumed.finishGateAnimation(1,resumed.actors()[1].gateRevision);w.tick(.016f,{});resumed.tick(.016f,{});need(encoded(w)==encoded(resumed),"live gate portable continuation");++checks;
  if(tick==4){need(w.actors()[1].gateRevision>1&&w.player().heldKeys.empty(),"live gate opens and consumes key");auto snap=w.snapshot();need(w.restoreSnapshot(snap,error),error);need(resumed.restoreSnapshot(disk(w),error),error);}
 }
 need(w.actors()[1].solid&&!(w.actors()[1].gateState.flags&GateOpened),"live gate closes after departure");
 // Consume a real retained Carry/Key through the existing callback path.
 {auto keyed=fixture();keyed.objects[4].properties.push_back(value("Carry","1"));keyed.objects[4].properties.push_back(block("TYPE",{value("Only-for-player","1")},"Item"));keyed.objects[4].properties.push_back(block("TYPE",{value("Id","9")},"Key"));keyed.objects[4].position={960,1024};GameWorld held;need(held.load(keyed,level,error),error);held.setPlayerPosition(960,1024);held.movePlayer(0,0);need(held.snapshot().heldItems.size()==1&&held.player().heldKeys.size()==1,"real Carry key retained");auto carried=held.snapshot();carried.actors[1].gateState.flags|=GateDestroyKey;carried.actors[1].gateAnimationFinished=true;need(held.restoreSnapshot(carried,error),error);held.updateGates(16);need(held.snapshot().heldItems.empty()&&held.player().heldKeys.empty()&&!held.actors()[4].alive,"gate callback consumes retained original identity");++checks;}
 std::cout<<"PASS "<<checks<<" live gate sensing/order/occupancy/key/continuation controls\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
