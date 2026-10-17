// Selected-impact handoff tests. Collision selection and projectile removal
// belong to separate probes; these exercise production radial -> HitFX -> direct.
#include "game/world.h"
#include "game/save_state.h"
#include "game/properties.h"
#include "game/spider_contact_native.h"
#include "game/menu.h"
#include "io/vfs.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace vyruz {struct AmmoImpactNativeTestAccess {
 static void impact(GameWorld& w,Projectile p){p.nativeOwnerGeneration=w.actors()[p.owner].nativeGeneration;w.applyAmmoImpact(p,1,true,{1500,500},16);}
};}
static void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(k);n.values={std::move(v)};return n;}
static TextNode block(std::string k,std::vector<TextNode> c,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=std::move(k);n.children=std::move(c);if(!type.empty())n.arguments={std::move(type)};return n;}
static ContentPosition point(){auto p=spiderNativeToSurface(1500,500);return {p.x,p.y};}
static ObjectClass targetClass(unsigned id){ObjectClass c;c.id={id};c.name="Order-Audit-Target";c.behaviours={"Fixed"};c.properties={value("Sub-type","Staticgen"),value("Managed","1"),value("Bounding-box-size","1"),block("Energy",{value("Initial","10000"),value("Max","10000")})};return c;}
static void place(SceneContent& s,ClassId cls,ContentPosition p){ObjectPlacement o;o.id={uint32_t(s.objects.size())};o.classId=cls;o.position=p;o.properties=s.classes[cls.value].properties;o.behaviours=s.classes[cls.value].behaviours;o.flags=s.classes[cls.value].flags;o.sprite=s.classes[cls.value].sprite;s.objects.push_back(std::move(o));}
static void load(GameWorld& w,SceneContent& s,const Level& l){std::string e;need(w.load(s,l,e),e);auto saved=w.snapshot();saved.actors[1].nativeMotion=SpiderMotionHistory{{1500,500},saved.objects[1].position};need(w.restoreSnapshot(saved,e),e);w.setDebugEnemyAI(false);}
static Projectile shot(ClassId cls,std::string hit){Projectile p;p.classId=cls;p.owner=0;p.position=point();p.damage=100;p.damageRadius=64;p.ageMs=1000;p.directionX=1;p.hitEffect=std::move(hit);return p;}
static void units(){
 for(unsigned mode=0;mode<7;++mode){
  SceneContent s;s.mapPath="TEST/HIT-ORDER.3DG";
  ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};player.properties={value("Sub-type","Vyruz"),block("Energy",{value("Initial","10000"),value("Max","10000")})};
  ObjectClass ammo;ammo.id={2};ammo.name="Order-Ammo";ammo.properties={value("Sub-type","Ammo"),block("TYPE",{value("Damage","100"),value("Damage-zone","64")},"Ammo")};
  ObjectClass fx;fx.id={3};fx.name="Order-HitFX";fx.behaviours={"Fixed"};fx.properties={value("Sub-type","FX"),value("Bounding-box-size","0"),value("Uniq-at-xy","1")};if(mode!=3)fx.properties.push_back(block("TYPE",{},"FX"));
  s.classes={player,targetClass(1),ammo,fx};place(s,{0},{100,100});place(s,{1},point());
  if(mode==2||mode==5||mode==6){auto pos=point();if(mode==5)pos.x+=.25f;place(s,{3},pos);}s.player=ObjectId{0};
  Level l;l.mapW=l.mapH=32;l.hasTerrainCollision=true;GameWorld w;load(w,s,l);
  if(mode==4){NativeEffectCamera camera;camera.offsetX=-10000;w.setEffectCamera(camera);}
  if(mode==6){auto saved=w.snapshot();saved.actors[2].alive=false;saved.actors[2].reactivateMs=1000;std::string error;need(w.restoreSnapshot(saved,error),error);}
  const auto before=s.objects.size();const auto p=shot({2},mode==0?"":"Order-HitFX");AmmoImpactNativeTestAccess::impact(w,p);
  const bool direct=mode!=2&&mode!=3&&mode!=6;
  need(w.actors()[1].vitals.energy==(direct?9800:9900),"radial precedes HitFX/direct admission mode="+std::to_string(mode));
  const bool created=mode==1||mode==5;
  need(s.objects.size()==before+unsigned(created),"HitFX construction admission mode="+std::to_string(mode));
  need(w.snapshot().ammoHits==0&&w.snapshot().ammoMisses==1,"contact statistics precede HitFX rejection");
  if(created){need(s.objects.back().owner==ObjectId{0},"HitFX inherits immediate Ammo owner");need(w.actors().back().effect,"HitFX participates in native animation expiry");}
 }
 std::cout<<"selected impact radial/HitFX/direct phase, exact and inactive duplicate, camera and owner controls passed\n";
}
static void resources(const char* data){
 Vfs v;need(v.init(data),v.error());ContentReader read=[&](const auto& p,auto& bytes){return v.read(p,bytes);};unsigned maps=0,cases=0,authoredFX=0,unique=0;
 for(const auto& path:debugLevels()){
  SceneContent original;std::string error;need(loadSceneContent(read,path,original,error),error);
  for(const auto& ammo:original.classes){const auto* metadata=attribute(ammo.properties,"ammo");if(!metadata)continue;
   auto s=original;s.objects.clear();auto player=original.objects[original.player->value];player.id={0};player.owner.reset();player.position={100,100};s.objects.push_back(player);s.player=ObjectId{0};
   const auto id=uint32_t(s.classes.size());s.classes.push_back(targetClass(id));place(s,{id},point());
   Level l;l.mapW=l.mapH=32;l.hasTerrainCollision=true;GameWorld w;load(w,s,l);
   const auto text=propertyText(metadata->children,"Hit-fx");const auto* fx=findClass(original,text);
   const bool fxValid=fx&&attribute(fx->properties,"fx"),uniq=fxValid&&propertyBool(fx->properties,"Uniq-at-xy");authoredFX+=fxValid;unique+=uniq;
   auto p=shot(ammo.id,text);AmmoImpactNativeTestAccess::impact(w,p);
   need(w.actors()[1].vitals.energy==(fx&&!fxValid?9900:9800),path+" "+ammo.name+" authored HitFX first admission");
   need(s.objects.size()==2+unsigned(fxValid),path+" "+ammo.name+" authored HitFX product");
   if(fxValid){need(w.actors().back().effect&&s.objects.back().owner==ObjectId{0},"authored HitFX lifecycle/owner");}
   AmmoImpactNativeTestAccess::impact(w,p);
   const float expected=fx&&!fxValid?9800:uniq?9700:9600;
   need(w.actors()[1].vitals.energy==expected,path+" "+ammo.name+" authored duplicate suppression after radial");
   ++cases;
  }++maps;
 }
 need(maps==25&&cases==550,"all25 maps/550 original Ammo blocks");
 std::cout<<"selected impact resource maps="<<maps<<" Ammo blocks="<<cases<<" authored HitFX="<<authoredFX<<" unique HitFX="<<unique<<" passed\n";
}
int main(int argc,char**argv){try{units();if(argc==2)resources(argv[1]);return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
