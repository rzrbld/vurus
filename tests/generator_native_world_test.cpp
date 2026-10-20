#include "snapshot_fixture.h"
#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/spider_contact_native.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace {
TextNode prop(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
TextNode type(std::string name,std::vector<TextNode> nodes){TextNode n;n.kind=TextKind::Block;n.key="TYPE";n.arguments={name};n.children=nodes;return n;}
SceneContent fixture(bool stable=true,bool active=true){
 SceneContent s;s.mapPath="TEST/GEN.3DG";ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};p.properties={prop("Bounding-box-size","10")};s.classes.push_back(p);
 ObjectClass g;g.id={1};g.name="Generator";g.properties={prop("Bounding-box-size","30"),type("Generator",{prop("Product","Cop"),prop("Max-births","1"),prop("Birth-delay","300"),prop("Birth-duration","400"),prop("Active",active?"1":"0"),prop("Stressable","1"),prop("Stable-population",stable?"1":"0")})};TextNode energy;energy.key="Energy";energy.children={prop("Initial","100"),prop("Max","100")};g.properties.push_back(energy);s.classes.push_back(g);
 ObjectClass c;c.id={2};c.name="Cop";c.properties={prop("Bounding-box-size","10")};s.classes.push_back(c);
 for(unsigned i=0;i<2;++i){ObjectPlacement o;o.id={i};o.classId={i};o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;o.position={float(200+i*300),200};s.objects.push_back(o);}s.player=ObjectId{0};return s;
}
}


