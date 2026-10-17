#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
namespace {
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
TextNode prop(std::string key,std::string value){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={value};return n;}
TextNode type(std::string name,std::vector<TextNode> children){TextNode n;n.kind=TextKind::Block;n.key="TYPE";n.arguments={name};n.children=children;return n;}
TextNode have(std::string name){TextNode n;n.kind=TextKind::Bare;n.key="HAVE";n.arguments={name};return n;}
SceneContent fixture(std::string path="TEST/A.3DG"){
 SceneContent s;s.mapPath=path;
 for(const auto* name:{"Player","Key-Empty","Box","Drop","Surprise"}){ObjectClass c;c.id={uint32_t(s.classes.size())};c.name=name;c.behaviours={c.id.value==0?"Player":"Fixed"};c.properties={prop("Bounding-box-size","10")};s.classes.push_back(c);}
 s.classes[0].properties.push_back(prop("Managed","1"));s.classes[1].properties.push_back(prop("Managed","1"));
 s.classes[0].properties.push_back(have("Drop"));s.classes[2].properties.push_back(have("Drop"));
 s.classes[1].properties.push_back(prop("Carry","1"));s.classes[1].properties.push_back(type("Item",{prop("Only-for-player","1"),prop("Reactivate-after","200")}));
 s.classes[4].properties.push_back(prop("Carry","1"));s.classes[4].properties.push_back(type("Item",{prop("Only-for-player","1"),prop("Surprise-type","bonus-triple-fire")}));
 const unsigned classes[]={0,1,1,2,4};
 for(unsigned i=0;i<5;++i){ObjectPlacement o;o.id={i};o.classId={classes[i]};o.position={float(200+i*100),200};o.properties=s.classes[classes[i]].properties;o.behaviours=s.classes[classes[i]].behaviours;if(i==1)o.properties.push_back(prop("Placement-marker","original"));s.objects.push_back(o);}s.player=ObjectId{0};return s;
}
struct Fixture {SceneContent scene;Level level;GameWorld world;std::string error;
 Fixture(std::string path="TEST/A.3DG"):scene(fixture(path)){level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;check(world.load(scene,level,error),error.c_str());}
 void collect(size_t id){const auto p=scene.objects[id].position;world.setPlayerPosition(p.x,p.y);world.movePlayer(0,0);}
 void restore(const WorldSnapshot& s){const bool ok=world.restoreSnapshot(s,error);check(ok,error.c_str());}
};
}
int main(){
 Fixture f;auto initial=f.world.snapshot();initial.actors[1].ageMs=321;initial.actors[1].vitals.energy=37;f.restore(initial);f.collect(1);
 auto collected=f.world.snapshot();check(collected.heldItems.size()==1&&collected.heldItems[0].actor.ageMs==321&&collected.heldItems[0].actor.vitals.energy==37&&propertyText(collected.heldItems[0].object.properties,"Placement-marker")=="original","Carry retains original actor and placement state");
 check(!f.world.actors()[1].alive&&f.world.actors()[1].reactivateMs==-1&&f.scene.objects.size()==6&&!f.world.actors()[5].alive&&f.world.actors()[5].reactivateMs==200,"Retained instance and pending fresh replacement are separate");
 for(int i=0;i<5;++i)f.world.movePlayer(0,0);check(f.world.snapshot().heldItems.size()==1,"Repeated contact does not duplicate held identity");
 f.world.setPlayerPosition(900,400);f.world.tick(.1f,{});f.world.tick(.1f,{});check(f.world.actors()[5].alive&&!f.world.actors()[1].alive&&f.world.snapshot().heldItems[0].actor.ageMs==521,"Managed retained original ages on both visits; replacement stays independent");
 check(propertyText(f.scene.objects[5].properties,"Placement-marker").empty(),"Returning clone uses class defaults");
 SaveGame save;save.currentLevel=f.scene.mapPath;save.levels[save.currentLevel].world=f.world.snapshot();std::vector<uint8_t> bytes;SaveGame disk;check(encodeSave(save,bytes,f.error)&&decodeSave(bytes,disk,f.error),"Held inventory disk round trip");Fixture resumed;resumed.restore(disk.levels.at(save.currentLevel).world);check(resumed.world.snapshot().heldItems[0].actor.ageMs==521,"Disk restores held original state");
 // Generic holders expose the native held-object death branch independently
 // of the player-only pickup admission implemented by the reconstruction.
 Fixture release;release.collect(1);release.collect(2);auto owned=release.world.snapshot();for(auto& h:owned.heldItems)h.holder=3;owned.heldItems[0].actor.ageMs=111;owned.heldItems[1].actor.ageMs=222;owned.objects[3].position={500.75f,300.5f};vyruz_test::synchronizeActor(owned,3);release.restore(owned);const auto before=release.scene.objects.size();release.world.damage(3,100000,0);
 check(release.world.snapshot().heldItems.empty()&&release.scene.objects.size()==before+1,"Death releases held originals and creates only authored HAVE");
 check(release.scene.objects[before].classId.value==3&&release.scene.objects[before].position.x==500,"HAVE uses first shared scatter position");
 check(release.world.actors()[2].alive&&release.world.actors()[1].alive&&release.scene.objects[2].position.x==468&&release.scene.objects[1].position.x==532&&release.scene.objects[2].position.y==300,"Held objects release newest first after HAVE scatter");
 check(release.world.actors()[2].ageMs==222&&release.world.actors()[1].ageMs==111&&propertyText(release.scene.objects[1].properties,"Placement-marker")=="original","Death release preserves original state and properties");
 Fixture death;death.collect(1);death.collect(4);check(death.world.snapshot().heldItems.size()==2&&death.world.surprise().kind==SurpriseKind::Triple,"Player carries ordinary item and pending Surprise");const auto deathCount=death.scene.objects.size();death.world.damage(0,100000,3);check(death.scene.objects.size()==deathCount&&!death.world.actors()[1].alive&&death.world.snapshot().heldItems.size()==1&&death.world.surprise().kind==SurpriseKind::None,"Player death neither releases inventory nor spawns HAVE and cancels retained Surprise");
 Fixture outbound;outbound.collect(1);const auto aState=outbound.world.snapshot();Fixture destination("TEST/B.3DG");destination.world.restore(outbound.world.carry());check(destination.world.snapshot().heldItems.size()==1&&destination.world.actors()[1].alive,"Cross-level carry does not hide same-index foreign placement");Fixture returned;returned.restore(aState);returned.world.restore(destination.world.carry());check(returned.world.snapshot().heldItems.size()==1&&!returned.world.actors()[1].alive,"Returning to source map retains one identity");returned.world.setPlayerPosition(900,400);returned.world.tick(.1f,{});check(returned.world.snapshot().heldItems.size()==1,"Cross-level held identity remains stable on update");
 Fixture gate;auto gateState=gate.world.snapshot();gateState.objects[1].properties.push_back(type("Key",{prop("Id","7")}));gateState.objects[3].position={320,200};gateState.actors[3].gate=GateConfig{GateActive|GateSensor|GateDestroyKey,100,7,{1}};gateState.actors[3].gateState=initializeGate(*gateState.actors[3].gate);gateState.actors[3].gateAnimationFinished=true;vyruz_test::synchronizeActor(gateState,3);gate.restore(gateState);gate.collect(1);check(gate.world.snapshot().heldItems.size()==1&&gate.world.player().heldKeys.size()==1,"Gate fixture retains real keyed object");gate.world.updateGates(100);check(gate.world.snapshot().heldItems.empty()&&gate.world.player().heldKeys.empty()&&!gate.world.actors()[1].alive,"Destroy-key consumes retained item and key bookkeeping together");
 auto invalid=aState;invalid.heldItems.push_back(invalid.heldItems.front());check(!outbound.world.restoreSnapshot(invalid,outbound.error)&&outbound.world.snapshot().heldItems.size()==1,"Duplicate held identity is rejected transactionally");
 std::cout<<"held item lifecycle fixtures passed\n";
}
