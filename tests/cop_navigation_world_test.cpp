#include "cop_navigation_fixture.inc"
#include <bit>

static Level arena(){Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;l.collisionMask.assign(10000,0xff);return l;}
static void nativePosition(WorldSnapshot& s,size_t id,NativeNavPoint p){const auto surface=spiderNativeToSurface(p.x,p.y);moveObject(s,id,{surface.x,surface.y});s.actors[id].nativeMotion->center=p;}

static void queryCases(){
 NativeNavGraph g;for(auto b:{NativeNavObstacle{1260,236,1300,276},NativeNavObstacle{1334,221,1354,291},NativeNavObstacle{1196,268,1236,308},NativeNavObstacle{1245,182,1315,202}})insertNativeNavigationObject(g,3,b);
 const auto a=insertNativeNavigationPoint(g,{1152,128}),b=insertNativeNavigationPoint(g,{1196,288});
 need(!queryNativeNavigation(g,a,b,{25,25},false,{}),"accepted shared X arithmetic remains intact");
 need(!queryNativeNavigation(g,a,b,{25,25},false,{},true),"original x87 segment intersection rejects boundary blocker");
 const auto ya=insertNativeNavigationPoint(g,{1152,221}),yb=insertNativeNavigationPoint(g,{1280,236});
 need(!queryNativeNavigation(g,ya,yb,{25,25},false,{}),"default Y arithmetic remains unchanged");
 const auto hit=queryNativeNavigation(g,ya,yb,{25,25},false,{},true);
 need(hit&&hit->minX==1260&&hit->minY==236,"original Y stored-slope intercept admits boundary blocker");
 auto l=arena();for(auto bounds:{NativeNavObstacle{1260,236,1300,276},NativeNavObstacle{1334,221,1354,291},NativeNavObstacle{1196,268,1236,308},NativeNavObstacle{1245,182,1315,202}}){NativeNavigationDecor d;d.minX=bounds.minX;d.minY=bounds.minY;d.maxX=bounds.maxX;d.maxY=bounds.maxY;d.rectangle=true;l.nativeNavigationDecor.push_back(d);}
 for(const std::string family:{"Cop","Burner"}){
  auto scene=familyFixture(family);const auto from=spiderNativeToSurface(1152,221),to=spiderNativeToSurface(1280,236);scene.objects[1].position={from.x,from.y};scene.objects[0].position={to.x,to.y};const auto authored=scene;
  GameWorld w;std::string e;need(w.load(scene,l,e),e);auto state=w.snapshot();nativePosition(state,1,{1152,221});nativePosition(state,0,{1280,236});need(w.restoreSnapshot(state,e),e);
  w.tick(.017f,{});w.tick(.017f,{});const auto& route=w.actors()[1].ordinaryAI->targeting;
  need(route.clock.active==(family!="Cop"),family+" uses selected query precision in live search");
  if(family!="Cop")need(route.points.size()==2,"other-family direct route is preserved");
  continuation(w,authored,l,60);
 }
 std::cout<<"PASS native query + live Cop/default-family controls\n";
}

static void corridorCases(){
 const float x=std::bit_cast<float>(uint32_t(1148026879));
 need(spiderInsideRouteCorridor(x,100,1000,100,1200,100,50,.00004f),"default corridor remains unchanged");
 need(!spiderInsideRouteCorridor(x,100,1000,100,1200,100,50,.00004f,true),"original extended corridor rejects boundary");
 for(const std::string family:{"Cop","Burner","Walker"}){
  auto l=arena();auto scene=familyFixture(family);const auto authored=scene;GameWorld w;std::string e;need(w.load(scene,l,e),e);w.tick(.017f,{});w.tick(.017f,{});
  auto snapshot=w.snapshot();auto& actor=snapshot.actors[1];auto& route=actor.ordinaryAI->targeting;need(route.clock.active&&route.points.size()==2,"route acquired before corridor test");
  const NativeNavPoint start{1000,100,route.points[0].graphNode},goal{1200,100,route.points[1].graphNode};
  for(auto& p:route.ownedPoints){if(p.graphNode==start.graphNode)p=start;else if(p.graphNode==goal.graphNode)p=goal;}
  route.points={start,goal};route.cursor=1;route.segmentStart=start;route.segmentEnd=goal;route.desiredHeading=spiderContactHeading({200,0});route.clock.scanRemainingMs=1000;
  for(auto p:{start,goal})updateNativeNavigationObject(*snapshot.spiderNavigation,p.graphNode,{p.x,p.y,p.x,p.y});
  nativePosition(snapshot,1,{x,100});nativePosition(snapshot,0,{1200,100});actor.enemyMagnitude=.00004f;actor.contactVelocity={.00004f,0};
  route.sampledTarget=snapshot.objects[0].position;route.sampledCellX=int(route.sampledTarget.x)/64;route.sampledCellY=int(route.sampledTarget.y)/32;
  need(w.restoreSnapshot(snapshot,e),e);w.tick(.017f,{});const auto& after=w.actors()[1];
  need(after.ordinaryAI->targeting.clock.active==(family!="Cop"),family+" World corridor uses intended precision");
  if(family=="Cop")need(after.enemyMoveCommand==0,"original Cop corridor escape does not steer");
  continuation(w,authored,l,60);
 }
 std::cout<<"PASS native corridor + live Cop/default-family controls\n";
}

static void headingCases(){
 need(spiderContactHeading({0,0})==24,"contact sentinel remains unchanged");
 for(const std::string family:{"Cop","Burner","Walker","Jeep"}){
  auto l=arena();auto scene=familyFixture(family);scene.objects[0].position={1020,1000};const auto authored=scene;GameWorld w;std::string e;need(w.load(scene,l,e),e);w.tick(.017f,{});w.tick(.017f,{});
  auto state=w.snapshot();auto& route=state.actors[1].ordinaryAI->targeting;need(route.clock.active,"route acquired before heading test");const auto n=spiderSurfaceToNative(state.objects[0].position.x,state.objects[0].position.y);const NativeNavPoint target{n.x,n.y};
  route.segmentStart=target;route.segmentEnd={target.x+100,target.y};route.desiredHeading=18;route.clock.scanRemainingMs=1000;need(w.restoreSnapshot(state,e),e);w.tick(.017f,{});
  const auto& after=w.actors()[1].ordinaryAI->targeting;need((after.status&4)&&after.segmentStart==after.segmentEnd,"target reaches retained start");
  need(after.desiredHeading==0,family+" route-only zero heading matches intended scope");continuation(w,authored,l,60);
 }
 std::cout<<"PASS original Cop zero-route heading + retained other-family/contact controls\n";
}

int main(int argc,char** argv){try{const std::string mode=argc>1?argv[1]:"all";if(mode=="all"||mode=="query")queryCases();if(mode=="all"||mode=="corridor")corridorCases();if(mode=="all"||mode=="heading")headingCases();need(mode=="all"||mode=="query"||mode=="corridor"||mode=="heading","unknown case group");return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
