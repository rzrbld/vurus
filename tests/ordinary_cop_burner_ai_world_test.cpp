#define main retainedWalkerControllerTests
#include "ordinary_enemy_ai_world_test.cpp"
#undef main
#include "game/spider_contact_native.h"
#include "game/native_navigation_filter.h"
#include "snapshot_fixture.h"
namespace {
void replace(auto& p,const std::string& key,const std::string& text){for(auto& n:p)if(n.key==key){n.values={text};return;}p.push_back(value(key,text));}
SceneContent familyFixture(std::string family,std::string base="Fixed",std::string action="Seeker",bool guardian=false){
 auto s=fixture(base);auto& c=s.classes[1];c.name=family;c.behaviours={base,action};if(guardian)c.behaviours.push_back("Guardian");replace(c.properties,"Sub-type",family);replace(c.properties,"Move-during-path-search","0");replace(c.properties,"Bounding-box-size","25");replace(c.properties,"Speed-max","0");replace(c.properties,"Speed-min","0");
 for(auto& n:c.properties)if(n.kind==TextKind::Block&&n.key=="TYPE"&&!n.arguments.empty()&&n.arguments[0]=="Seeker"){
  n.arguments[0]=action;replace(n.children,"Sensor-Range",family=="Cop"?"500":"600");replace(n.children,"Scan-Delay",family=="Cop"?"400":"50");replace(n.children,"Min-distance",family=="Cop"?"250":"200");
 }
 auto pair=[](std::string key){auto n=value(key,"-1");n.values.push_back("-1");return n;};if(guardian)c.properties.push_back(block("TYPE",{pair("Location"),pair("Center"),value("Range","300")},"Guardian"));
 s.objects[1].properties=c.properties;s.objects[1].behaviours=c.behaviours;return s;
}
void moveObject(WorldSnapshot& snapshot,size_t id,ContentPosition position){snapshot.objects[id].position=position;const auto n=spiderSurfaceToNative(position.x,position.y);snapshot.actors[id].nativeMotion=SpiderMotionHistory{{n.x,n.y},position};if(snapshot.actorCells)snapshot.actorCells->relocate(id,snapshot.actorCells->coordinate(position.x,position.y),snapshot.nextNativeCellOrder++);}
void appendStationaryAmmo(WorldSnapshot& snapshot,const SceneContent& scene,Projectile shot){
 vyruz_test::appendProjectile(snapshot,scene,shot);auto& stored=snapshot.projectiles.back();const auto p=stored.nativeBareBody->motion.center;
 need(snapshot.spiderNavigation.has_value(),"stationary Ammo fixture requires native graph");stored.spiderGraphNode=insertNativeNavigationObject(*snapshot.spiderNavigation,(uint64_t(1)<<62)|stored.nativeBodyIdentity,{p.x-stored.radius,p.y-stored.radius,p.x+stored.radius,p.y+stored.radius});
}
}
int main(){try{Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);std::string e;unsigned actions=0;
 //42caa0 refreshes the endpoint from the target;42c9d0/4075c0 store
 //heading0 when it returns exactly to the retained segment start. Exercise
 //real route creation and a powered approach without injecting an action.
 need(spiderContactHeading({0,0})==24,"shared contact zero-heading sentinel remains unchanged");
 for(bool powered:{false,true})for(bool coincident:{false,true}){
  auto scene=familyFixture("Burner");
  if(powered){auto& p=scene.classes[1].properties;
   for(const auto& [key,text]:{std::pair{"Speed-max","20"},{"Speed-min","-10"},{"Use-inertia","1"},{"Grip","1"},{"Friction-factor","0.97"},{"Friction-threshold","0.01"},{"Acceleration-step","1.5"},{"Decceleration-step","0.5"}})replace(p,key,text);
   p.push_back(block("Weight",{value("Initial","10")}));
   p.push_back(block("Thrust",{value("Initial","20"),value("Min","-20"),value("Max","20")}));
   scene.objects[1].properties=p;
  }
  const auto authored=scene;GameWorld world;need(world.load(scene,l,e),e);
  if(powered){auto initial=world.snapshot();auto& actor=initial.actors[1];
   // Ordinary motion history is created on its first AI visit, not load.
   const auto from=spiderSurfaceToNative(initial.objects[1].position.x,initial.objects[1].position.y);
   const auto to=spiderSurfaceToNative(initial.objects[0].position.x,initial.objects[0].position.y);
   actor.direction=spiderContactHeading({to.x-from.x,to.y-from.y});
   actor.enemyTurn=initializeEnemyTurn(actor.enemyTurnConfig,actor.direction,0);
   need(world.restoreSnapshot(initial,e),e);
  }
  bool ready=false;
  for(int tick=0;tick<600;++tick){world.tick(.017f,{});
   const auto& actor=world.actors()[1];const auto& route=actor.ordinaryAI->targeting;
   const auto center=actor.nativeMotion->center;
   const float dx=center.x-route.segmentStart.x,dy=center.y-route.segmentStart.y;
   if(route.clock.active&&route.clock.scanRemainingMs>0&&(!powered||(dx*dx+dy*dy>3600&&dx*dx+dy*dy<40000))){ready=true;break;}
  }
  need(ready,"Burner follows an authored-origin route before target return");
  const auto start=world.actors()[1].ordinaryAI->targeting.segmentStart;
  const auto birth=spiderSurfaceToNative(1000,1000);
  if(!(start==NativeNavPoint{birth.x,birth.y}))std::cerr<<std::hexfloat<<"Burner start powered="<<powered<<" coincident="<<coincident<<" actual="<<start.x<<','<<start.y<<" expected="<<birth.x<<','<<birth.y<<'\n';
  need(start==NativeNavPoint{birth.x,birth.y},"Burner retains its original route start through powered movement");
  world.setPlayerPosition(coincident?1000:1001,1000);world.tick(.017f,{});
  const auto& route=world.actors()[1].ordinaryAI->targeting;
  const int expected=coincident?0:spiderContactHeading({route.segmentEnd.x-start.x,route.segmentEnd.y-start.y});
  need(route.clock.active&&(route.status&4)&&route.desiredHeading==expected,"Burner target return stores native zero or nonzero route heading");
  need((route.segmentEnd==start)==coincident,"nearby-target control is not a zero segment");
  continuation(world,authored,l,30);
 }
 //Original graph queries ignore Ammo even though its real collision bounds
 //remain enrolled. A large non-Ammo Thing at the same coordinates must still
 //obstruct the search; simply removing all nonsolid bounds is incorrect.
 need(!nativeNavigationAnimatedActorAdmitted(3,0x10)&&nativeNavigationAnimatedActorAdmitted(3,0x18)&&nativeNavigationAnimatedActorAdmitted(2,0x10),"original navigation filter distinguishes animated Ammo, Player and decor");
 for(const std::string family:{"Cop","Burner"})for(const std::string obstacle:{"Ammo","BareAmmo","Thing"}){
  auto scene=familyFixture(family);ObjectClass barrier;barrier.id={2};barrier.name="Route-Barrier";
  barrier.properties={value("Sub-type",obstacle=="BareAmmo"?"Ammo":obstacle),value("Bounding-box-size","100"),block("Energy",{value("Initial","1000"),value("Max","1000")})};
  scene.classes.push_back(barrier);if(obstacle!="BareAmmo"){ObjectPlacement object;object.id={2};object.classId={2};object.position={1250,1000};object.properties=barrier.properties;scene.objects.push_back(object);}
  GameWorld world;need(world.load(scene,l,e),e);
  if(obstacle=="BareAmmo"){
   auto initial=world.snapshot();Projectile shot;shot.classId={2};shot.owner=0;shot.position={1250,1000};shot.radius=100;shot.lifetimeMs=10000;shot.range=10000;shot.ageMs=1000;
   appendStationaryAmmo(initial,scene,shot);need(world.restoreSnapshot(initial,e),e);
  }
  world.tick(.017f,{});world.tick(.017f,{});
  const auto state=world.snapshot();const auto& route=state.actors[1].ordinaryAI->targeting;
  const bool direct=route.clock.active&&route.points.size()==2;
  need(direct==(obstacle!="Thing"),family+" navigation ignores Ammo but retains Thing obstruction");
  bool enrolled=false;for(const auto& node:state.spiderNavigation->objects)enrolled|=node.alive&&!node.point&&(obstacle=="BareAmmo"?bool(node.reference&(uint64_t(1)<<62)):node.reference==2);
  need(enrolled,"Ammo navigation exclusion retains its real physical graph bounds");
 }
 //A persistent combat object must not continually destroy a
 //moving-target route. Ignore-owner is a native physical-contact rule here:
 //the pursuer owns this stationary Ammo, so it can cross without a collision.
 //Both worlds receive identical target movement through several scan periods.
 for(const std::string family:{"Cop","Burner"}){
  auto quietScene=familyFixture(family);auto& p=quietScene.classes[1].properties;
  replace(p,"Speed-max",family=="Burner"?"20":"25");replace(p,"Speed-min","-10");replace(p,"Use-inertia","1");replace(p,"Grip","1");replace(p,"Friction-factor","0.97");replace(p,"Friction-threshold","0.01");replace(p,"Acceleration-step","1.5");replace(p,"Decceleration-step","0.5");
  p.push_back(block("Weight",{value("Initial","10")}));p.push_back(block("Thrust",{value("Initial","20"),value("Min","-20"),value("Max","20")}));quietScene.objects[1].properties=p;
  quietScene.objects[0].position={1600,1000};ObjectClass ammo;ammo.id={2};ammo.name="Stationary-Combat-Ammo";ammo.properties={value("Sub-type","Ammo"),value("Bounding-box-size","30"),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("TYPE",{value("Ignore-owner","1")},"Ammo")};quietScene.classes.push_back(ammo);
  auto combatScene=quietScene;GameWorld quiet,combat;need(quiet.load(quietScene,l,e)&&combat.load(combatScene,l,e),e);
  for(auto* world:{&quiet,&combat}){auto aligned=world->snapshot();const auto from=spiderSurfaceToNative(aligned.objects[1].position.x,aligned.objects[1].position.y),to=spiderSurfaceToNative(aligned.objects[0].position.x,aligned.objects[0].position.y);auto& actor=aligned.actors[1];actor.direction=spiderContactHeading({to.x-from.x,to.y-from.y});actor.enemyTurn=initializeEnemyTurn(actor.enemyTurnConfig,actor.direction,0);need(world->restoreSnapshot(aligned,e),e);}
  auto initial=combat.snapshot();Projectile shot;shot.classId={2};shot.owner=1;shot.ignoreOwner=true;shot.position={1350,1000};shot.radius=30;shot.lifetimeMs=20000;shot.range=100000;shot.ageMs=1000;
  appendStationaryAmmo(initial,combatScene,shot);need(combat.restoreSnapshot(initial,e),"persistent Ammo initialization: "+e);
  unsigned forward=0,reacquired=0;bool wasSearching=false;const auto start=combatScene.objects[1].position;
  for(unsigned tick=0;tick<240;++tick){
   const ContentPosition target{1600+std::sin(float(tick)*.023f)*25,1000+std::sin(float(tick)*.035f)*35};
   auto q=quiet.snapshot(),c=combat.snapshot();moveObject(q,0,target);moveObject(c,0,target);need(quiet.restoreSnapshot(q,e),"moving-target quiet tick "+std::to_string(tick)+": "+e);need(combat.restoreSnapshot(c,e),"moving-target combat tick "+std::to_string(tick)+": "+e);quiet.tick(.017f,{});combat.tick(.017f,{});
   const auto& qa=quiet.actors()[1];const auto& ca=combat.actors()[1];const auto& qr=qa.ordinaryAI->targeting;const auto& cr=ca.ordinaryAI->targeting;
   need(quietScene.objects[1].position.x==combatScene.objects[1].position.x&&quietScene.objects[1].position.y==combatScene.objects[1].position.y&&qa.direction==ca.direction&&qa.enemyMoveCommand==ca.enemyMoveCommand&&qr.clock.searching==cr.clock.searching&&qr.clock.active==cr.clock.active,family+" sustained moving-target pursuit matches quiet control despite persistent Ammo");
   forward+=ca.enemyMoveCommand==200;if(wasSearching&&cr.clock.active)++reacquired;wasSearching=cr.clock.searching;
  }
  const auto travelled=std::sqrt(nativeDistanceSquared(start,combatScene.objects[1].position));need(forward>30&&reacquired>2&&travelled>80,family+" pursuit advances and reacquires across original scan periods (forward="+std::to_string(forward)+", acquired="+std::to_string(reacquired)+", distance="+std::to_string(travelled)+")");
  need(combat.projectiles().size()==1&&combat.projectiles()[0].alive,"persistent combat Ammo survives native owner contact exemption");
 }
 // Native LAB441d11 retains ordinary inertia for Cop/Burner; only the
 // Walker/Spider/Tank branch clears velocity after an unconstrained move.
 // Earlier action fixtures deliberately had zero Speed-max, so also exercise
 // authored nonzero propulsion and a neutral coasting pass here.
 for(const std::string family:{"Cop","Burner","Walker"}){
  auto scene=familyFixture(family);auto& p=scene.classes[1].properties;
  replace(p,"Speed-max",family=="Burner"?"20":"25");replace(p,"Speed-min","-10");replace(p,"Use-inertia","1");replace(p,"Grip","1");replace(p,"Friction-factor","0.97");replace(p,"Friction-threshold","0.01");replace(p,"Acceleration-step","1.5");replace(p,"Decceleration-step","0.5");
  p.push_back(block("Weight",{value("Initial","10")}));p.push_back(block("Thrust",{value("Initial","20"),value("Min","-20"),value("Max","20")}));scene.objects[1].properties=p;
  scene.objects[0].position={5000,2500};const auto authored=scene;GameWorld coast;need(coast.load(scene,l,e),e);auto state=coast.snapshot();state.actors[1].contactVelocity={.5f,.75f};state.actors[1].enemyMagnitude=std::hypot(.5f,.75f);need(coast.restoreSnapshot(state,e),e);coast.tick(.014f,{});
  const auto& c=coast.actors()[1];need(c.enemyMoveCommand==0&&std::abs(c.enemyMagnitude-std::hypot(.485f,.7275f))<.00001f,"native quiet pass retains solver magnitude");
  if(family=="Walker")need(c.contactVelocity.x==0&&c.contactVelocity.z==0,"native small family clears successful-step momentum");
  else need(std::abs(c.contactVelocity.x-.485f)<.00001f&&std::abs(c.contactVelocity.z-.7275f)<.00001f,"Cop/Burner retain successful-step inertia instead of erasing velocity");
  continuation(coast,authored,l,10);
  if(family!="Walker"){
   auto chase=authored;chase.objects[0].position={597.50776f,1201.24612f};const auto original=chase;GameWorld moving;need(moving.load(chase,l,e),e);auto aligned=moving.snapshot();aligned.actors[1].direction=0;aligned.actors[1].enemyTurn=initializeEnemyTurn(aligned.actors[1].enemyTurnConfig,0,0);need(moving.restoreSnapshot(aligned,e),e);float peak=0;for(int tick=0;tick<40;++tick){moving.tick(.014f,{});peak=std::max(peak,moving.actors()[1].enemyMagnitude);}
   need(peak>1.5f&&std::sqrt(nativeDistanceSquared(original.objects[1].position,chase.objects[1].position))>40,"nonzero-speed Cop/Burner pursuit accumulates native inertia");continuation(moving,original,l,10);
  }
 }
 for(const std::string family:{"Cop","Burner"})for(const std::string mode:{"Seeker","Tracker"})for(const std::string base:{"Fixed","Patrol","Random"}){
  auto scene=familyFixture(family,base,mode);const auto original=scene;GameWorld w;need(w.load(scene,l,e),e);const auto& cfg=w.actors()[1].ordinaryAIConfig;need(cfg.enabled&&cfg.targeting.subtype==(family=="Cop"?0x16:0x1a)&&cfg.patrolArrival==64,"Cop/Burner native subtype and ordinary Patrol arrival");
  w.tick(.017f,{});const auto pending=w.snapshot();need(pending.actors[1].ordinaryAI->targeting.clock.searching,"first targeting visit creates native action without wave");need(pending.actors[1].route.empty()&&!pending.actors[1].routeSearch,"migrated families do not allocate port A*");continuation(w,original,l,3);need(w.actors()[1].ordinaryAI->targeting.clock.active,"Cop/Burner multi-angle target route admitted");continuation(w,original,l,45);
  auto failed=pending;failed.actors[1].ordinaryAI->targeting.search->candidates.clear();failed.actors[1].ordinaryAI->targeting.saved.back().clock.scanRemainingMs=29;auto failureScene=original;GameWorld failure;need(failure.load(failureScene,l,e)&&failure.restoreSnapshot(failed,e),e);failure.tick(.017f,{});need(failure.actors()[1].ordinaryAI->targeting.clock.scanRemainingMs==29,"failed action restores saved signed scan clock");need(validMissileNativeNavigationSnapshot(failure.snapshot()),"failed Cop/Burner graph ownership remains valid");continuation(failure,original,l,3);
  if(base!="Fixed"){auto away=familyFixture(family,base,mode);away.objects[0].position={5000,2500};const auto authored=away;GameWorld b;need(b.load(away,l,e),e);b.tick(.017f,{});need(b.actors()[1].ordinaryAI->base.clock.searching,"unacquired target admits separate Random/Patrol native action");need(b.actors()[1].enemyMoveCommand==0,"authored Move-during-path-search0 does not invent forward motion");continuation(b,authored,l,30);}
  ++actions;
 }
 // First matching cell-list member wins, including a second instance of the
 // target class. This cannot be represented by the former player-only brain.
 //The HUD bulb uses this manager-cycle result, not acquisition alone or the
 //500ms warning text. A matching non-player instance must not light it.
 for(bool otherTarget:{false,true}){
  auto scene=familyFixture("Cop");
  if(otherTarget){auto target=scene.objects[0];target.id={2};target.position={1480,1008};scene.objects.push_back(target);}
  GameWorld w;need(w.load(scene,l,e),e);need(!w.playerLocked(),"level load clears transient HUD lock");
  for(int tick=0;tick<3;++tick)w.tick(.017f,{});
  need(w.actors()[1].ordinaryAI->targeting.clock.active,"HUD lock fixture follows active native action");
  need(w.playerLocked()==!otherTarget,"only a native target action against the player lights HUD lock");
  if(!otherTarget){
   const auto locked=w.snapshot();w.setDebugEnemyAI(false);w.tick(.017f,{});
   need(!w.playerLocked(),"next manager pass clears previous HUD lock");
   bool warning=false;for(const auto& notice:w.notifications().entries())warning|=notice.kind==NoticeKind::Warning;
   need(warning,"HUD unlock remains independent of lingering warning text");
   need(w.restoreSnapshot(locked,e),e);need(!w.playerLocked(),"checkpoint restores no transient HUD lock");
   w.setDebugEnemyAI(true);w.tick(.017f,{});need(w.playerLocked(),"restored native action rebuilds HUD lock");
  }
 }
 for(bool invisible:{false,true}){auto scene=familyFixture("Cop");auto target=scene.objects[0];target.id={2};target.position={1480,1008};target.flags.invisible=invisible;scene.objects.push_back(target);GameWorld w;need(w.load(scene,l,e),e);w.tick(.017f,{});need(w.actors()[1].ordinaryAI->targeting.target==(invisible?0u:2u),"native target selection follows linked first-match order and invisibility filter");}
 {auto scene=familyFixture("Cop");auto target=scene.objects[0];target.id={2};target.position={1480,1008};scene.objects.push_back(target);GameWorld w;need(w.load(scene,l,e),e);for(int tick=0;tick<3;++tick)w.tick(.017f,{});auto off=w.snapshot();need(off.actors[1].ordinaryAI->targeting.clock.active&&off.actors[1].ordinaryAI->targeting.target==2,"off-target control owns active terminal route");off.actors[2].alive=false;off.actors[2].reactivateMs=5000;off.actors[1].ordinaryAI->targeting.clock.scanRemainingMs=0;need(w.restoreSnapshot(off,e),e);w.tick(.017f,{});need(w.actors()[1].ordinaryAI->targeting.clock.active&&w.actors()[1].ordinaryAI->targeting.target==2,"active rescan validates native object membership, not off-state alive flag");}
 for(const std::string family:{"Cop","Burner"})for(bool negativeBirth:{false,true}){
  auto scene=familyFixture(family,"Fixed","Seeker",true);scene.objects[1].position=negativeBirth?ContentPosition{2000,500}:ContentPosition{1000,1000};scene.objects[0].position=negativeBirth?ContentPosition{2100,500}:ContentPosition{1500,1000};const auto authored=scene;GameWorld w;need(w.load(scene,l,e),e);const auto config=w.actors()[1].ordinaryAIConfig;need(config.guardian&&config.guardianRange==300,"reachable Guardian mode enabled with separate unscaled range");
  auto moved=w.snapshot();moveObject(moved,1,negativeBirth?ContentPosition{2400,500}:ContentPosition{1400,1000});moveObject(moved,0,negativeBirth?ContentPosition{2500,500}:ContentPosition{1500,1000});need(w.restoreSnapshot(moved,e),e);w.tick(.017f,{});const auto& route=w.actors()[1].ordinaryAI->targeting;need(route.clock.searching&&route.search,"Guardian initiates a coherent native route");
  if(negativeBirth)need(route.target==0,"negative resolved native Center follows current actor in42da80");
  else {need(!route.target,"positive native Guardian center stays at birth despite actor motion");const auto goal=nativeEnemyGuardianGoal(config.guardianHome);need(route.sampledTarget.x==goal.sample.x&&route.sampledTarget.y==goal.sample.y&&route.sampledCellX==goal.cellX&&route.sampledCellY==goal.cellY,"Guardian fallback samples retained native Location as shipped");need(route.search->seen.front()==goal.destination,"Guardian fallback preserves original double transform");}
  continuation(w,authored,l,30);
 }
 // Accepted preview behavior: retain Cop action markers across corridor exits.
 // This intentionally differs from original441076, which clears each visit.
 unsigned markerCases=0;
 for(const std::string mode:{"Seeker","Tracker","Random","Patrol","Burner"}){
  const bool base=mode=="Random"||mode=="Patrol",control=mode=="Burner";
  auto scene=familyFixture(control?"Burner":"Cop",base?mode:"Fixed",mode=="Tracker"?"Tracker":"Seeker");
  if(mode=="Patrol"){scene.classes[1].behaviours={mode};scene.objects[1].behaviours={mode};}
  if(mode=="Random")scene.objects[0].position={5000,2500};
  if(mode=="Patrol")scene.paths[0].points={{1200,1000},{1500,1100},{1300,1200}};
  const auto authored=scene;GameWorld world;need(world.load(scene,l,e),e);
  for(int tick=0;tick<3;++tick)world.tick(.017f,{});
  auto saved=world.snapshot();need(saved.actors[1].ordinaryAI.has_value(),mode+" has native action state");auto& ai=*saved.actors[1].ordinaryAI;
  auto& route=base?ai.base:ai.targeting;
  const int marker=mode=="Random"?4:mode=="Patrol"?12:mode=="Tracker"?2:1;
  need(route.clock.active&&ai.lastAction==marker,mode+" owns action before escape");
  moveObject(saved,1,{3500,2200});
  const auto position=saved.actors[1].nativeMotionHistory()->center;
  need(!spiderInsideRouteCorridor(position.x,position.y,route.segmentStart.x,route.segmentStart.y,
      route.segmentEnd.x,route.segmentEnd.y,saved.actors[1].radius*2,saved.actors[1].enemyMagnitude),mode+" is outside corridor");
  need(world.restoreSnapshot(saved,e),e);world.tick(.017f,{});
  const auto& actual=*world.actors()[1].ordinaryAI;
  const auto& after=base?actual.base:actual.targeting;
  need(!after.clock.active&&!after.clock.searching,mode+" performs real corridor escape");
  const int expectedMarker=control?0:mode=="Random"?1:marker;
  std::cout<<mode<<" expected="<<expectedMarker<<" actual="<<actual.lastAction<<'\n';
  need(actual.lastAction==expectedMarker,mode+" accepted Cop marker retained (other-family control unchanged)");
  continuation(world,authored,l,60);++markerCases;
 }
 need(markerCases==5,"all retained-marker and shared-family controls ran");
 // v31 pre-migration Cop checkpoints have no ordinary state; initialize their
 // native actions while retaining authored Patrol progress and momentum.
 auto oldScene=familyFixture("Cop","Patrol");GameWorld old;need(old.load(oldScene,l,e),e);auto legacy=old.snapshot();legacy.actors[1].ordinaryAI.reset();legacy.actors[1].brainState.initialized=true;legacy.actors[1].brainState.point=2;legacy.actors[1].brainState.reverse=true;legacy.actors[1].contactVelocity={2,3};need(old.restoreSnapshot(legacy,e),e);need(old.actors()[1].ordinaryAI->patrol.point==2&&old.actors()[1].ordinaryAI->patrol.reverse,"legacy native migration retains Patrol progress");need(old.actors()[1].contactVelocity.x==2&&old.actors()[1].contactVelocity.z==3,"legacy migration preserves accepted momentum");
 old.damage(1,1e9f,0);need(!old.actors()[1].alive&&!old.actors()[1].ordinaryAI,"Cop destruction disposes native action ownership");auto corpse=old.snapshot();need(old.restoreSnapshot(corpse,e)&&!old.actors()[1].ordinaryAI,"dead Cop restore does not recreate a live action");
 std::cout<<"PASS "<<actions<<" Cop/Burner coupled-action modes, pending/following/failure/disk, first-match targets,4 Guardian modes and legacy cleanup\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
