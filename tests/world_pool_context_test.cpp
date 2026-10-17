#include "game/world.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include "host_temp.h"
using namespace vyruz;
namespace vyruz {struct AmmoImpactNativeTestAccess {
 static void held(GameWorld& w,size_t id){w.retainItem(id,false);w.actors_[id].alive=false;w.actors_[id].reactivateMs=-1;w.synchronizeActorCell(id);}
 static void destroy(GameWorld& w,size_t id){w.destroy(id,{});}
 static void awake(GameWorld& w,size_t id,float life=-1){w.actors_[id].lifeMs=life;w.enrollNativeActor(id);}
 static void bare(GameWorld& w){Projectile p;p.classId={2};p.owner=w.player_;p.position={256,128};p.alive=true;w.initializeBareAmmoBody(p);w.projectiles_.push_back(p);w.synchronizeBareAmmoCell(w.projectiles_.back());w.rebuildBareAmmoIndex();}
 static void releaseBare(GameWorld& w){auto&p=w.projectiles_.back();p.alive=false;w.releaseNativeProjectile(p);}
};}
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(k);n.values={std::move(v)};return n;}
static TextNode type(std::string name){TextNode n;n.kind=TextKind::Block;n.key="TYPE";n.arguments={std::move(name)};return n;}
static SceneContent scene(std::string map="TEST/POOL.3DG") {
 SceneContent s;s.mapPath=std::move(map);s.player=ObjectId{0};
 for(const auto*name:{"Player","Body","Bare-Ammo"}) {
  ObjectClass c;c.id={uint32_t(s.classes.size())};c.name=name;c.behaviours={"Fixed"};
  c.properties={value("Managed","0"),value("Bounding-box-size","0"),value("Sub-type",c.name=="Player"?"Vyruz":c.name=="Bare-Ammo"?"Ammo":"Thing")};
  if(c.name=="Bare-Ammo"){c.flags.invisible=true;c.properties.push_back(type("Ammo"));}
  if(c.name=="Body"){c.properties.push_back(value("Carry","1"));auto item=type("Item");item.children={value("Only-for-player","1")};c.properties.push_back(item);}
  s.classes.push_back(c);
 }
 for(auto p:std::vector<ContentPosition>{{64,64},{128,96},{192,128}}){ObjectPlacement o;o.id={uint32_t(s.objects.size())};o.classId={o.id.value?1u:0u};o.position=p;o.properties=s.classes[o.classId.value].properties;o.behaviours={"Fixed"};s.objects.push_back(o);}
 return s;
}
static Level level(){Level l;l.mapW=l.mapH=8;l.hasTerrainCollision=true;l.collisionMask.assign(64,0xff);return l;}
static NativeTileBodyCatalog tiles(){NativeTileBodyCatalog c;c.width=c.height=8;c.sources={{},{1,15,1}};c.cells.resize(64);c.cells[2*8+2][0]=1;c.cells[4*8+6][0]=1;return c;}
static void load(GameWorld& w,SceneContent& s,const Level& l){w.setNativeTileCatalog(tiles());std::string error;need(w.load(s,l,error),error);}
static WorldSnapshot disk(const GameWorld& w){SaveGame save;save.currentLevel="TEST/POOL.3DG";save.levels[save.currentLevel].world=w.snapshot();
 const auto path=(std::filesystem::temp_directory_path()/("vurus-pool-context-"+std::to_string(getpid())+".vzs")).string();std::string error;SaveGame out;
 need(writeSaveFile(path,save,error)&&readSaveFile(path,out,error),error);std::filesystem::remove(path);return out.levels.at(save.currentLevel).world;}
