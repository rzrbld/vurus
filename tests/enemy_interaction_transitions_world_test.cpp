// Coupled native interaction adapters, not an original Windows playthrough.
#define main retainedWalkerTransitionFixture
#include "ordinary_enemy_ai_world_test.cpp"
#undef main
#include "snapshot_fixture.h"
#include "game/properties.h"
#include <set>
namespace {
void set(std::vector<TextNode>& p,const std::string& key,const std::string& text){for(auto& n:p)if(contentNameIs(n.key,key)){n.values={text};return;}p.push_back(value(key,text));}
SceneContent interactionFixture(const std::string& family,const std::string& base,const std::string& mode){
 auto s=fixture(base);auto&c=s.classes[1];c.name=family;c.behaviours={base,mode};set(c.properties,"Sub-type",family);set(c.properties,"Speed-max","0");set(c.properties,"Speed-min","0");set(c.properties,"Move-during-path-search","0");set(c.properties,"Two-pieces",family=="Turret"||family=="Tank"?"1":"0");set(c.properties,"Turret-turn-timeout","75");
 for(auto&n:c.properties)if(contentNameIs(n.key,"type")&&!n.arguments.empty()&&contentNameIs(n.arguments[0],"seeker"))n.arguments[0]=mode;
 s.objects[1].properties=c.properties;s.objects[1].behaviours=c.behaviours;
 // A separately allocated matching-class target proves identity/cell selection
 // and target destruction without terminating the controlled player's lifetime.
 auto target=s.objects[0];target.id={2};target.position={1500,1000};s.objects.push_back(target);s.objects[0].position={5000,2500};
 ObjectClass blocker;blocker.id={2};blocker.name="Blocking-Thing";blocker.behaviours={"Fixed"};blocker.properties={value("Sub-type","Thing"),value("Managed","0"),value("Bounding-box-size","100"),block("Energy",{value("Initial","1000"),value("Max","1000")})};s.classes.push_back(blocker);return s;
}
bool terminal(const NativeEnemyRouteCore&r){return r.cursor>=r.points.size()||r.cursor+1==r.points.size();}
void valid(const GameWorld&w){need(validMissileNativeNavigationSnapshot(w.snapshot()),"transition keeps every live graph point owned");}
void active(GameWorld&w){for(int i=0;i<12&&!w.actors()[1].ordinaryAI->targeting.clock.active;++i)w.tick(.017f,{});need(w.actors()[1].ordinaryAI->targeting.clock.active&&w.actors()[1].ordinaryAI->targeting.target==2,"actual matching-class target owns active native route");valid(w);}
bool baseReturned(const GameWorld&w,const std::string&base){const auto&s=*w.actors()[1].ordinaryAI;return !s.targeting.clock.active&&!s.targeting.clock.searching&&nativeEnemyBaseActionAdmitted(s.actorFlags)&&(base=="Fixed"?w.actors()[1].enemyMoveCommand==0:s.lastAction==(base=="Patrol"?12:4));}
}
namespace vyruz {struct AmmoImpactNativeTestAccess {
 static void hide(GameWorld&w,size_t id){w.scene_->objects[id].flags.invisible=true;w.updateSpiderNavigation(id);}
 static void relocate(GameWorld&w,size_t id,ContentPosition p){w.scene_->objects[id].position=p;const auto n=spiderSurfaceToNative(p.x,p.y);if(auto*h=w.actors_[id].nativeMotionHistory())*h={{n.x,n.y},p};w.synchronizeActorCell(id);w.updateSpiderNavigation(id);}
};}
int main(){try{Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);std::string e;unsigned loss=0,blocked=0,interior=0;
 for(const std::string family:{"Cop","Burner","Walker","Jeep","Roller","Heli","Turret","Tank"})for(const std::string mode:{"Seeker","Tracker"})for(const std::string base:{"Fixed","Patrol","Random"}){
  for(bool death:{false,true}){auto scene=interactionFixture(family,base,mode);const auto authored=scene;GameWorld w;need(w.load(scene,l,e),e);active(w);need(terminal(w.actors()[1].ordinaryAI->targeting),"unblocked acquired route is terminal");continuation(w,authored,l,3);const auto prior=w.actors()[1].ordinaryAI->targeting.clock.scanRemainingMs;need(prior>17,"loss fixture has a genuine retained future scan");
   if(death){w.damage(2,1e9f,std::nullopt);need(!w.actors()[2].alive&&w.actors()[2].nativeGeneration==0,"real target full death releases identity");}else AmmoImpactNativeTestAccess::hide(w,2);
   // Retention is checked on the original due scan, never immediately on flag change.
   need(w.actors()[1].ordinaryAI->targeting.clock.active,"changing target does not reset native action immediately");w.tick(.017f,{});need(w.actors()[1].ordinaryAI->targeting.clock.active,"positive scan countdown retains active route");
   bool returned=false;for(int tick=0;tick<200&&!returned;++tick){w.tick(.017f,{});valid(w);returned=baseReturned(w,base);}need(returned,"due target loss admits independent original base: "+family+"/"+mode+"/"+base);continuation(w,authored,l,5);++loss;
  }
  auto scene=interactionFixture(family,base,mode);const auto authored=scene;GameWorld w;need(w.load(scene,l,e),e);active(w);const auto old=w.actors()[1].ordinaryAI->targeting.sampledTarget;
  const auto blocker=w.spawn(ClassId{2},{1250,1000});need(blocker<w.actors().size(),"real blocking Thing factory");
  // Native active validation tests target/corridor, not a newly invented immediate
  // obstacle ray. Move the target to trigger its real due terminal resample.
  AmmoImpactNativeTestAccess::relocate(w,2,{old.x+8,old.y});bool pending=false;for(int tick=0;tick<200&&!pending;++tick){w.tick(.017f,{});valid(w);pending=w.actors()[1].ordinaryAI->targeting.clock.searching;}
  need(pending,"due moved target starts a real replacement search around inserted Thing");const auto pendingRoute=w.actors()[1].ordinaryAI->targeting;need(!pendingRoute.saved.empty(),"blocked replacement retains prior complete action snapshot");const auto prior=pendingRoute.saved.back();
  bool finished=false;for(int tick=0;tick<300&&!finished;++tick){w.tick(.017f,{});valid(w);finished=!w.actors()[1].ordinaryAI->targeting.clock.searching;}
  need(finished,"native blocked wave reaches a decision");const auto result=w.actors()[1].ordinaryAI->targeting;
  if(result.clock.active)need(result.points.size()>2,"accepted blocked route must own its corner path");
  else need(result.saved.empty()&&result.points==prior.points&&result.ownedPoints==prior.ownedPoints&&result.cursor==prior.cursor&&result.clock.scanRemainingMs==prior.clock.scanRemainingMs,"failed blocked wave restores prior cache/ownership/signed clock exactly");
  continuation(w,authored,l,3);++blocked;
  w.damage(blocker,1e9f,std::nullopt);need(!w.actors()[blocker].alive&&w.actors()[blocker].nativeGeneration==0,"real Thing removal clears obstruction identity");
  bool recovered=false;for(int tick=0;tick<300&&!recovered;++tick){w.tick(.017f,{});valid(w);const auto&r=w.actors()[1].ordinaryAI->targeting;recovered=r.clock.active&&r.sampledTarget.x==old.x+8;}
  need(recovered,"unblocked moved target reacquires a direct native route");continuation(w,authored,l,3);
  // Supply a legal owned INTERIOR action state to isolate42c5a0's different
  // retention predicate. This is not an invented authored Patrol fallback.
  auto supplied=w.snapshot();auto&r=supplied.actors[1].ordinaryAI->targeting;need(r.points.size()>=2,"unblocked route has selected endpoints");const auto first=r.points.front(),last=r.points.back();const auto middle=insertNativeNavigationPoint(*supplied.spiderNavigation,{(first.x+last.x)*.5f,(first.y+last.y)*.5f});r.points={first,middle,last};r.ownedPoints.push_back(middle);r.cursor=1;r.segmentStart=first;r.segmentEnd=middle;r.desiredHeading=spiderContactHeading({middle.x-r.segmentStart.x,middle.y-r.segmentStart.y});need(w.restoreSnapshot(supplied,e),e);
  // Interior42c5a0 validation ignores visibility while target remains in its
  // sampled cell; terminal visibility is a different original branch.
  if(!terminal(w.actors()[1].ordinaryAI->targeting)){AmmoImpactNativeTestAccess::hide(w,2);
   bool rescan=false;for(int tick=0;tick<200&&!rescan;++tick){const auto prior=w.actors()[1].ordinaryAI->targeting.clock.scanRemainingMs;w.tick(.017f,{});valid(w);if(prior<=0){need(w.actors()[1].ordinaryAI->targeting.clock.active&&w.actors()[1].ordinaryAI->targeting.target==2,"interior same-cell hidden target remains retained at due scan");rescan=true;}}
   need(rescan,"interior due-scan control actually executes");continuation(w,authored,l,5);++interior;
  }
 }
 unsigned spiderLoss=0,spiderBlocked=0;
 for(bool death:{false,true}){auto scene=interactionFixture("Spider","Patrol","Seeker");const auto delta=nativeToSurface(400,0);scene.objects[0].position={1000+delta.x,1000+delta.y};scene.objects[2].position={5000,2500};const auto authored=scene;GameWorld w;need(w.load(scene,l,e),e);for(int tick=0;tick<20&&!w.actors()[1].spiderAI->seekerRoute.action.active;++tick)w.tick(.017f,{});need(w.actors()[1].spiderAI->seekerRoute.action.active&&w.actors()[1].spiderAI->seekerRoute.target==0,"Spider genuine cardinal target route");need(w.actors()[1].spiderAI->seekerRoute.points.size()==2,"Spider active target routes are terminal by original admission");continuation(w,authored,l,3);
  if(death)w.damage(0,1e9f,std::nullopt);else AmmoImpactNativeTestAccess::hide(w,0);need(w.actors()[1].spiderAI->seekerRoute.action.active,"Spider target change does not reset action immediately");w.tick(.017f,{});need(w.actors()[1].spiderAI->seekerRoute.action.active,"Spider positive scan countdown retains terminal route");bool returned=false;for(int tick=0;tick<200&&!returned;++tick){w.tick(.017f,{});valid(w);const auto&a=*w.actors()[1].spiderAI;returned=!a.seekerRoute.action.active&&!a.seekerRoute.action.searching&&(!(a.actorFlags&1)||(a.actorFlags&2))&&a.patrol.initialized;}
  need(returned,"Spider due terminal loss resumes authored Patrol");continuation(w,authored,l,5);++spiderLoss;
 }
 {auto scene=interactionFixture("Spider","Patrol","Seeker");const auto delta=nativeToSurface(400,0);scene.objects[0].position={1000+delta.x,1000+delta.y};scene.objects[2].position={5000,2500};const auto authored=scene;GameWorld w;need(w.load(scene,l,e),e);for(int tick=0;tick<20&&!w.actors()[1].spiderAI->seekerRoute.action.active;++tick)w.tick(.017f,{});need(w.actors()[1].spiderAI->seekerRoute.action.active,"Spider cardinal pre-block route");const auto middle=nativeToSurface(200,0);const auto obstacle=w.spawn(ClassId{2},{1000+middle.x,1000+middle.y});const auto moved=nativeToSurface(408,0);w.setPlayerPosition(1000+moved.x,1000+moved.y);bool pending=false;for(int tick=0;tick<200&&!pending;++tick){w.tick(.017f,{});valid(w);pending=w.actors()[1].spiderAI->seekerRoute.action.searching;}need(pending,"Spider actual blocker/moved target starts replacement");bool finished=false;for(int tick=0;tick<300&&!finished;++tick){w.tick(.017f,{});valid(w);finished=!w.actors()[1].spiderAI->seekerRoute.action.searching;}need(finished&&!w.actors()[1].spiderAI->seekerRoute.action.active,"Spider rejects blocked/noncardinal replacement under original direct/cardinal gate");continuation(w,authored,l,3);w.damage(obstacle,1e9f,std::nullopt);bool recovered=false;for(int tick=0;tick<300&&!recovered;++tick){w.tick(.017f,{});valid(w);const auto&r=w.actors()[1].spiderAI->seekerRoute;recovered=r.action.active&&r.sampledTarget.x==scene.objects[0].position.x;}need(recovered,"Spider recovers direct cardinal route after blocker removal");continuation(w,authored,l,5);++spiderBlocked;
 }
 std::cout<<"PASS "<<loss<<" terminal hidden/dead target-to-base transitions, "<<blocked<<" real Thing blocked search/rollback/recovery controls, "<<interior<<" interior hidden-target retention controls; Spider "<<spiderLoss<<" terminal losses/"<<spiderBlocked<<" blocked-recovery with graph ownership and memory/disk continuations\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
