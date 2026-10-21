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
