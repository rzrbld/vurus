#include "game/world.h"
#include "game/save_file.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static TextNode value(std::string k,std::string v){TextNode n;n.key=std::move(k);n.kind=TextKind::Assignment;n.values={std::move(v)};return n;}
static TextNode block(std::string k,std::vector<TextNode> c,std::string type={}){TextNode n;n.key=std::move(k);n.kind=TextKind::Block;n.children=std::move(c);if(!type.empty())n.arguments={std::move(type)};return n;}
static SceneContent fixture(){
 SceneContent s;s.mapPath="TEST/REUSE.3DG";s.player=ObjectId{0};
 for(const char* name:{"Player","Spark"}){ObjectClass c;c.id={uint32_t(s.classes.size())};c.name=name;c.behaviours={"Fixed"};c.properties={value("Managed","1"),value("Bounding-box-size","0")};s.classes.push_back(c);}
 s.classes[0].behaviours={"Player"};s.classes[0].properties.push_back(block("Energy",{value("Initial","1000"),value("Max","1000")}));
 s.classes[1].properties.push_back(block("TYPE",{},"FX"));
 ObjectPlacement p;p.id={0};p.classId={0};p.position={1000,1000};p.properties=s.classes[0].properties;p.behaviours=s.classes[0].behaviours;p.authored.key="Create-object";s.objects={p};return s;
}
static std::vector<uint8_t> disk(const GameWorld&w,const std::string&map){SaveGame s;s.currentLevel=map;s.levels[map].world=w.snapshot();std::string e;std::vector<uint8_t>b;need(encodeSave(s,b,e),e.c_str());return b;}
// The real FX producer marks the generic factory body as an effect.
static size_t emit(GameWorld& w,ContentPosition p){auto id=w.spawn({1},p);const_cast<ActorState&>(w.actors()[id]).effect=true;return id;}
int main(){try{
 auto s=fixture(),original=s;Level l;l.mapW=100;l.mapH=100;l.hasTerrainCollision=true;GameWorld w;std::string e;need(w.load(s,l,e),e.c_str());
 for(int i=0;i<1100;++i){auto id=emit(w,{1000,1000});w.expireVisual(id);}
 const auto authored=emit(w,{1000,1000});s.objects[authored].authored.key="Create-object";w.expireVisual(authored);
 const auto keeper=emit(w,{1000,1000});s.objects[keeper].owner=ObjectId{1};
 const auto size=w.actors().size();w.takeAudioEvents();w.tick(.014f,{});
 auto id=emit(w,{1100,1100});need(id==2,"lowest free same-class slot, live owner reference pins slot1");
 need(w.actors().size()==size&&w.actors()[id].nativeGeneration&&(w.actors()[id].visualRevision&0xffffffffu)==1,"reuse with fresh native identity and renderer revision");
 need(!w.actors()[authored].alive&&s.objects[authored].authored.key=="Create-object","authored dead placement retained");
 w.expireVisual(id);w.takeAudioEvents();
 SaveGame decoded;need(decodeSave(disk(w,s.mapPath),decoded,e),e.c_str());GameWorld resumed;auto clone=original;
 need(resumed.load(clone,l,e)&&resumed.restoreSnapshot(decoded.levels.at(s.mapPath).world,e),e.c_str());
 for(int tick=0;tick<100;++tick){
  w.takeAudioEvents();resumed.takeAudioEvents();w.tick(.014f,{});resumed.tick(.014f,{});
  for(int j=0;j<64;++j){auto a=emit(w,{1100,1100}),b=emit(resumed,{1100,1100});need(a==b,"same recycled IDs after reload");w.expireVisual(a);resumed.expireVisual(b);}
  need(w.actors().size()==size,"6400 subsequent transient births do not grow records");
  need(disk(w,s.mapPath)==disk(resumed,s.mapPath),"byte-identical recycling continuation after save/reload");
 }
 // A saved placement can override its class. Derive the role from that
 // resolved placement, even if an in-memory checkpoint carries stale flags.
 auto roles=w.snapshot();
 roles.objects[2].properties.push_back(block("TyPe",{},"AmMo"));
 roles.actors[2].effect=false;roles.actors[2].hasAmmoAttribute=false;
 roles.actors[3].effect=false;roles.actors[3].hasAmmoAttribute=true;
 need(w.restoreSnapshot(roles,e),e.c_str());
 need(w.actors()[2].hasAmmoAttribute&&!w.actors()[3].hasAmmoAttribute,"restore derives Ammo role from resolved instance, not cached flag or class");
 w.takeAudioEvents();w.tick(.014f,{});
 need(w.spawn({1},{1100,1100})==2,"released non-FX Ammo placement retains lowest-slot recycling");
 need(!w.actors()[2].hasAmmoAttribute,"reuse reconstructs the new instance role");
 auto unchanged=disk(w,s.mapPath);auto stale=w.snapshot();
 for(auto& actor:stale.actors)actor.hasAmmoAttribute=!actor.hasAmmoAttribute;
 SaveGame derived;derived.currentLevel=s.mapPath;derived.levels[s.mapPath].world=stale;
 std::vector<uint8_t> encoded;need(encodeSave(derived,encoded,e),e.c_str());
 need(encoded==unchanged,"derived Ammo role never changes serialized layout or bytes");
 // Restore discards conservative membership history. A later factory birth
 // must enter the index before its producer sets effect, even on a later tick.
 auto late=w.snapshot();
 for(size_t i=1;i<late.actors.size();++i){
  late.objects[i].authored.key="Create-object";
  late.actors[i].effect=false;
 }
 need(w.restoreSnapshot(late,e),e.c_str());
 w.takeAudioEvents();w.tick(.014f,{});
 const auto deferred=w.spawn({1},{1100,1100});
 need(deferred==late.actors.size(),"protected old slots require a new factory record");
 need(!w.actors()[deferred].effect,"generic factory has not yet assigned the FX role");
 const_cast<ActorState&>(w.actors()[deferred]).effect=true;
 w.expireVisual(deferred);
 const_cast<ActorState&>(w.actors()[deferred]).effect=false;
 w.takeAudioEvents();w.tick(.014f,{});
 const_cast<ActorState&>(w.actors()[deferred]).effect=true;
 w.takeAudioEvents();w.tick(.014f,{});
 need(w.spawn({1},{1100,1100})==deferred,"factory candidate survives delayed role assignment and earlier ineligible ticks");
 auto invalid=w.snapshot();invalid.objects[0].classId={999};
 const auto beforeRejected=disk(w,s.mapPath);
 need(!w.restoreSnapshot(invalid,e),"invalid restore rejected");
 need(disk(w,s.mapPath)==beforeRejected,"rejected restore leaves saved state unchanged");
 const_cast<ActorState&>(w.actors()[deferred]).effect=true;
 w.expireVisual(deferred);w.takeAudioEvents();w.tick(.014f,{});
 need(w.spawn({1},{1100,1100})==deferred,"rejected restore preserves candidate reuse");
 std::cout<<"PASS transient reclamation, pinned/authored preservation and100tick disk continuation\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
