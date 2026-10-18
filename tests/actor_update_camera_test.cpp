#include "game/world.h"
#include "game/save_file.h"
#include "game/properties.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace {
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
TextNode value(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={text};return n;}
TextNode block(std::string key,std::vector<TextNode> children,std::string type={}){TextNode n;n.kind=TextKind::Block;n.key=key;n.children=std::move(children);if(!type.empty())n.arguments={type};return n;}
void place(SceneContent&s,unsigned cls,ContentPosition pos){ObjectPlacement o;o.id={unsigned(s.objects.size())};o.classId={cls};o.position=pos;o.properties=s.classes[cls].properties;o.behaviours=s.classes[cls].behaviours;s.objects.push_back(o);}
SceneContent fixture(std::string family="Cop") {
 SceneContent s;s.mapPath="TEST/ACTOR-UPDATE-CAMERA.3DG";
 ObjectClass p;p.id={0};p.name="Player";p.behaviours={"Player"};p.properties={value("Sub-type","Player"),block("Energy",{value("Initial","10000"),value("Max","10000")})};
 ObjectClass enemy;enemy.id={1};enemy.name="Enemy";enemy.behaviours={"Fixed","Seeker"};enemy.properties={value("Sub-type",family),value("Graphic-update-needed","1"),value("Can-turn-on-place","1"),value("Turn-timeout","42"),value("Turret-turn-timeout","75"),value("Bounding-box-size","0"),value("Speed-max","0"),value("Shield-up-step","3"),block("Shield",{value("Initial","0"),value("Max","100")}),block("Energy",{value("Initial","1000"),value("Max","1000")}),block("TYPE",{value("Target","Player"),value("Sensor-Range","1"),value("Scan-Delay","1000"),value("Vision-Angle","360")},"Seeker"),block("CREATE",{value("Name","Child"),value("Delay","100"),value("Repeat-timeout","20")}),block("ADD",{value("Name","Protection")},"FX")};
 ObjectClass child;child.id={2};child.name="Child";child.behaviours={"Fixed"};child.properties={value("Sub-type","FX"),value("Speed-max","0"),block("Energy",{value("Initial","1000")}),block("TYPE",{},"FX")};
 ObjectClass protection;protection.id={3};protection.name="Protection";protection.properties={value("Sub-type","FX"),block("TYPE",{value("Check-event","bonus-invulnerability"),value("On-timeout","10"),value("Off-timeout","20")},"FX")};
 p.properties.push_back(value("Managed","1"));enemy.properties.push_back(value("Managed","1"));child.properties.push_back(value("Managed","1"));
 s.classes={p,enemy,child,protection};place(s,0,{100,100});place(s,1,{500,500});s.player=ObjectId{0};return s;
}
Level level(){Level l;l.mapW=l.mapH=100;l.surfaceWidth=6336;l.surfaceHeight=3168;return l;}
void load(GameWorld&w,SceneContent&s){std::string e;need(w.load(s,level(),e),e.c_str());}
NativeEffectCamera outside(){NativeEffectCamera c;c.offsetX=-2000;return c;}
size_t children(const GameWorld&w,const SceneContent&s){size_t n=0;for(size_t i=0;i<s.objects.size();++i)if(s.objects[i].classId.value==2 && w.actors()[i].alive)++n;return n;}
}
int main(){try {
 const auto terrain=level();std::string error;
 for(const std::string family:{"Thing","Cop","Player","Walker","Burner","Jeep","Roller","Heli","Turret","Spider","Tank"}) {
  auto s=fixture(family);GameWorld w;need(w.load(s,terrain,error),error.c_str());w.setEffectCamera(outside());
  auto initial=w.snapshot();initial.nativeWorldRandom={1,2118850534u};initial.actors[1].contactVelocity={5,-5};initial.actors[1].contactMotion=true;
  need(w.restoreSnapshot(initial,error),error.c_str());const auto rng=initial.nativeWorldRandom;
  w.tick(.1f,{});auto after=w.snapshot();
  need(after.actors[1].nativeCameraGated,"ordinary source families are camera gated, including Graphic-update-needed sources");
  need(after.actors[1].ageMs==100,"offscreen ordinary age advances before camera admission");
  need(after.objects[1].position.x==500 && after.objects[1].position.y==500,"camera rejection freezes ordinary propulsion/motion");
  need(after.actors[1].vitals.shield==0 && after.actors[1].brainState.scanRemainingMs==initial.actors[1].brainState.scanRemainingMs,"camera rejection freezes regeneration and seeker clocks");
  need(after.nativeWorldRandom.seed==rng.seed && after.nativeWorldRandom.counter==rng.counter,"camera rejection consumes no ordinary idle RNG");
  need(children(w,s)==0,"camera rejection suppresses ordinary CREATE despite due lifetime age");
  need(after.actors[1].timedProtection.onRemainingMs!=initial.actors[1].timedProtection.onRemainingMs,"independent nativeFX protection timer continues through owner camera rejection");
  w.setEffectCamera({});w.tick(.016f,{});
  need(children(w,s)==1,"re-entering camera admits one overdue ordinary CREATE without catch-up burst");
 }
 // Subtype exclusions are exact: no Graphic-update-needed bypass is invented.
 for(const std::string family:{"Ammo","StaticGen","FX","Item","Lift","Gate"}) {
  auto s=fixture(family);GameWorld w;need(w.load(s,terrain,error),error.c_str());w.setEffectCamera(outside());w.tick(.1f,{});
  need(!w.actors()[1].nativeCameraGated,"Ammo/StaticGen and FX/Item/Lift/Gate bypass actor camera gate");
  need(children(w,s)==1,"bypass subtype continues ordinary CREATE outside camera");
 }
 for(const std::string logicalType:{"Ammo","FX","Item","Lift","Gate"}) {
  auto s=fixture();auto& properties=s.classes[1].properties;
  std::erase_if(properties.write(),[](const auto& p){return contentNameIs(p.key,"sub-type");});
  properties.push_back(block("TYPE",{},logicalType));s.objects[1].properties=properties;
  GameWorld w;need(w.load(s,terrain,error),error.c_str());w.setEffectCamera(outside());w.tick(.1f,{});
  if(w.actors()[1].nativeCameraGated)std::cerr<<"failed logical type "<<logicalType<<" subtype "<<objectSubtype(s.objects[1])<<"\n";
  need(!w.actors()[1].nativeCameraGated,"sprite-less logical Ammo/FX/Item/Lift/Gate retains native bypass when subtype is unspecified");
 }
 // Explicit subtype wins over an auxiliary attribute: a Thing with FX data
 // must not silently acquire the independent FX actor's camera bypass.
 {auto s=fixture("Thing");s.classes[1].properties.push_back(block("TYPE",{},"FX"));s.objects[1].properties=s.classes[1].properties;
  GameWorld w;need(w.load(s,terrain,error),error.c_str());w.setEffectCamera(outside());w.tick(.1f,{});
  need(w.actors()[1].nativeCameraGated && children(w,s)==0,"explicit native subtype takes precedence over supplemental FX attributes");
 }
 {auto s=fixture();s.classes[1].properties.push_back(value("Life-time","50"));s.objects[1].properties=s.classes[1].properties;
  GameWorld w;need(w.load(s,terrain,error),error.c_str());w.setEffectCamera(outside());w.tick(.1f,{});
  need(!w.actors()[1].alive && children(w,s)==0,"lifetime death precedes camera gate and blocks due ordinary CREATE");
 }
 {auto s=fixture();GameWorld w;need(w.load(s,terrain,error),error.c_str());w.setEffectCamera(outside());auto snap=w.snapshot();
  snap.actors[1].alive=false;snap.actors[1].reactivateMs=10;need(w.restoreSnapshot(snap,error),error.c_str());w.tick(.016f,{});
  need(w.actors()[1].alive && w.actors()[1].reactivateMs<0 && w.actors()[1].ageMs==16,"off/reactivation timer completes before camera gate");
 }
 {auto s=fixture();GameWorld w;need(w.load(s,terrain,error),error.c_str());w.setEffectCamera(outside());auto snapshot=w.snapshot();snapshot.actors[1].nativeCameraGated=false;
  SaveGame save;save.currentLevel=s.mapPath;save.levels[s.mapPath].world=snapshot;std::vector<uint8_t> bytes;SaveGame decoded;
  need(encodeSave(save,bytes,error)&&decodeSave(bytes,decoded,error)&&w.restoreSnapshot(decoded.levels.at(s.mapPath).world,error),error.c_str());
  need(w.actors()[1].nativeCameraGated,"camera-gate cache rebuilds from subtype on restore, independent of serialized snapshot cache");
  w.tick(.1f,{});need(children(w,s)==0,"save restore retains external camera and recomputed actor admission");
 }
 {auto s=fixture();GameWorld w;need(w.load(s,terrain,error),error.c_str());w.tick(.1f,{});need(children(w,s)==1,"unset view preserves intentional camera-independent headless mechanics");}
 std::cout<<"Native actor camera subtype bypass, age/lifetime/reactivation ordering, idle/regeneration/motion/CREATE suppression and save cache reconstruction passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