namespace vyruz {struct AmmoImpactNativeTestAccess {
 static void initializeGraph(GameWorld& w){w.initializeSpiderNavigation(true);}
 static bool blocked(GameWorld& w){const auto p=spiderSurfaceToNative(500,200);
  return w.spiderNavigationObstacle(0,{}, {p.x-100,p.y},{p.x+100,p.y},{1,1},false).has_value();}
 static bool hasNode(const GameWorld& w){return w.spiderNavigationActors_.size()>1&&w.spiderNavigationActors_[1]!=SIZE_MAX;}
 static NativeAmmoContactObject body(const GameWorld& w,size_t id){return w.nativeBodyContactObject(id);}
 static void creator(GameWorld& w,size_t id,size_t parent){w.assignNativeCreator(id,parent);}
 static bool playerOwned(GameWorld& w,Projectile& shot){return w.nativeAmmoPlayerOwned(shot);}
};}
namespace {
void need(bool b,const std::string& text){if(!b)throw std::runtime_error(text);}
SceneContent generatorFixture(bool active=true,bool stable=true){
 auto s=fixture(stable,active);
 for(size_t i=0;i<s.classes.size();++i){auto& c=s.classes[i];
  c.properties.push_back(prop("Sub-type",i==0?"Vyruz":i==1?"StaticGen":"Cop"));
  if(i==2){TextNode energy;energy.key="Energy";energy.children={prop("Initial","100"),prop("Max","100")};c.properties.push_back(energy);}
  if(i<2)s.objects[i].properties=c.properties;
 }
 return s;
}
std::vector<uint8_t> encoded(const GameWorld& w){SaveGame s;s.currentLevel="TEST/GEN.3DG";s.levels[s.currentLevel].world=w.snapshot();
 std::vector<uint8_t>b;std::string e;need(encodeSave(s,b,e),e);return b;}
WorldSnapshot disk(const GameWorld& w){SaveGame s;std::string e;need(decodeSave(encoded(w),s,e),e);return s.levels.at("TEST/GEN.3DG").world;}
void load(GameWorld& w,SceneContent& s,const Level& l){std::string e;need(vyruz_test::loadManagedFixture(w,s,l,e),e);w.setDebugEnemyAI(false);}
unsigned continuation(GameWorld& w,const SceneContent& authored,const Level& l,unsigned ticks){
 auto s=authored;GameWorld other;load(other,s,l);std::string e;need(other.restoreSnapshot(disk(w),e),e);
 // Restore intentionally resets debug options; apply the same AI admission
 // setting to both executions before comparing their persisted scheduler.
 other.setDebugEnemyAI(w.debugEnemyAI());
 need(encoded(w)==encoded(other),"exact initial generator disk state");
 for(unsigned n=0;n<ticks;++n){w.tick(.017f,{});other.tick(.017f,{});need(encoded(w)==encoded(other),"byte-exact generator disk continuation");}
 return ticks;
}
}
int main(){try{Level l;l.mapW=l.mapH=40;std::string e;unsigned checks=0,ticks=0;
 // A real manager birth must expose the same owner identity to all native
 // consumers, not just the separate population adapter (4374aa/4374b9).
 for(bool stable:{false,true}){
  auto s=generatorFixture(true,stable);GameWorld w;load(w,s,l);const auto authored=s;
  w.tick(.016f,{});need(w.actors().size()==3,"actual manager factory birth");
  need(w.actors()[2].generatorParent==1&&s.objects[2].owner==ObjectId{1},"generator and general creator links agree");
  const auto body=AmmoImpactNativeTestAccess::body(w,2);
  need(body.creator==2&&body.creatorGeneration==w.actors()[1].nativeGeneration,"native contact consumer sees producer identity and captured UID");
  const auto uid=w.actors()[1].nativeGeneration;need(w.actors()[2].nativeCreatorGeneration==uid,"captured live producer UID");
  ticks+=continuation(w,authored,l,60);
  need(s.objects[2].owner==ObjectId{1}&&w.actors()[2].nativeCreatorGeneration==uid,"ancestry survives real continuation");
  w.damage(2,1000);need(w.actors()[1].generators[0].birthCount==(stable?0:1),"stable replacement and lifetime caps retained");
  ticks+=continuation(w,authored,l,60);++checks;
 }
 // Existing ancestry traversal must reach a parent through a generated child.
 // The generator->player link is a synthetic consumer control, not authored AI.
 {auto s=generatorFixture();GameWorld w;load(w,s,l);AmmoImpactNativeTestAccess::creator(w,1,0);w.tick(.016f,{});
  Projectile shot;shot.owner=2;shot.nativeOwnerGeneration=w.actors()[2].nativeGeneration;
  need(AmmoImpactNativeTestAccess::playerOwned(w,shot),"native ancestry traversal reaches generator's creator");
  auto stale=w.snapshot();++stale.actors[2].nativeCreatorGeneration;need(w.restoreSnapshot(stale,e),e);
  need(!AmmoImpactNativeTestAccess::playerOwned(w,shot)&&!s.objects[2].owner,"stale captured generator UID rejects and clears general pointer");++checks;
 }
 // Do not infer missing ancestry in older checkpoints. New births acquire it;
 // a retained old snapshot continues to describe the state that was saved.
 {auto s=generatorFixture();GameWorld w;load(w,s,l);w.tick(.016f,{});auto legacy=disk(w);
  legacy.objects[2].owner.reset();legacy.actors[2].nativeCreatorGeneration=0;
  need(w.restoreSnapshot(legacy,e),e);need(w.actors()[2].generatorParent==1&&!s.objects[2].owner,"old checkpoint missing creator stays explicit");++checks;
 }
 // Original4373cb/437418 publish the temporary query enable/disable even
 // if occupancy prevents appearance. The node remains available for later use.
 {auto s=generatorFixture(false);GameWorld w;load(w,s,l);const auto authored=s;
  AmmoImpactNativeTestAccess::initializeGraph(w);w.setPlayerPosition(500,200);w.tick(.016f,{});
  need(!w.actors()[1].solid&&!w.actors()[1].generators[0].active,"occupied inactive generator remains hidden");
  need(AmmoImpactNativeTestAccess::hasNode(w)&&!AmmoImpactNativeTestAccess::blocked(w),"blocked attempt retains disabled graph node");
  w.setPlayerPosition(200,200);w.tick(.016f,{});
  need(w.actors()[1].solid&&w.actors()[1].generators[0].active&&AmmoImpactNativeTestAccess::blocked(w),"same-pass first appearance blocks navigation");
  ticks+=continuation(w,authored,l,60);++checks;
 }
 // With no initial obstruction, a genuine first-activation tick must publish
 // appearance before any following query, including after immediate disk load.
 {auto s=generatorFixture(false);GameWorld w;load(w,s,l);AmmoImpactNativeTestAccess::initializeGraph(w);w.tick(.016f,{});
  need(AmmoImpactNativeTestAccess::blocked(w),"first activation publishes navigation immediately");
  need(w.restoreSnapshot(disk(w),e),e);need(AmmoImpactNativeTestAccess::blocked(w),"new graph state survives disk before next tick");++checks;
 }
 std::cout<<"PASS "<<checks<<" generator World ancestry/UID/cap/query controls; "<<ticks<<" byte-exact disk continuation ticks\n";
}catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