int main(){try {
 auto s=scene();const auto l=level();GameWorld w;load(w,s,l);
 auto snapshot=w.snapshot();need(snapshot.nativePool.has_value()&&validNativeWorldPoolSnapshot(snapshot),"fresh pool snapshot valid");
 need(snapshot.nativePool->pool.liveCount==5&&snapshot.nativePool->tiles.size()==2&&snapshot.nextNativeGeneration==6,"three actors and two compact terrain bodies share allocator");
 need(snapshot.actors.size()==3,"terrain bodies do not allocate heavyweight actors");
 const auto stored=disk(w);auto resumedScene=scene();GameWorld resumed;load(resumed,resumedScene,l);std::string error;
 need(resumed.restoreSnapshot(stored,error),error);need(resumed.captureNativePool()==w.captureNativePool(),"disk restores complete physical/free catalog and tile bindings");
 const auto birth=w.spawn({1},{280,160}),other=resumed.spawn({1},{280,160});
 need(w.actors()[birth].nativeGeneration==resumed.actors()[other].nativeGeneration&&w.actors()[birth].nativePoolSlot==resumed.actors()[other].nativePoolSlot,"exact disk branch allocates same next physical identity");
 AmmoImpactNativeTestAccess::bare(w);auto p=w.snapshot();need(validNativeWorldPoolSnapshot(p),"bare Ammo belongs to common pool");
 const auto live=p.nativePool->pool.liveCount;AmmoImpactNativeTestAccess::releaseBare(w);
 need(w.captureNativePool().pool.liveCount==live-1,"bare physical removal compacts pool exactly once");
 AmmoImpactNativeTestAccess::releaseBare(w);need(w.captureNativePool().pool.liveCount==live-1,"duplicate physical removal does not compact twice");
 // Scenery cleanup order differs from text writer order. Held original1 is
 // recursively released immediately before player0 at the final visited cell.
 auto heldScene=scene();GameWorld held;load(held,heldScene,l);AmmoImpactNativeTestAccess::held(held,1);
 auto ageScene=scene();GameWorld age;load(age,ageScene,l);AmmoImpactNativeTestAccess::awake(age,1);AmmoImpactNativeTestAccess::held(age,1);
 const auto ageBefore=age.actors()[1].ageMs;age.tick(.01f,{});auto ageSnapshot=age.snapshot();
 need(std::abs(ageSnapshot.actors[1].ageMs-ageBefore-10)<.0001 && !ageSnapshot.actors[1].alive,"retained manager body ages without rendering as ground actor");
 need(ageSnapshot.heldItems[0].actor.ageMs==ageSnapshot.actors[1].ageMs&&ageSnapshot.actorCells->cell[1]==SIZE_MAX,"retained age alias stays synchronized and detached");
 need(!ageSnapshot.actors[1].nativeAwake,"retained rest visit removes current manager entry by native sleep rule");
 age.tick(.01f,{});need(age.actors()[1].ageMs==ageSnapshot.actors[1].ageMs,"sleeping held original stops ordinary manager ageing");
 auto ageDisk=disk(age);auto ageReloadScene=scene();GameWorld ageReload;load(ageReload,ageReloadScene,l);need(ageReload.restoreSnapshot(ageDisk,error),error);
 need(ageReload.actors()[1].nativeHeldOriginal,"disk restore derives canonical inventory admission");
 auto lethalScene=scene();GameWorld lethal;load(lethal,lethalScene,l);AmmoImpactNativeTestAccess::awake(lethal,1,5);AmmoImpactNativeTestAccess::held(lethal,1);
 lethal.tick(.01f,{});need(lethal.actors()[1].nativePoolSlot==SIZE_MAX&&validNativeWorldPoolSnapshot(lethal.snapshot()),"retained original reaches normal lifetime release without dangling held pool alias");
 auto h=held.snapshot();need(validNativeWorldPoolSnapshot(h),"inventory copy aliases dormant local native body");
 auto falseAlias=h;falseAlias.heldItems[0].actor.nativePoolSlot=h.actors[2].nativePoolSlot;falseAlias.heldItems[0].actor.nativeGeneration=h.actors[2].nativeGeneration;
 need(!validNativeWorldPoolSnapshot(falseAlias),"held record cannot alias an unrelated actor merely by matching raw slot and UID");
 NativeActorPool expected;need(expected.restore(h.nativePool->pool),"restore independent cleanup oracle");
 for(size_t slot:{4u,2u,3u,1u,0u})need(expected.release(slot),"expected cleanup slot live");
 const auto seed=held.nativePoolForReplacement();need(seed.pool==expected.snapshot()&&seed.originalHistoryKnown,"X-desc/Y-desc mixed head cleanup and held-before-holder");
 const auto liveContext=held.nativePoolReplacementContext();NativeActorPool replacementOracle;need(replacementOracle.restore(h.nativePool->pool),"live replacement oracle");
 std::vector<NativeActorPoolAllocation> destinationActors;for(int i=0;i<3;++i)destinationActors.push_back(replacementOracle.allocate());
 for(const auto slot:liveContext.releaseOrder)need(replacementOracle.release(slot),"old scene cleanup occurs after destination Actor factories");
 for(int i=0;i<2;++i)replacementOracle.allocate();
 auto liveScene=scene("TEST/LIVE.3DG");GameWorld liveWorld;load(liveWorld,liveScene,l);need(liveWorld.reconstructNativePoolForReplacement(liveContext,error),error);
 need(liveWorld.captureNativePool().pool==replacementOracle.snapshot()&&liveWorld.actors()[0].nativePoolSlot==destinationActors[0].slot,"live replacement parses Actors before cleanup and allocates terrain afterward");
 auto badContext=liveContext;badContext.releaseOrder.push_back(badContext.releaseOrder.front());const auto unchanged=liveWorld.captureNativePool();
 need(!liveWorld.reconstructNativePoolForReplacement(badContext,error)&&liveWorld.captureNativePool()==unchanged,"corrupt cleanup context rejected before mutation");
 auto nextScene=scene("TEST/NEXT.3DG");GameWorld next;load(next,nextScene,l);
 need(next.reconstructNativePoolForReplacement(seed,error),error);
 need(next.actors()[0].nativeGeneration==6&&next.captureNativePool().pool.nextGeneration==11,"new World continues pool sequence without phantom validation allocations");
 need(validNativeWorldPoolSnapshot(next.snapshot()),"new scene has coherent bindings");
 auto carried=held.carry();auto carryingScene=scene("TEST/CARRY.3DG");GameWorld carrying;load(carrying,carryingScene,l);carrying.restore(carried);
 need(carrying.reconstructNativePoolForReplacement(seed,error),error);
 const auto carriedState=carrying.snapshot();need(validNativeWorldPoolSnapshot(carriedState)&&carriedState.heldItems.size()==1,"foreign carried original rebound into destination pool");
 need(carriedState.heldItems[0].actor.nativeGeneration==6&&carriedState.actors[0].nativeGeneration==7&&carriedState.nextNativeGeneration==12,"carried body constructs before destination player and terrain");
 need(!carriedState.heldItems[0].actor.nativeAwake&&carriedState.heldItems[0].actor.nativeAwakeIndex==0,"foreign retained copy never aliases a destination manager index");
 const auto before=next.captureNativePool();need(!next.reconstructNativePoolForReplacement(before,error),"owned live replacement seed rejected");
 need(next.captureNativePool()==before,"failed replacement leaves World allocator unchanged");
 auto corrupt=next.snapshot();corrupt.nativePool->pool.catalog.push_back(corrupt.nativePool->pool.catalog.size());corrupt.nativePool->pool.generations.push_back(999);++corrupt.nativePool->pool.liveCount;
 need(!next.restoreSnapshot(corrupt,error)&&next.captureNativePool()==before,"unowned nonzero slot rejected transactionally");
 // Original raw sequence wraps through0; higher4199d0 skips destroying UID0,
 // retaining an unowned physical slot through later scene replacement.
 auto wrapScene=scene();GameWorld wrap;load(wrap,wrapScene,l);auto end=wrap.snapshot();end.nextNativeGeneration=UINT32_MAX;end.nativePool->pool.nextGeneration=UINT32_MAX;
 need(wrap.restoreSnapshot(end,error),error);const auto high=wrap.spawn({1},{320,160}),zero=wrap.spawn({1},{384,160});
 need(wrap.actors()[high].nativeGeneration==UINT32_MAX&&wrap.actors()[zero].nativeGeneration==0,"raw counter includes UID0");
 const auto zeroSlot=wrap.actors()[zero].nativePoolSlot;AmmoImpactNativeTestAccess::destroy(wrap,zero);
 auto orphan=wrap.snapshot();need(orphan.actors[zero].nativePoolSlot==SIZE_MAX&&validNativeWorldPoolSnapshot(orphan),"UID0 higher destruction detaches body but retains legal orphan slot");
 auto orphanSeed=wrap.nativePoolForReplacement();need(orphanSeed.pool.liveCount==1&&orphanSeed.pool.generations[zeroSlot]==0,"cleanup retains UID0 orphan context");
 auto afterScene=scene("TEST/AFTER.3DG");GameWorld after;load(after,afterScene,l);need(after.reconstructNativePoolForReplacement(orphanSeed,error),error);
 need(after.actors()[0].nativeGeneration==1&&after.captureNativePool().pool.liveCount==6&&validNativeWorldPoolSnapshot(after.snapshot()),"replacement continues after wrap and keeps original orphan allocation");
 // The private loader's disk/retry reconstruction consumes one new body set,
 // despite constructing an authored validation World before restoration.
 auto retryScene=scene();GameWorld retry;load(retry,retryScene,l);need(retry.restoreSnapshot(stored,error),error);
 need(retry.reconstructNativePoolForReplacement(seed,error,true),error);
 need(retry.captureNativePool().pool.nextGeneration==11&&validNativeWorldPoolSnapshot(retry.snapshot()),"retry reconstructs saved bodies once in writer order");
 auto legacy=stored;legacy.nativePool.reset();GameWorld migrated;auto migratedScene=scene();load(migrated,migratedScene,l);need(migrated.restoreSnapshot(legacy,error),error);
 need(!migrated.captureNativePool().originalHistoryKnown&&validNativeWorldPoolSnapshot(migrated.snapshot()),"legacy migration preserves availability limit and produces valid deterministic continuation");
 std::cout<<"PASS pool World/disk/fresh/retry/tiles/held alias/compact Ammo/UID0 orphan/transaction controls\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
