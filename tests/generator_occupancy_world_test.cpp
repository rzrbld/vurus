//405df0 live mixed Actor/compact-Ammo bridge. Native instruction evidence is
//retained under work/quirk-audit/n1-generator-occupancy/native; no original assets
//are needed for these synthetic source/target and saved-lifecycle controls.
#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "game/properties.h"
#include "game/generator_occupancy_native.h"
#include <limits>
#include <bit>
#include "snapshot_fixture.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace vyruz {struct AmmoImpactNativeTestAccess {
 static bool occupied(GameWorld&w,size_t id=1){return w.generatorOccupied(id);}
 static void update(GameWorld&w,size_t id=1){w.updateGenerator(id,16);}
 static void remove(GameWorld&w,size_t shot){w.projectiles_[shot].alive=false;w.releaseNativeProjectile(w.projectiles_[shot]);}
};}
namespace {
void need(bool v,const std::string&m){if(!v)throw std::runtime_error(m);}
TextNode value(std::string k,std::string v){TextNode n;n.key=std::move(k);n.values={std::move(v)};n.kind=TextKind::Assignment;return n;}
TextNode block(std::string k,std::vector<TextNode> ns,std::string type={}){TextNode n;n.key=std::move(k);n.kind=TextKind::Block;n.children=std::move(ns);if(!type.empty())n.arguments={std::move(type)};return n;}
void place(SceneContent&s,unsigned c,ContentPosition p){ObjectPlacement o;o.id={uint32_t(s.objects.size())};o.classId={c};o.position=p;o.properties=s.classes[c].properties;o.behaviours=s.classes[c].behaviours;o.flags=s.classes[c].flags;o.sprite=s.classes[c].sprite;s.objects.push_back(std::move(o));}
SceneContent fixture(float radius=1,bool keep=true){SceneContent s;s.mapPath="TEST/GENERATOR-OCCUPANCY.3DG";
 ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};p.properties={value("Sub-type","Vyruz"),value("Bounding-box-size","10"),value("Managed","1"),block("Energy",{value("Initial","10000"),value("Max","10000")})};
 ObjectClass g;g.id={1};g.name="Producer";g.behaviours={"Fixed"};g.properties={value("Sub-type","StaticGen"),value("Bounding-box-size","30"),value("Managed","1"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("TYPE",{value("Product","Product"),value("Max-births","1"),value("Birth-delay","100"),value("Birth-duration","100"),value("Active","1"),value("Stable-population","1")},"Generator")};
 ObjectClass product;product.id={2};product.name="Product";product.behaviours={"Fixed"};product.properties={value("Sub-type","Cop"),value("Bounding-box-size","10"),value("Managed","1"),block("Energy",{value("Initial","1000"),value("Max","1000")})};
 ObjectClass a;a.id={3};a.name="Bare-Ammo";a.behaviours={"Straight"};a.properties={value("Sub-type","Ammo"),value("Bounding-box-size",std::to_string(radius)),value("Keep-bb-when-invisible",keep?"1":"0"),value("Managed","1"),value("Speed-max","0"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("TYPE",{value("Damage","0"),value("Damage-zone","0"),value("Ignore-owner","1")},"Ammo")};
 ObjectClass item;item.id={4};item.name="Item";item.behaviours={"Fixed"};item.properties={value("Sub-type","Item"),value("Bounding-box-size","10"),value("Managed","1"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("TYPE",{value("Only-for-player","0")},"Item")};
 s.classes={p,g,product,a,item};place(s,0,{500,500});place(s,1,{2560.25f,1280.125f});s.player=ObjectId{0};return s;}
Level arena(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;return l;}
void load(GameWorld&w,SceneContent&s,const Level&l){std::string e;need(w.load(s,l,e),e);w.setDebugEnemyAI(false);w.setDebugGodMode(true);}
void append(GameWorld&w,const SceneContent&s,ContentPosition p,float radius,bool hidden=false,float inactive=0,bool ignoreOwner=true,bool divergent=false){auto snapshot=w.snapshot();Projectile shot;shot.owner=snapshot.player;shot.classId={3};shot.position=p;shot.radius=radius;shot.damage=shot.damageRadius=0;shot.ignoreOwner=ignoreOwner;shot.phaseInitialized=true;shot.inactiveMs=inactive;shot.phaseHidden=hidden;vyruz_test::appendProjectile(snapshot,s,shot);
 if(divergent)snapshot.projectiles.back().nativeBareBody->motion.center={100000,-100000};std::string e;need(w.restoreSnapshot(snapshot,e),e);w.setDebugEnemyAI(false);w.setDebugGodMode(true);}
std::vector<uint8_t> encoded(const GameWorld&w){SaveGame save;save.currentLevel="TEST/GENERATOR-OCCUPANCY.3DG";save.levels[save.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string e;need(encodeSave(save,b,e),e);return b;}
WorldSnapshot decoded(const GameWorld&w){SaveGame s;std::string e;need(decodeSave(encoded(w),s,e),e);return s.levels.begin()->second.world;}
void expectCenter(const Projectile&p,ContentPosition surface){need(p.nativeBareBody.has_value(),"compact Ammo body retained");const long double x=static_cast<long double>(surface.x)*.015625f,y=static_cast<long double>(surface.y)*.03125f;const float nx=static_cast<float>((y+x)*35.7770881652832f),nz=static_cast<float>((y-x)*35.7770881652832f);need(p.nativeBareBody->motion.center.x==nx&&p.nativeBareBody->motion.center.y==nz,"41b4b0 refreshed exact current BBOX from float surface");}
}
int main(){try{size_t cases=0;const auto l=arena();
 // Positive authored size1 is an enabled point; zero authored size disables its
 // BBOX. Owner/Ignore-owner, hidden visuals and Off are not independent filters.
 for(float radius:{0.f,1.f,8.f,10.f,20.f})for(bool keep:{false,true})for(bool hidden:{false,true})for(float inactive:{0.f,500.f})for(bool ignore:{false,true}){
  auto s=fixture(radius,keep);GameWorld w;load(w,s,l);const auto p=s.objects[1].position;append(w,s,p,radius,hidden,inactive,ignore,true);const auto previous=w.projectiles()[0].nativeBareBody->motion.previousSurface;
  const bool admitted=radius>0&&(inactive==0||(hidden&&keep))&&(!hidden||keep);
  need(AmmoImpactNativeTestAccess::occupied(w)==admitted,"enabled/phase/Keep-BBOX/owner compact query");if(admitted){expectCenter(w.projectiles()[0],p);need(w.projectiles()[0].nativeBareBody->motion.previousSurface.x==previous.x&&w.projectiles()[0].nativeBareBody->motion.previousSurface.y==previous.y,"current-BBOX refresh does not overwrite previous surface");}
  AmmoImpactNativeTestAccess::update(w);need(w.actors()[1].generators[0].birthCount==(admitted?0:1),"actual Generator birth blocked only by admitted compact body");++cases;
 }
 // Large extents do not expand the original 3x3 cell scan. The cached spatial
 // cell comes from the body's surface position, independently of old BBOXcenter.
 for(int dx=-2;dx<=2;++dx)for(int dy=-2;dy<=2;++dy){auto s=fixture(10000);GameWorld w;load(w,s,l);const auto p=s.objects[1].position;append(w,s,{p.x+dx*64,p.y+dy*32},10000);need(AmmoImpactNativeTestAccess::occupied(w)==(std::abs(dx)<=1&&std::abs(dy)<=1),"strict3x3 cached-cell scan, no radius expansion");++cases;}
 // Two compact bodies in one cell: newest head refreshes and terminates first.
 {auto s=fixture();GameWorld w;load(w,s,l);const auto p=s.objects[1].position;append(w,s,p,1,false,0,true,true);append(w,s,p,1,false,0,true,true);need(AmmoImpactNativeTestAccess::occupied(w),"two compact candidates");need(w.projectiles()[0].nativeBareBody->motion.center.x==100000,"older compact body untouched after head early return");expectCenter(w.projectiles()[1],p);++cases;}
 // Mixed actor/Ammo order uses the shared arrival ordinal, not all Actor IDs
 // first or all Ammo first. This ordinary Item is not Only-for-player.
 for(bool ammoLast:{false,true}){auto s=fixture();GameWorld w;load(w,s,l);const auto p=s.objects[1].position;size_t item;
  if(!ammoLast)append(w,s,p,1,false,0,true,true);item=w.spawn({4},p);auto state=w.snapshot();state.actors[item].nativeMotion=SpiderMotionHistory{{100000,-100000},p};std::string e;need(w.restoreSnapshot(state,e),e);if(ammoLast)append(w,s,p,1,false,0,true,true);
  need(AmmoImpactNativeTestAccess::occupied(w),"mixed Item/bare-Ammo query");const bool ammoRefreshed=w.projectiles()[0].nativeBareBody->motion.center.x!=100000,actorRefreshed=w.actors()[item].nativeMotionHistory()->center.x!=100000;need(ammoRefreshed==ammoLast&&actorRefreshed!=ammoLast,"shared mixed-chain head refresh/early return");++cases;
 }
 // Actual41a5d0/41a740 constructor controls prove zero-size disable and
 // positive-size FX point admission. A generic effect label is not a filter.
 for(const char*subtype:{"FX","Item","Cop"})for(float radius:{0.f,1.f,10.f}){
  auto s=fixture();auto cls=s.classes[4];cls.id={5};cls.name="Bounds-control";cls.properties={value("Sub-type",subtype),value("Bounding-box-size",std::to_string(radius)),value("Managed","1"),block("Energy",{value("Initial","1000"),value("Max","1000")})};if(std::string(subtype)=="FX")cls.properties.push_back(block("TYPE",{},"FX"));s.classes.push_back(cls);place(s,5,s.objects[1].position);GameWorld w;load(w,s,l);
  if(std::string(subtype)=="FX"){auto state=w.snapshot();state.actors.back().effect=true;std::string e;need(w.restoreSnapshot(state,e),e);}
  need(AmmoImpactNativeTestAccess::occupied(w)==(radius>0),"rawzero-size disable / enabledFX point / ordinarybounds");++cases;
 }
 //405df0 exception: Item subtype0x12 +TYPE Item0x13 Only-for-player.
 // Gate/Lift subtypes with an Item attribute are not this special case.
 for(const char*subtype:{"Item","Gate","Lift"})for(bool onlyPlayer:{false,true}){
  auto s=fixture();auto cls=s.classes[4];cls.id={5};cls.name="Only-player-filter-control";cls.sprite.sequence={1};cls.properties={value("Sub-type",subtype),value("Bounding-box-size","10"),value("Managed","1"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("TYPE",{value("Only-for-player",onlyPlayer?"1":"0")},"Item")};
  if(std::string(subtype)!="Item")cls.properties.push_back(block("TYPE",{value("Active","1"),value("Opened","0")},"Gate"));s.classes.push_back(cls);place(s,5,s.objects[1].position);GameWorld w;load(w,s,l);if(std::string(subtype)!="Item")need(w.actors().back().gate.has_value()&&w.actors().back().solid,"closed Gate/Lift retains rawBBOX");need(AmmoImpactNativeTestAccess::occupied(w)!=((std::string(subtype)=="Item")&&onlyPlayer),"exactItem+Only-for-player exception instead ofallItemattributes");++cases;
 }
 //4391d0 Off producer retains a linked kind3 body and disables bounds unless
 //Keep-BBOX.439140 reactivation restores admission; use actual World visits.
 for(bool keep:{false,true}){auto s=fixture();s.classes[2].properties.push_back(value("Keep-bb-when-invisible",keep?"1":"0"));GameWorld w;load(w,s,l);const auto p=s.objects[1].position;const auto off=w.spawn({2},p);auto state=w.snapshot();state.actors[off].alive=state.actors[off].solid=false;state.actors[off].reactivateMs=500;vyruz_test::synchronizeActor(state,off);std::string e;need(w.restoreSnapshot(state,e),e);w.setDebugEnemyAI(false);w.setDebugGodMode(true);need(AmmoImpactNativeTestAccess::occupied(w)==keep,"positiveBBox Off admission followsnativeKeep-BBOX");
  //A separate ordinary blocker prevents production while the Off actor wakes;
  //remove it physically before checking the reactivated actor by itself.
  const auto blocker=w.spawn({2},p);for(int t=0;t<6;++t)w.tick(.1f,{});need(w.actors()[off].alive,"realWorldOff timeout reactivates actor");state=w.snapshot();state.actors[blocker].alive=state.actors[blocker].solid=false;state.actors[blocker].reactivateMs=-1;vyruz_test::releaseActor(state,blocker);need(w.restoreSnapshot(state,e),e);need(AmmoImpactNativeTestAccess::occupied(w),"reactivated positiveBBox blocks independentlyofoldOff flag");++cases;
 }
 // Native repeated square refresh can move extrema by rounding even at a
 // fixed authored surface. Preserve that six-float history through checkpoint.
 {auto s=fixture();s.objects[1].position={5184,1664};auto cls=s.classes[4];cls.id={5};cls.name="Square";cls.sprite.sequence={1};cls.properties={value("Sub-type","Gate"),value("Bounding-box-size","48"),value("Managed","1"),block("TYPE",{value("Active","1"),value("Opened","0")},"Gate")};s.classes.push_back(cls);place(s,5,s.objects[1].position);const auto original=s;GameWorld w;load(w,s,l);const auto square=size_t(2);const auto p=s.objects[square].position;
  const auto equal=[](const NativeGeneratorSquareState&a,const NativeGeneratorSquareState&b){return a.centerX==b.centerX&&a.centerZ==b.centerZ&&a.minX==b.minX&&a.minZ==b.minZ&&a.maxX==b.maxX&&a.maxZ==b.maxZ;};
  auto expected=makeNativeGeneratorSquare(p.x,p.y,48);const auto initial=expected;need(w.actors()[square].nativeGeneratorSquare&&equal(*w.actors()[square].nativeGeneratorSquare,initial),"actual constructor square state");
  for(int t=0;t<100;++t){need(AmmoImpactNativeTestAccess::occupied(w),"closed enabled square blocks producer");refreshNativeGeneratorSquare(expected,p.x,p.y);need(equal(*w.actors()[square].nativeGeneratorSquare,expected),"persistent raw square refresh rounding");}need(!equal(initial,expected),"original authored static square demonstrates retained rounding history");
  auto copy=original;GameWorld resumed;load(resumed,copy,l);std::string e;need(resumed.restoreSnapshot(decoded(w),e),e);need(equal(*resumed.actors()[square].nativeGeneratorSquare,expected),"disk retains six-float square history");
  for(int t=0;t<100;++t){need(AmmoImpactNativeTestAccess::occupied(w)&&AmmoImpactNativeTestAccess::occupied(resumed),"resumed square query");need(encoded(w)==encoded(resumed),"exact continued square rounding history");}
  const auto before=encoded(w);auto bad=w.snapshot();bad.actors[square].nativeGeneratorSquare->minX=bad.actors[square].nativeGeneratorSquare->maxX+1;need(!w.restoreSnapshot(bad,e)&&encoded(w)==before,"inverted square extrema rejected transactionally");bad=w.snapshot();bad.actors[square].nativeGeneratorSquare->centerX=std::numeric_limits<float>::quiet_NaN();need(!w.restoreSnapshot(bad,e)&&encoded(w)==before,"nonfinite square rejected transactionally");bad=w.snapshot();bad.actors[0].nativeGeneratorSquare=expected;need(!w.restoreSnapshot(bad,e)&&encoded(w)==before,"square state on wrong native shape rejected transactionally");
  auto legacy=w.snapshot();legacy.actors[square].nativeGeneratorSquare.reset();need(w.restoreSnapshot(legacy,e),e);need(w.actors()[square].nativeGeneratorSquare&&equal(*w.actors()[square].nativeGeneratorSquare,initial),"legacy missing square history explicitly reconstructs constructor");++cases;
 }
 // Full World and portable save preserve a blocked due producer. Real compact
 // removal unlinks its cells/awake/pool identity and admits a following birth.
 {auto s=fixture();GameWorld w;load(w,s,l);append(w,s,s.objects[1].position,1);w.tick(.016f,{});need(w.actors()[1].generators[0].birthCount==0,"live World compact body blocks due birth");auto copy=fixture();GameWorld resumed;load(resumed,copy,l);std::string e;need(resumed.restoreSnapshot(decoded(w),e),e);resumed.setDebugEnemyAI(false);resumed.setDebugGodMode(true);
  for(int t=0;t<8;++t){w.tick(.016f,{});resumed.tick(.016f,{});need(encoded(w)==encoded(resumed),"blocked compact-body disk continuation");}
  AmmoImpactNativeTestAccess::remove(w,0);AmmoImpactNativeTestAccess::remove(resumed,0);need(!AmmoImpactNativeTestAccess::occupied(w),"physical Ammo removal clears occupied cell");w.tick(.016f,{});resumed.tick(.016f,{});need(w.actors()[1].generators[0].birthCount==1&&encoded(w)==encoded(resumed),"following actual birth and exact disk state after removal");++cases;
 }
 std::cout<<"PASS "<<cases<<" generator mixed compact-body bounds/phase/cell/order/lifecycle controls\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
