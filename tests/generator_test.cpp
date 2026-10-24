#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
TextNode prop(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
TextNode type(std::string name,std::vector<TextNode> nodes){TextNode n;n.kind=TextKind::Block;n.key="TYPE";n.arguments={name};n.children=nodes;return n;}
SceneContent fixture(bool stable=true,bool active=true){
 SceneContent s;s.mapPath="TEST/GEN.3DG";ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};p.properties={prop("Bounding-box-size","10")};s.classes.push_back(p);
 ObjectClass g;g.id={1};g.name="Generator";g.properties={prop("Bounding-box-size","30"),type("Generator",{prop("Product","Cop"),prop("Max-births","1"),prop("Birth-delay","300"),prop("Birth-duration","400"),prop("Active",active?"1":"0"),prop("Stressable","1"),prop("Stable-population",stable?"1":"0")})};TextNode energy;energy.key="Energy";energy.children={prop("Initial","100"),prop("Max","100")};g.properties.push_back(energy);s.classes.push_back(g);
 ObjectClass c;c.id={2};c.name="Cop";c.properties={prop("Bounding-box-size","10")};s.classes.push_back(c);
 for(unsigned i=0;i<2;++i){ObjectPlacement o;o.id={i};o.classId={i};o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;o.position={float(200+i*300),200};s.objects.push_back(o);}s.player=ObjectId{0};return s;
}
void advance(GameWorld& w,int n){while(n--)w.tick(.1f,{});}
int main(){Level level;level.mapW=40;level.mapH=40;std::string error;
 auto s=fixture();GameWorld w;check(vyruz_test::loadManagedFixture(w,s,level,error),"load");check(w.actors()[1].solid,"active starts solid");advance(w,1);check(w.actors().size()==3 && w.actors()[2].generatorParent==1 && !w.actors()[1].solid,"immediate authored birth and parent");advance(w,10);check(w.actors().size()==3,"population cap");w.damage(2,100);check(w.actors()[1].generators[0].birthCount==0,"stable death frees slot");advance(w,3);check(w.actors()[1].generators[0].active && w.actors()[1].solid,"delay ends with active duration");advance(w,3);check(w.actors().size()==3,"duration waits");advance(w,1);check(w.actors().size()==4,"replacement born");
 auto blocked=fixture(false,false);GameWorld b;check(vyruz_test::loadManagedFixture(b,blocked,level,error),"inactive load");b.setPlayerPosition(500,200);advance(b,5);check(!b.actors()[1].generators[0].active,"occupied phase waits");b.setPlayerPosition(200,200);advance(b,1);check(b.actors()[1].generators[0].active,"vacancy starts duration");b.damage(1,1);advance(b,2);check(b.actors()[1].generators[0].remainingMs==0,"damage doubles subtraction without early transition");
 SaveGame save;save.currentLevel=blocked.mapPath;save.levels[blocked.mapPath].world=b.snapshot();std::vector<uint8_t> bytes;SaveGame decoded;check(encodeSave(save,bytes,error)&&decodeSave(bytes,decoded,error)&&bytes[8]==37,"generator disk serialization");check(b.restoreSnapshot(decoded.levels.at(blocked.mapPath).world,error),"restore");advance(b,1);check(b.actors().size()==3 && b.actors()[2].generatorParent==1,"save resumes exact birth");b.damage(2,100);advance(b,20);check(b.actors().size()==3 && b.actors()[1].generators[0].birthCount==1,"nonstable cap counts lifetime births");
 auto legacy=b.snapshot();legacy.actors[1].generators.clear();legacy.actors[1].generatorsInitialized=false;check(b.restoreSnapshot(legacy,error)&&b.actors()[1].generators.size()==1,"legacy initial schedule migration");auto bad=b.snapshot();bad.actors[1].generatorParent=999;check(!b.restoreSnapshot(bad,error),"invalid parent rejected");
 auto placed=fixture();placed.objects[1].authored.children.push_back(type("Generator",{prop("Birth-delay","300")}));GameWorld placement;
 check(vyruz_test::loadManagedFixture(placement,placed,level,error),"placement delay load");advance(placement,2);check(placement.actors().size()==2,"placement delay seeds initial countdown");advance(placement,1);check(placement.actors().size()==3,"placement first birth after seeded delay");
 auto dead=fixture();GameWorld d;check(vyruz_test::loadManagedFixture(d,dead,level,error),"dead load");d.damage(1,1000);advance(d,20);check(d.actors().size()==2,"destroyed generator cannot spawn");
 // Ordered multi-product records share the native bounds latch. Inactive
 // records may not start while a preceding record keeps the generator solid.
 auto multi=fixture();auto second=multi.objects[1].properties[1];
 second.children[0]=prop("Product","Other");multi.objects[1].properties.push_back(second);
 auto other=multi.classes[2];other.id={3};other.name="Other";multi.classes.push_back(other);
 GameWorld m;check(vyruz_test::loadManagedFixture(m,multi,level,error),"multi load");advance(m,1);
 check(m.actors().size()==3 && m.actors()[1].generators[1].birthCount==0,"newborn blocks independent product occupancy");
 auto state=m.snapshot();state.objects[2].position={800,500};state.actorCells.reset();state.nativeAmmoLifecycleKnown=false;state.actors[2].nativeMotion.reset();check(m.restoreSnapshot(state,error),"move newborn");advance(m,1);
 check(m.actors().size()==4 && multi.objects[3].classId.value==3,"second product independently births when space clears");
 auto duplicate=fixture();duplicate.objects[1].properties.push_back(duplicate.objects[1].properties[1]);GameWorld du;
 check(vyruz_test::loadManagedFixture(du,duplicate,level,error),"duplicate load");advance(du,1);du.damage(2,100);
 check(du.actors()[1].generators[0].birthCount==0 && du.actors()[1].generators[1].birthCount==-1,"native death decrements every matching stable record");
 auto restoredCap=w.snapshot();check(w.restoreSnapshot(restoredCap,error),"capped restore");advance(w,10);check(w.actors().size()==4,"cap survives restore");
 std::cout<<"generator fixtures passed\n";
}
