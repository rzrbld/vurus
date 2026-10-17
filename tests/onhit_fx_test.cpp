#include "game/world.h"
#include "snapshot_fixture.h"
#include "game/properties.h"
#include "game/save_file.h"
#include "game/menu.h"
#include "io/vfs.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const std::string& s){if(!b)throw std::runtime_error(s);}
static TextNode value(std::string k,std::string v){TextNode n;n.key=k;n.kind=TextKind::Assignment;n.values={v};return n;}
static TextNode block(std::string k,std::vector<TextNode> c,std::string t={}){TextNode n;n.key=k;n.kind=TextKind::Block;n.children=c;if(!t.empty())n.arguments={t};return n;}
static void place(SceneContent& s,ClassId c,ContentPosition p){const auto& cls=s.classes[c.value];ObjectPlacement o;o.id={unsigned(s.objects.size())};o.classId=c;o.position=p;o.properties=cls.properties;o.behaviours=cls.behaviours;o.sprite=cls.sprite;o.flags=cls.flags;s.objects.push_back(o);}
static SceneContent fixture(){
 SceneContent s;s.mapPath="TEST.3DG";
 const auto add=[&](std::string name,std::vector<TextNode> p){ObjectClass c;c.id={unsigned(s.classes.size())};c.name=name;c.properties=p;c.properties.push_back(value("Managed","1"));c.behaviours={"Fixed"};s.classes.push_back(c);};
 add("Player",{block("Energy",{value("Initial","1000")})});s.classes[0].behaviours={"Player"};
 add("Target",{value("Bounding-box-size","10"),block("Energy",{value("Initial","100000")})});
 add("Ammo",{block("TYPE",{value("Hit-fx","Impact")},"Ammo")});
 add("Impact",{block("TYPE",{},"FX")});add("Bullet-Ground-Impact",{block("TYPE",{},"FX")});
 add("Water-Splash",{block("TYPE",{},"FX")});
 place(s,{0},{100,100});s.player=ObjectId{0};return s;
}
struct Result {SceneContent scene;WorldSnapshot snapshot;};
// mode0 actor,1 wall,2 airborne fuse,3 ground fuse,4 water fuse,
// 5 mixed-mask fuse,6 sloped ground termination,7 bounced-water disposal,
// 8..11 out-of-extent fixed-point clamp controls.
static Result run(SceneContent s,ClassId ammo,int mode,float altitude=24,bool visual=true,
                  int hz=60,bool nonplayer=false,bool roundtrip=false){
 Level level;level.mapW=level.mapH=100;level.hasTerrainCollision=true;
 if(mode==1)level.collisionEdges.push_back({540,0,540,1000});
 if(mode==4||mode==5||mode==7){level.collisionMask.resize(10000);level.collisionMask[15*100+7]=mode==5?0x101:0x100;}
 const auto* type=attribute(s.classes[ammo.value].properties,"ammo");need(type,"test source is Ammo");
 const auto hit=propertyText(type->children,"Hit-fx");
 if(mode==0)place(s,{1},{500,500});
 GameWorld w;std::string e;need(w.load(s,level,e),e);w.setDebugEnemyAI(false);
 const size_t owner=nonplayer?w.spawn({1},{1500,1500}):0;
 Projectile p;p.classId=ammo;p.owner=owner;p.position={500,500};p.hitEffect=hit;p.radius=1;
 p.ignoreOwner=true;p.phaseInitialized=true;p.motionConfig.inertia=true;
 p.slopeAltitude=altitude;p.slopeHeight=altitude;
 if(mode==0)p.position.x=450;
 if(mode==0||mode==1){p.motion.velocityX=100;p.motion.velocityZ=-100;}
 if(mode>=2){p.fuseMs=50;p.damageRemainingMs=0;}
 if(mode==6){p.slopeEnabled=true;p.slopeAltitude=1;p.slopeHeight=0;p.slopeRadians=-1;}
 if(mode==7){p.fuseMs=0;p.verticalBounce=true;p.ageMs=320;p.motion.velocityX=10;p.motion.magnitude=10;}
 if(mode==8)p.position.x=-3;
 if(mode==9)p.position.x=6403;
 if(mode==10)p.position.y=-3;
 if(mode==11)p.position.y=3203;
 if(visual)p.visual=w.spawn(ammo,p.position);
 auto saved=w.snapshot();vyruz_test::appendProjectile(saved,s,p);saved.spiderNavigation.reset();
 if(p.visual){saved.actors[*p.visual].solid=false;saved.actors[*p.visual].projectileAltitude=altitude;}
 need(w.restoreSnapshot(saved,e),e);w.setDebugEnemyAI(false);
 // Restore rebuilds authored bounce parameters; this control specifically
 // exercises the shipped Grenade's separate negative-water-halfwave path.
 w.tick(1.f/hz,{});
 const bool rebound=mode==1&&!w.projectiles().empty()&&w.projectiles()[0].terrainBounce;
 need(w.projectiles().empty()||rebound,"test projectile terminates: "+s.classes[ammo.value].name+" mode="+std::to_string(mode)+" hz="+std::to_string(hz));
 if(roundtrip){SaveGame game;game.currentLevel=s.mapPath;game.levels[s.mapPath].world=w.snapshot();std::vector<uint8_t>b;SaveGame decoded;
  need(encodeSave(game,b,e)&&decodeSave(b,decoded,e),e);need(w.restoreSnapshot(decoded.levels.at(s.mapPath).world,e),e);}
 auto result=w.snapshot();return {std::move(s),std::move(result)};
}
static std::vector<size_t> effects(const Result& r){std::vector<size_t> ids;for(size_t i=0;i<r.snapshot.actors.size();++i){
 const auto& a=r.snapshot.actors[i];const auto& name=r.scene.classes[r.scene.objects[i].classId.value].name;
 const bool direct=name=="Impact"||name=="Bullet-Spark"||name=="Bullet-Ground-Impact"||name=="Water-Splash"||name=="Flame-Smoke"||name=="Laser-Impact"||name=="Small-Explosion";
 if(a.alive&&(a.effect||attribute(r.scene.objects[i].properties,"fx"))&&!a.explosion&&direct)ids.push_back(i);}return ids;}
