// Production common-query adapter compared with original4105d0 records. The
// fixture supplies graph history explicitly; this does not prove map producers.
#include "game/world.h"
#include "game/properties.h"
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static TextNode value(std::string key,std::string val){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(val)};return n;}
static TextNode block(std::string kind,std::vector<TextNode> fields){TextNode n;n.kind=TextKind::Block;n.key="TYPE";n.arguments={std::move(kind)};n.children=std::move(fields);return n;}
struct Record {uint32_t movement,positions,types,reverseOrder,age,owner,ignore,result,target,closest;float x,z;};
static constexpr float motion[8][2]={{20,0},{-20,0},{0,20},{0,-20},{20,20},{-20,20},{20,-20},{0,0}};
static constexpr float offsets[14][4]={{12,0,-12,0},{0,12,0,-12},{10,10,-10,-10},{3,0,7,0},{7,0,3,0},{0,0,0,0},{40,0,7,0},{-25,0,-35,0},{17,0,18,0},{17,17,-17,-17},{2,0,-2,0},{.5f,0,-.5f,0},{-8,0,8,0},{-50,0,50,0}};
namespace vyruz {
struct AmmoImpactNativeTestAccess {
 static NativeBodyContactQuery query(GameWorld& world,const Record& r){
  NativeNavGraph graph;const ContactVelocity proposed{motion[r.movement][0],motion[r.movement][1]};
  const std::array<ContactVelocity,3> centers{{{}, {proposed.x+offsets[r.positions][0],proposed.z+offsets[r.positions][1]}, {proposed.x+offsets[r.positions][2],proposed.z+offsets[r.positions][3]}}};
  for(size_t n=0;n<3;++n){const size_t id=n+1;auto& a=world.actors_[id];auto& o=world.scene_->objects[id];const auto p=centers[n];
   a.radius=n==2?12:8;a.ageMs=1000;a.nativeGeneration=7;a.nativeCreatorGeneration=0;a.ammoHidden=false;
   const auto surface=spiderNativeToSurface(p.x,p.z);o.position={surface.x,surface.y};a.nativeMotion=SpiderMotionHistory{{p.x,p.z},o.position};
   o.owner.reset();o.properties={value("Sub-type",n==1?"Ammo":n==2&&r.types?"Item":"Cop")};
   if(n==1){a.ageMs=r.age==0?0:r.age==1?499:r.age==2?500:1000;
    if(r.owner)o.owner=ObjectId{r.owner==3?3u:1u};a.nativeCreatorGeneration=r.owner==2?8:7;
    o.properties.push_back(block("Ammo",{value("Ignore-owner",std::to_string(r.ignore))}));}
   // This oracle rewrites constructed placement types directly through its
   // friend hook. Refresh the compiled placement field just as a factory/load
   // does; keep the original query fingerprint and point assertions unchanged.
   a.nativeSubtype=nativeObjectSubtype(o,id==world.player_);
   graph.objects.push_back({id,{p.x-a.radius,p.z-a.radius,p.x+a.radius,p.z+a.radius}});
  }
  for(unsigned axis=0;axis<2;++axis){auto& edges=axis?graph.yEdges:graph.xEdges;
   for(unsigned n=0;n<3;++n)for(bool maximum:{false,true})edges.push_back({n,maximum});
   const auto coordinate=[&](auto e){auto b=graph.objects[e.object].bounds;return axis?(e.maximum?b.maxY:b.minY):(e.maximum?b.maxX:b.minX);};
   std::vector<NativeNavGraphEdge> ordered(edges.begin(),edges.end());std::sort(ordered.begin(),ordered.end(),[&](auto a,auto b){const float x=coordinate(a),y=coordinate(b);const unsigned ai=a.object*4+axis+(a.maximum?2:0),bi=b.object*4+axis+(b.maximum?2:0);return x!=y?x<y:r.reverseOrder?ai>bi:ai<bi;});edges.assign(ordered.begin(),ordered.end());
  }
  world.spiderNavigation_=std::move(graph);world.spiderNavigationActors_={SIZE_MAX,0,1,2};
  return world.queryNativeBodyContacts(1,{},proposed,8);
 }
 static void stationaryPlayer(GameWorld& world){
  world.actors_[1].alive=world.actors_[3].alive=false;
  auto& player=world.actors_[0];auto& ammo=world.actors_[2];player.radius=ammo.radius=8;
  player.nativeMotion.reset();ammo.nativeMotion.reset();ammo.ageMs=1000;world.scene_->objects[2].owner.reset();
  for(float x:{.1f,123.1f,500.1f,1234.125f})for(float y:{.2f,123.2f,500.2f}){
   world.scene_->objects[0].position={x,y};world.scene_->objects[2].position={x,y};
   world.initializeSpiderNavigation(true);const auto p=spiderSurfaceToNative(x,y);
   const auto result=world.queryNativeBodyContacts(0,{p.x,p.y},{p.x,p.y},8);
   need(!result.selected&&!result.hasAmmoCandidate,"fractional stationary player without history must not invent Ammo contact");
  }
 }
 static void subtypeShapes(GameWorld& world){
  struct Case{const char* subtype;float radius;uint32_t shape;bool extraFx;};
  for(const auto c:{Case{"Gate",48,4,false},Case{"Lift",48,4,false},Case{"Thing",1,2,false},Case{"Ammo",1,2,false},Case{"Vyruz",1,5,false},Case{"Cop",8,5,true},Case{"FX",8,1,true}}){
   auto& object=world.scene_->objects[2];auto& actor=world.actors_[2];object.properties={value("Sub-type",c.subtype)};
   if(c.extraFx)object.properties.push_back(block("FX",{}));object.position={500.1f,500.2f};actor.radius=c.radius;actor.nativeMotion.reset();
   actor.nativeSubtype=nativeObjectSubtype(object);
   world.initializeSpiderNavigation(true);auto shape=world.resolveNativeBodyContact(2);need(shape&&shape->filter.shape==c.shape,"explicit native subtype shape precedence");
   const auto node=world.spiderNavigationActors_[2];
   if(c.shape==1){need(node==SIZE_MAX&&shape->disabled,"FX subtype disables native graph shape");continue;}
   need(node<world.spiderNavigation_->objects.size(),"native subtype graph enrollment");const auto b=world.spiderNavigation_->objects[node].bounds;
   if(c.shape==2)need(b.minX==b.maxX&&b.minY==b.maxY,"small Thing/Ammo graph bounds are points");
   if(c.shape==5)need(b.minX<b.maxX&&b.minY<b.maxY,"small player/nonFX hybrid remains a circle");
   if(c.shape==4){need(!actor.gate&&!actor.lift,"Gate-Empty shape test has no TYPE Gate/Lift attribute");need(b.minX==shape->minimum.x&&b.maxX==shape->maximum.x&&b.minY==shape->minimum.z&&b.maxY==shape->maximum.z,"subtype square bounds match common query");}
  }
 }
};
}
int main(int argc,char** argv){try{
 need(argc==1||argc==2,"usage: actor_ammo_world_query_test [closest-probe/result.bin]");
 std::ifstream in;if(argc==2){in.open(argv[1],std::ios::binary);need(bool(in),"missing original query records");}
 SceneContent scene;Level level;level.mapW=level.mapH=32;scene.mapPath="TEST/COMMON.3DG";
 for(unsigned n=0;n<4;++n){ObjectClass c;c.id={n};c.name=n?"Body"+std::to_string(n):"Player";if(!n)c.behaviours={"Player"};c.properties={value("Sub-type",n?"Cop":"Vyruz"),value("Bounding-box-size","8")};scene.classes.push_back(c);
  ObjectPlacement o;o.id={n};o.classId=c.id;o.properties=c.properties;o.behaviours=c.behaviours;o.position={float(500+n*100),500};scene.objects.push_back(o);}
 scene.player=ObjectId{0};GameWorld world;std::string error;need(world.load(scene,level,error),error.c_str());
 uint64_t hash=14695981039346656037ull;unsigned count=0,finite=0,singular=0,hints=0;
 for(unsigned m=0;m<8;++m)for(unsigned p=0;p<14;++p)for(unsigned t=0;t<2;++t)for(unsigned rev=0;rev<2;++rev)for(unsigned age=0;age<4;++age)for(unsigned owner=0;owner<4;++owner)for(unsigned ignore=0;ignore<2;++ignore){
  Record expected{m,p,t,rev,age,owner,ignore,0,0,0,0,0};if(argc==2){Record record{};need(bool(in.read(reinterpret_cast<char*>(&record),sizeof record)),"short original record");need(record.movement==m&&record.positions==p&&record.types==t&&record.reverseOrder==rev&&record.age==age&&record.owner==owner&&record.ignore==ignore,"original record inputs");expected=record;}
  auto q=AmmoImpactNativeTestAccess::query(world,expected);need(q.used&&!q.unsupportedShape,"live adapter declined circle contract");
  const uint32_t target=q.selected?uint32_t(q.selected->reference-1):0;
  const uint32_t closest=q.closestNonContactNode?uint32_t(*q.closestNonContactNode+1):0;hints+=closest!=0;
  Record actual{m,p,t,rev,age,owner,ignore,target?0u:1u,target,closest,0,0};
  const auto* words=reinterpret_cast<const unsigned char*>(&actual);for(unsigned n=0;n<40;++n){hash^=words[n];hash*=1099511628211ull;}
  if(argc==2){need(target==expected.target&&closest==expected.closest&&actual.result==expected.result,"World/native winner or closest hint mismatch");
   if(target&&std::isfinite(expected.x)&&std::isfinite(expected.z)){need(std::bit_cast<uint32_t>(q.selected->point.x)==std::bit_cast<uint32_t>(expected.x)&&std::bit_cast<uint32_t>(q.selected->point.z)==std::bit_cast<uint32_t>(expected.z),"World/native finite point mismatch");++finite;}
   else if(target){const auto match=[](float a,float b){return std::isnan(b)?std::isnan(a):a==b;};need(match(q.selected->point.x,expected.x)&&match(q.selected->point.z,expected.z),"original indefinite selection point");++singular;}}
  need(world.actors()[2].ageMs==(age==0?0:age==1?499:age==2?500:1000),"query advanced target phase age");++count;
 }
 // Fingerprint includes every native input, selected target and closest hint.
 need(hash==0xa00aec22012db3a5ull,"retained original World query fingerprint");
 if(argc==2){char extra;need(!in.read(&extra,1),"extra original records");}
 AmmoImpactNativeTestAccess::stationaryPlayer(world);
 AmmoImpactNativeTestAccess::subtypeShapes(world);
 std::cout<<"PASS "<<count<<" World/native common query cases, "<<hints<<" nonzero hints, "<<finite<<" original finite points, "<<singular<<" original indefinite controls\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
