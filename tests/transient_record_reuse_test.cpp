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
 std::cout<<"PASS transient reclamation, pinned/authored preservation and100tick disk continuation\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
