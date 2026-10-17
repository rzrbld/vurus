#include "game/world.h"
#include "game/save_file.h"
#include "game/save_state.h"
#include "game/ordinary_enemy_ai_validation.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
static TextNode block(std::string k,std::vector<TextNode> children,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=std::move(children);if(!type.empty())n.arguments={type};return n;}
// These actively simulated classes mirror authored Managed=1; omission means
// native unmanaged scenery and must not rely on the former all-actors loop.
static SceneContent fixture(const std::string& base="Fixed"){
 SceneContent s;s.mapPath="CPU/T3P1.3DG";ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};p.properties={value("Managed","1"),value("Sub-type","Vyruz"),value("Bounding-box-size","10"),block("Energy",{value("Initial","1000"),value("Max","1000")})};
 ObjectClass w;w.id={1};w.name="Walker";w.behaviours={base,"Seeker"};w.properties={value("Managed","1"),value("Sub-type","Walker"),value("Can-turn-on-place","1"),value("Move-during-path-search","1"),value("Turn-timeout","100"),value("Bounding-box-size","30"),value("Speed-max","15"),value("Speed-min","-15"),value("Acceleration","1"),value("Deceleration","1"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("TYPE",{value("Target","Player"),value("Sensor-Range","600"),value("Scan-Delay","1000"),value("Min-distance","300"),value("Vision-Angle","360")},"Seeker"),block("TYPE",{value("Range","300"),value("Delay","2000")},"Random"),block("TYPE",{value("Path-ID","1"),value("Scan-Delay","250")},"Patrol")};
 s.classes={p,w};for(unsigned i=0;i<2;++i){ObjectPlacement o;o.id={i};o.classId={i};o.position=i?ContentPosition{1000,1000}:ContentPosition{1500,1000};o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;s.objects.push_back(o);}s.player=ObjectId{0};s.paths.push_back({{1},{{1000,1000},{1200,1100},{1000,1200}}, {}});return s;
}
static std::vector<uint8_t> encoded(const GameWorld& w){SaveGame s;s.currentLevel="CPU/T3P1.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t> b;std::string e;need(encodeSave(s,b,e),e);return b;}
static void continuation(GameWorld& w,SceneContent original,const Level& level,int ticks){
 std::string e;auto checkpoint=w.snapshot();GameWorld memory,disk;auto memoryScene=original,diskScene=original;
 need(memory.load(memoryScene,level,e)&&memory.restoreSnapshot(checkpoint,e),e);SaveGame decoded;need(decodeSave(encoded(w),decoded,e),e);
 need(disk.load(diskScene,level,e)&&disk.restoreSnapshot(decoded.levels.begin()->second.world,e),e);
 for(int t=0;t<ticks;++t){w.tick(.017f,{});memory.tick(.017f,{});disk.tick(.017f,{});need(encoded(w)==encoded(memory),"Walker memory continuation");need(encoded(w)==encoded(disk),"Walker disk continuation");}
}
int main(){try{
 Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);std::string e;
 auto scene=fixture();const auto original=scene;GameWorld w;need(w.load(scene,l,e),e);need(w.actors()[1].ordinaryAIConfig.enabled,"Walker migrated, other families retain accepted controller");
 w.tick(.017f,{});auto pending=w.snapshot();need(pending.actors[1].ordinaryAI->targeting.clock.searching&&pending.actors[1].ordinaryAI->targeting.search.has_value(),"First due Seeker starts native graph without running a wave");
 need(pending.actors[1].route.empty()&&!pending.actors[1].routeSearch,"Walker does not allocate old surface A* frontier");continuation(w,original,l,2);
 need(w.actors()[1].ordinaryAI->targeting.clock.active,"Direct multi-angle Walker route accepted without Spider cardinal restriction");need(w.actors()[1].enemyMoveCommand==200,"Fixed Walker advances while Seeker owns active route");continuation(w,original,l,120);
 auto failed=pending;failed.actors[1].ordinaryAI->targeting.search->candidates.clear();failed.actors[1].ordinaryAI->targeting.saved.back().clock.scanRemainingMs=29;
 auto failedScene=original;GameWorld failure;need(failure.load(failedScene,l,e)&&failure.restoreSnapshot(failed,e),e);failure.tick(.017f,{});
 need(failure.actors()[1].ordinaryAI->targeting.clock.scanRemainingMs==29&&!failure.actors()[1].ordinaryAI->targeting.search,"Failed wave restores saved action after pre-rollback scan increment");
 const auto failedSnapshot=failure.snapshot();size_t livePoints=0;for(const auto& o:failedSnapshot.spiderNavigation->objects)livePoints+=o.alive&&o.point;need(livePoints==0,"Failed replacement releases its complete seen array");
 continuation(failure,original,l,30);
 for(const std::string base:{"Random","Patrol"}){
  auto baseScene=fixture(base);baseScene.objects[0].position={5000,2500};const auto authored=baseScene;GameWorld baseWorld;need(baseWorld.load(baseScene,l,e),e);
  baseWorld.tick(.017f,{});const auto& state=*baseWorld.actors()[1].ordinaryAI;
  need(state.base.clock.searching&&state.base.search.has_value(),"Rejected Seeker admits independent native base search");
  need((baseWorld.actors()[1].enemyMoveCommand==200)==(base=="Patrol"),"Only Patrol default reads Move-during-path-search");
  continuation(baseWorld,authored,l,120);
 }
 // A Seeker commit can cancel a base replacement while its selected cache
 // still refers to the saved base's old points. Disposal must not leave a
 // false live graph claim, lose the retained cursor, or prevent disk saves.
 auto raceScene=fixture("Random");const auto raceOriginal=raceScene;GameWorld race;need(race.load(raceScene,l,e),e);race.tick(.017f,{});auto racing=race.snapshot();
 auto& base=racing.actors[1].ordinaryAI->base;auto& old=base.saved.back();const auto oldA=insertNativeNavigationPoint(*racing.spiderNavigation,{500,500}),oldB=insertNativeNavigationPoint(*racing.spiderNavigation,{600,500});old.points=old.ownedPoints={oldA,oldB};old.cursor=1;base.points=old.points;base.cursor=1;
 need(race.restoreSnapshot(racing,e),e);race.tick(.017f,{});const auto cancelled=race.snapshot();const auto& cancelledBase=cancelled.actors[1].ordinaryAI->base;
 need(cancelledBase.saved.empty()&&!cancelledBase.clock.searching&&cancelledBase.search.has_value(),"Seeker commit cancels base flags and saved chain while retaining unfinished ownership");
 need(cancelledBase.cursor==1&&cancelledBase.points.size()==2&&cancelledBase.points.front().graphNode==SIZE_MAX,"Disposed selected cache retains values with no live graph claim");
 need(validMissileNativeNavigationSnapshot(cancelled),"Cancelled base cache remains save-valid");continuation(race,raceOriginal,l,30);
 // Restore must derive caches from the checkpoint difficulty, not the world's
 // pre-existing policy. Synthetic policies deliberately use noninteger ratios.
 auto policyScene=fixture();TextDocument doc;for(int d=0;d<4;++d){doc.nodes.push_back(value("difficulty-level",std::to_string(d)));doc.nodes.push_back(value("nmy-scan-delay-ratio",d==3?"0.8":"1"));doc.nodes.push_back(value("nmy-scan-range-ratio",d==3?"1.2":"1"));}policyScene.classDocuments.push_back(doc);
 auto hardScene=policyScene,easyScene=policyScene;GameWorld hard,easy;need(hard.load(hardScene,l,e,3)&&easy.load(easyScene,l,e,0),e);need(easy.restoreSnapshot(hard.snapshot(),e),e);
 need(easy.actors()[1].ordinaryAIConfig.targeting.scanPeriodMs==800&&easy.actors()[1].ordinaryAIConfig.sensorRange==720,"Restore applies checkpoint difficulty to class-derived integer action cache");
 auto corpseScene=original;GameWorld corpse;need(corpse.load(corpseScene,l,e),e);corpse.damage(1,1e9f,0);need(!corpse.actors()[1].alive&&!corpse.actors()[1].ordinaryAI,"Walker death disposes native actions");
 auto corpseRestoreScene=original;GameWorld corpseRestore;need(corpseRestore.load(corpseRestoreScene,l,e)&&corpseRestore.restoreSnapshot(corpse.snapshot(),e),e);
 need(!corpseRestore.actors()[1].ordinaryAI,"Dead terminal Walker restore does not manufacture a new live action");
 SaveGame corpseDisk;need(decodeSave(encoded(corpseRestore),corpseDisk,e),e);need(corpseRestore.restoreSnapshot(corpseDisk.levels.begin()->second.world,e),"Dead Walker restore/resave/restore remains valid");
 auto off=pending;off.actors[1].alive=false;off.actors[1].reactivateMs=10;auto offScene=original;GameWorld offWorld;need(offWorld.load(offScene,l,e)&&offWorld.restoreSnapshot(off,e),e);need(offWorld.actors()[1].ordinaryAI.has_value(),"Temporarily disabled Walker retains action for reactivation");offWorld.tick(.017f,{});need(offWorld.actors()[1].alive,"Disabled Walker reactivates with native state");
 std::cout<<"Walker pending/following/failure, independent bases, native graph ownership, restored difficulty and v31 continuation passed\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
