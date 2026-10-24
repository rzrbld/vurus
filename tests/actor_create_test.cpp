#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace vyruz;
static void need(bool v,const std::string& s){if(!v)throw std::runtime_error(s);}
static TextNode number(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
static TextNode create(std::string name,int delay,int repeat,bool current=false){TextNode n;n.kind=TextKind::Block;n.key="CREATE";n.children={number("Name",name),number("Delay",std::to_string(delay)),number("Repeat-timeout",std::to_string(repeat)),number("At-current-position",current?"1":"0")};return n;}
static SceneContent fixture(){
 SceneContent s;s.mapPath="TEST/CREATE.3DG";s.player=ObjectId{0};
 for(const auto* name:{"Player","Emitter","Mark","Second"}){ObjectClass c;c.id={uint32_t(s.classes.size())};c.name=name;c.behaviours={"Fixed"};s.classes.push_back(c);}
 s.classes[1].properties={number("Altitude","24"),create("Mark",0,50),create("Second",25,0)};
 for(size_t c=0;c<2;++c){ObjectPlacement o;o.id={uint32_t(c)};o.classId={uint32_t(c)};o.properties=s.classes[c].properties;o.behaviours=s.classes[c].behaviours;o.position={500.f+float(c)*100,500};s.objects.push_back(o);}return s;
}
int main(){try{
 Level level;level.mapW=100;level.mapH=100;level.hasTerrainCollision=true;std::string error;
 auto s=fixture();GameWorld w;need(vyruz_test::loadManagedFixture(w,s,level,error),error);
 need(s.objects.size()==3 && s.objects[2].classId.value==2,"constructor emits zero-delay record immediately");
 need(w.actors()[2].projectileAltitude==24 && !s.objects[2].owner,"ordinary child inherits altitude but not creator");
 need(w.actors()[1].creates[0].nextMs==50 && !w.actors()[1].creates[1].done,"independent authored schedules");
 w.tick(.1f,{});need(s.objects.size()==5 && s.objects[3].classId.value==2 && s.objects[4].classId.value==3,"one attempt per record, authored order, no catch-up burst");
 need(w.actors()[1].creates[0].nextMs==100 && w.actors()[1].creates[1].done,"repeat increments due time and one-shot completes");
 SaveGame save;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=w.snapshot();std::vector<uint8_t> bytes;SaveGame disk;
 need(encodeSave(save,bytes,error)&&decodeSave(bytes,disk,error),error);
 auto other=fixture();GameWorld resumed;need(vyruz_test::loadManagedFixture(resumed,other,level,error)&&resumed.restoreSnapshot(disk.levels.at(s.mapPath).world,error),error);
 for(int i=0;i<20;++i){w.tick(.02f,{});resumed.tick(.02f,{});need(s.objects.size()==other.objects.size() && w.actors()[1].creates[0].nextMs==resumed.actors()[1].creates[0].nextMs,"disk continuation keeps emission count and exact timer");}
 auto bad=w.snapshot();bad.actors[1].creates[0].classId.value=999;need(!w.restoreSnapshot(bad,error),"reject invalid product reference");
 bad=w.snapshot();bad.actors[1].creates[0].nextMs=std::numeric_limits<float>::infinity();need(!w.restoreSnapshot(bad,error),"reject invalid timer");
 // Older actors resume future repeats without recreating old one-shots.
 auto legacy=w.snapshot();legacy.actors[1].creates.clear();legacy.actors[1].createsInitialized=false;
 need(w.restoreSnapshot(legacy,error),error);need(w.actors()[1].creates[1].done&&w.actors()[1].creates[0].nextMs>w.actors()[1].ageMs,"legacy migration avoids old emissions");
 // Expiry occurs before CREATE at the exact lifetime boundary.
 s=fixture();s.objects[1].properties.push_back(number("Life-time","50"));s.classes[1].properties.push_back(number("Life-time","50"));
 GameWorld expiry;need(vyruz_test::loadManagedFixture(expiry,s,level,error),error);expiry.tick(.05f,{});need(s.objects.size()==3&&!expiry.actors()[1].alive,"lifetime suppresses due emission at expiry");
 // Disabled actors retain constructor products, then pause until reactivation.
 s=fixture();GameWorld off;need(vyruz_test::loadManagedFixture(off,s,level,error),error);auto state=off.snapshot();state.actors[1].alive=false;state.actors[1].reactivateMs=100;vyruz_test::synchronizeActor(state,1);need(off.restoreSnapshot(state,error),error);
 off.tick(.05f,{});need(s.objects.size()==3,"disabled emitter does not run");off.tick(.05f,{});need(s.objects.size()==5,"reactivation runs due records once");
 // Failed off-map placement remains due and succeeds after the parent moves.
 s=fixture();s.objects[1].position={-10,500};GameWorld failed;need(vyruz_test::loadManagedFixture(failed,s,level,error),error);need(s.objects.size()==2&&failed.actors()[1].creates[0].nextMs==0,"failed construction leaves timer due");
 state=failed.snapshot();state.objects[1].position={600,500};vyruz_test::synchronizeActor(state,1);need(failed.restoreSnapshot(state,error),error);failed.tick(.01f,{});need(s.objects.size()==3,"failed spawn retries on later update");
 // A newly created child's own CREATE runs before inherited altitude is set.
 s=fixture();s.classes[2].properties={number("Altitude","8"),create("Second",0,0)};GameWorld nested;need(vyruz_test::loadManagedFixture(nested,s,level,error),error);
 need(s.objects.size()==4&&s.objects[2].classId.value==2&&s.objects[3].classId.value==3,"recursive construction creates child before grandchild");
 need(nested.actors()[2].projectileAltitude==24&&nested.actors()[3].projectileAltitude==8,"nested construction preserves native altitude assignment order");
 // CREATE follows actual accepted player movement, using distinct old/new positions.
 s=fixture();s.objects.resize(1);
 s.objects[0].properties={create("Mark",20,0),create("Second",20,0,true)};
 GameWorld moving;need(vyruz_test::loadManagedFixture(moving,s,level,error),error);const auto previous=moving.playerPosition();
 state=moving.snapshot();state.motion.thrust=10;state.motion.magnitude=10;state.motion.velocityX=10;
 need(moving.restoreSnapshot(state,error),error);
 WorldInput input;input.movement.forward=1;moving.tick(.02f,input);const auto current=moving.playerPosition();
 need(current.x!=previous.x || current.y!=previous.y,"movement fixture actually moves");
 need(s.objects.size()==3&&s.objects[1].position.x==previous.x&&s.objects[1].position.y==previous.y&&
      s.objects[2].position.x==current.x&&s.objects[2].position.y==current.y,"old/current CREATE positions follow native selector");
 // Constructor emission does not count as the first Trail-step history sample.
 s=fixture();s.objects[1].properties={create("Mark",0,10,true)};
 s.objects[1].properties[0].children.push_back(number("Trail-step","8"));
 TextNode fx;fx.kind=TextKind::Block;fx.key="TYPE";fx.arguments={"FX"};s.classes[2].properties={fx};
 GameWorld trail;need(vyruz_test::loadManagedFixture(trail,s,level,error),error);need(!trail.actors()[1].creates[0].previousOrigin,"constructor leaves trail history unset");
 trail.tick(.01f,{});need(!trail.actors().back().trailSegment&&trail.actors()[1].creates[0].previousOrigin.has_value(),"first update seeds history without connection");
 trail.tick(.01f,{});need(trail.actors().back().trailSegment.has_value(),"second update connects prior emission");

 // A spawned emitter's read-only launch lookup must retain its class tree,
 // including when nested CREATE products grow the scene/actor vectors.
 s=fixture();s.objects.resize(1);
 auto launch=create("White-Spark",0,14);
 launch.children.push_back(number("Dir","9"));launch.children.push_back(number("Set-speed","1"));launch.children.push_back(number("Speed","123"));
 s.classes[1].properties={number("Managed","1"),number("Altitude","24"),launch};
 s.classes[2].name="White-Spark";s.classes[2].behaviours={"Straight"};
 s.classes[2].properties={number("Managed","1"),number("Launched","1"),number("Use-inertia","1"),number("Bounding-box-size","0"),create("Second",0,0)};
 GameWorld shared;need(vyruz_test::loadManagedFixture(shared,s,level,error),error);
 const auto* definition=s.classes[1].properties.read().data();
 const auto parent=shared.spawn(s.classes[1].id,{600,500});
 need(parent<shared.actors().size()&&s.objects.size()==4,"launched child and nested product constructed");
 need(s.objects[parent].properties.read().data()==definition,"read-only launched CREATE retains shared parent properties");
 need(shared.actors()[parent+1].direction==9&&shared.actors()[parent+1].turnMs==123,"launch modifiers still applied");
 shared.tick(.014f,{});
 need(s.objects[parent].properties.read().data()==definition,"repeated launch lookup retains class sharing");
 need(shared.actors()[parent].creates[0].nextMs==28,"shared lookup preserves CREATE schedule");
 s.objects[parent].properties.push_back(number("Test-instance-only","1"));
 need(s.objects[parent].properties.read().data()!=definition&&!contentProperty(s.classes[1].properties,"Test-instance-only"),"real property mutation still detaches and protects class");
 std::cout<<"PASS ordinary CREATE construction, timing, lifetime, retry, nesting and save continuation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
