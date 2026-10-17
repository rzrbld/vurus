#include "game/world.h"
#include "game/ammo_collision_native.h"
#include "game/save_state.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace {
void need(bool v,const std::string& why){if(!v)throw std::runtime_error(why);}
TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=k;n.values={v};return n;}
TextNode block(std::string k,std::vector<TextNode> c){TextNode n;n.kind=TextKind::Block;n.key=k;n.children=c;return n;}
// These actively simulated classes mirror authored Managed=1; omission means
// native unmanaged scenery and must not rely on the former all-actors loop.
SceneContent fixture(ContentPosition position){
 SceneContent s;s.mapPath="TEST/COP-CORNER.3DG";ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};p.properties={value("Managed","1"),block("Energy",{value("Initial","1000"),value("Max","1000")})};
 ObjectClass c;c.id={1};c.name="Cop";c.behaviours={"Fixed"};c.properties={value("Managed","1"),value("Sub-type","Cop"),value("Bounding-box-size","25"),value("Friction-factor","1"),value("Friction-threshold","0"),value("Grip","1"),value("Elasticity","1"),value("Speed-max","0"),value("Can-turn-on-place","1"),block("Energy",{value("Initial","1000"),value("Max","1000")})};
 s.classes={p,c};for(unsigned i=0;i<2;++i){ObjectPlacement o;o.id={i};o.classId={i};o.position=position;if(!i)o.position.x-=100;o.properties=s.classes[i].properties;o.behaviours=s.classes[i].behaviours;s.objects.push_back(o);}s.player=ObjectId{0};return s;
}
}
int main(){try{
 // Captured native Cop561 first entry; compare original4069d0 zero-scratch
 // outputs in absolute and translated frames. The original scratch is not
 // initialized: other seed controls remain explicit in the retained PE probe.
 constexpr ContactVelocity old{2765.69067f,279.934448f},p{2768.56592f,278.978424f};
 for(bool relative:{false,true})for(unsigned shape=0;shape<3;++shape){
  const ContactVelocity o=relative?old:ContactVelocity{};
  ContactVelocity lo{2790.61279f-o.x,(shape?286.216705f:214.662537f)-o.z},hi{2862.16699f-o.x,(shape?357.770905f:286.216705f)-o.z};if(shape==2)hi.z=lo.z;
  std::optional<ContactVelocity> hit;ContactVelocity out{};
  if(shape<2)hit=nativeAmmoRectanglePoint({old.x-o.x,old.z-o.z},{p.x-o.x,p.z-o.z},25,lo,hi);
  else if(spiderCircleEdgeEndpoint({p.x-o.x,p.z-o.z},25,lo,hi,out)||nativeAmmoCircleEdgeCrossing({old.x-o.x,old.z-o.z},{p.x-o.x,p.z-o.z},25,lo,hi,out))hit=out;
  need(hit.has_value(),"original corner contact exists");const float expectedX=relative&&shape==0?0:2790.61279f-o.x;
  need(hit->x==expectedX&&hit->z==286.216705f-o.z,"zero-seed original4069d0 contact coordinates");
 }
 const ContactVelocity velocity{2.41529536f,-.803098321f};
 need(!spiderContactAccepted(velocity,{0,6.28225708f}),"old translated-zero point wrongly rejects incoming Cop");
 need(spiderContactAccepted(velocity,{24.9221191f,6.28225708f}),"absolute original point admits incoming Cop");
 unsigned cases=0;for(const ContactVelocity shift:{ContactVelocity{},ContactVelocity{500,-100},ContactVelocity{3000,1500}}){
  const ContactVelocity center{old.x+shift.x,old.z+shift.z};const auto surface=spiderNativeToSurface(center.x,center.z);auto scene=fixture({surface.x,surface.y});Level l;l.mapW=l.mapH=200;l.hasTerrainCollision=true;
  NativeNavigationDecor a;a.minX=2790.61279f+shift.x;a.maxX=2862.16699f+shift.x;a.minY=214.662537f+shift.z;a.maxY=286.216705f+shift.z;a.rectangle=true;
  auto b=a;b.minY=a.maxY;b.maxY=357.770905f+shift.z;l.nativeNavigationDecor={a,b};GameWorld w;std::string e;need(w.load(scene,l,e),e);auto start=w.snapshot();auto& cop=start.actors[1];cop.nativeMotion=SpiderMotionHistory{{center.x,center.z},{surface.x-1,surface.y}};cop.contactMotion=true;cop.contactVelocity={p.x-old.x,p.z-old.z};cop.enemyMagnitude=std::hypot(cop.contactVelocity.x,cop.contactVelocity.z);start.actors[0].solid=false;
  need(w.restoreSnapshot(start,e),e);w.setDebugEnemyAI(false);w.tick(.014f,{});const auto& after=w.actors()[1];need(after.nativeMotionHistory()->center.x==center.x&&after.nativeMotionHistory()->center.y==center.z,"incoming Cop cannot commit its native center inside corner");need(after.contactVelocity.x<0,"corner contact reflects Cop away from wall");
  for(int tick=0;tick<60;++tick)w.tick(.014f,{});need(w.actors()[1].nativeMotionHistory()->center.x<a.minX-25,"Cop moves clear rather than becoming buried in wall");++cases;
 }
 std::cout<<"PASS 6 original zero-seed geometry controls and "<<cases<<" World corner/translation regressions\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