static void expect(const Result& r,const std::string& name,float height,size_t owner){
 const auto ids=effects(r);need(ids.size()==1,"exactly one direct effect expected for "+name+", got "+std::to_string(ids.size()));
 const auto id=ids[0];const auto& o=r.scene.objects[id];const auto& a=r.snapshot.actors[id];
 need(contentNameEqual(r.scene.classes[o.classId.value].name,name),"correct effect class: "+name);
 need(o.owner==ObjectId{unsigned(owner)},"direct effect copies creator ancestry");
 need(a.projectileAltitude.value_or(propertyNumber(o.properties,"Altitude"))==height,"direct effect height for "+name+": expected "+std::to_string(height)+", got "+std::to_string(a.projectileAltitude.value_or(propertyNumber(o.properties,"Altitude"))));
}
static void unit(){
 for(bool visual:{false,true})for(bool enemy:{false,true})for(int hz:{60,90}){
  const auto owner=enemy?2u:0u;expect(run(fixture(),{2},0,24,visual,hz,enemy,true),"Impact",24,owner);
  expect(run(fixture(),{2},1,24,visual,hz),"Impact",24,0);
  need(effects(run(fixture(),{2},2,24,visual,hz)).empty(),"airborne fuse has no direct FX");
  expect(run(fixture(),{2},3,0,visual,hz),"Bullet-Ground-Impact",0,0);
  expect(run(fixture(),{2},4,0,visual,hz),"Water-Splash",0,0);
  expect(run(fixture(),{2},5,0,visual,hz),"Bullet-Ground-Impact",0,0);
  expect(run(fixture(),{2},6,1,visual,hz),"Bullet-Ground-Impact",0,0);
 }
 auto s=fixture();s.classes[3].flags.inGround=true;s.classes[3].properties.push_back(value("Altitude","7"));
 expect(run(s,{2},0),"Impact",7,0);
 expect(run(fixture(),{2},3,-3,false),"Bullet-Ground-Impact",-3,0);
 s=fixture();s.classes[3].properties.clear();need(effects(run(s,{2},0)).empty(),"Hit-fx requires TYPE FX");
 s=fixture();s.classes[3].sprite.path="COMMON/THING/IMPACT.GAF";expect(run(s,{2},0),"Impact",24,0);
 for(const auto& kind:{"FX","Item","Lift","Gate"}){s=fixture();s.classes[2].properties.push_back(value("Sub-type",kind));need(effects(run(s,{2},0)).empty(),"excluded source subtype");}
 s=fixture();s.classes[2].properties.push_back(value("Vertical-Bounce","1"));
 const auto splash=run(s,{2},7);const auto splashIds=effects(splash);need(splashIds.size()==1,"bounced grenade emits hyphenated Water-Splash");
 need(splash.scene.classes[splash.scene.objects[splashIds[0]].classId.value].name=="Water-Splash"&&!splash.scene.objects[splashIds[0]].owner,"separate grenade water disposal creates ownerless splash");
 for(int mode:{8,9,10,11}){const auto r=run(fixture(),{2},mode,0,false);expect(r,"Bullet-Ground-Impact",0,0);const auto p=r.scene.objects[effects(r)[0]].position;
  need(p.x==(mode==8?0:mode==9?6399:500)&&p.y==(mode==10?0:mode==11?3199:500),"independent surface extent clamps");}
 // Duplicate filtering applies before clamping and permits exact-position
 // reuse once the prior actor has been removed. Test fuse at a fixed point.
 for(int mode=0;mode<4;++mode){s=fixture();s.classes[4].properties.push_back(value("Uniq-at-xy","1"));
  place(s,{4},{mode==1?500.001f:500,500});
  if(mode==2)s.classes[4].properties.pop_back();
  if(mode==3){s.objects.back().classId={3};s.objects.back().properties=s.classes[3].properties;}
  const auto r=run(s,{2},3,0);need(effects(r).size()==(mode==0?1:2),"exact class/position duplicate admission");
 }
 std::cout<<"on-hit FX selection, height, ancestry, TYPE, uniqueness and save controls passed\n";
}
static void resources(const char* data){
 Vfs v;need(v.init(data),v.error());ContentReader read=[&](const auto&p,auto&b){return v.read(p,b);};unsigned maps=0,cases=0;
 for(const auto& path:debugLevels()){
  SceneContent original;std::string e;need(loadSceneContent(read,path,original,e),e);
  auto player=original.objects[original.player->value];player.id={0};player.position={100,100};original.objects={player};original.player=ObjectId{0};
  // Fixed generic target class, no authored death/CREATE products. Sources and
  // effect definitions remain the original resolved resource data.
  ObjectClass target;target.id={unsigned(original.classes.size())};target.name="ImpactTarget";target.behaviours={"Fixed"};target.properties={value("Bounding-box-size","10"),block("Energy",{value("Initial","100000")})};original.classes.push_back(target);
  for(const auto& ammo:original.classes){const auto* a=attribute(ammo.properties,"ammo");if(!a)continue;
   for(int mode:{0,1,2,3,4,5})for(int hz:{60,90}){
    // run() uses class1 for its synthetic target; replace its template only.
    auto s=original;s.classes[1]=target;s.classes[1].id={1};
    const float height=mode>=3?0:24;
    const auto r=run(s,ammo.id,mode,height,true,hz,false,mode==0);
    const auto chosen=mode==1&&!r.snapshot.projectiles.empty()?"":mode==0||mode==1?propertyText(a->children,"Hit-fx"):mode==2?"":mode==4?"Water-Splash":"Bullet-Ground-Impact";
    if(chosen.empty())need(effects(r).empty(),"no named/airborne direct effect: "+ammo.name);
    else {
     const size_t body=mode==0?2:1;
     const float currentHeight=std::trunc(r.snapshot.actors[body].projectileAltitude.value_or(height));
     expect(r,chosen,currentHeight,0);
    }
    ++cases;
   }
  }
  ++maps;
 }
 need(maps==25&&cases==6600,"all25 maps and22 Ammo classes covered");
 std::cout<<"original resource audit maps="<<maps<<" cases="<<cases<<" passed\n";
}
int main(int argc,char**argv){try{unit();if(argc==2)resources(argv[1]);return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
