#include "game/world.h"
#include "game/class_name_index.h"
#include "game/properties.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace vyruz;
namespace vyruz {struct ClassNameIndexTestAccess {
 static const ObjectClass* find(GameWorld& w,std::string_view name){return w.classByName(name);}
};}
namespace {
unsigned checks=0;
void need(bool v,const std::string& s){++checks;if(!v)throw std::runtime_error(s);}
ObjectClass cls(std::string name,unsigned id){ObjectClass c;c.name=std::move(name);c.id={id};return c;}
void compare(ClassNameIndex& index,const SceneContent& scene,const std::string& name){need(index.find(scene.classes,name)==findClass(scene,name),"first case-insensitive class identity: "+name);}
TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(k);n.values={std::move(v)};return n;}
SceneContent fixture(){
 SceneContent s;s.mapPath="TEST/CLASS-INDEX.3DG";
 auto p=cls("Player",0);p.behaviours={"Player"};p.properties={value("Managed","1"),value("Sub-type","Vyruz"),value("Bounding-box-size","10")};
 auto a=cls("Ammo",1);a.behaviours={"Tracker"};TextNode type;type.key="TYPE";type.arguments={"Ammo"};type.children={value("Usable-when-invisible","1")};a.properties={type};
 s.classes={p,a};ObjectPlacement o;o.id={0};o.classId={0};o.properties=p.properties;o.behaviours=p.behaviours;o.position={200,200};s.objects={o};s.player=ObjectId{0};return s;
}
std::vector<uint8_t> encoded(const GameWorld& w){SaveGame s;s.currentLevel="TEST/CLASS-INDEX.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t> b;std::string error;need(encodeSave(s,b,error),error);return b;}
}
int main()try{
 ClassNameIndex index;SceneContent s;compare(index,s,"");compare(index,s,"missing");
 s.classes={cls("Rocket",99),cls("rocket",0),cls("",13),cls("ROCKET",7),cls(std::string("A\0B",3),23)};
 for(const std::string name:{"Rocket","rocket","ROCKET","missing","","A"})compare(index,s,name);
 compare(index,s,std::string("a\0b",3));need(index.find(s.classes,"rOcKeT")==&s.classes[0],"catalog order, not ClassId, resolves duplicate names");
 auto copy=index;auto moved=std::move(index);compare(moved,s,"Rocket");compare(index,s,"Rocket");compare(copy,s,"Rocket");
 s.classes.push_back(cls("new",24));compare(index,s,"NEW");compare(index,s,"Rocket");
 s.classes.erase(s.classes.begin());compare(index,s,"Rocket");
 // Same-size authored catalog edits are explicit rebinding boundaries.
 s.classes[0].name="Changed";index.clear();compare(index,s,"Rocket");compare(index,s,"changed");
 std::mt19937 random(0x41a010);s.classes.clear();
 for(unsigned i=0;i<400;++i){std::string name;for(unsigned j=0,n=random()%18;j<n;++j)name+=char(random()%256);s.classes.push_back(cls(name,random()));if(i%7==0)s.classes.push_back(cls(name,random()));}
 for(unsigned i=0;i<20000;++i){auto name=s.classes[random()%s.classes.size()].name;for(auto& c:name)if(c>='a'&&c<='z'&&random()%2)c=char(c-'a'+'A');if(i%3==0)name+='?';compare(index,s,name);}
 for(unsigned i=0;i<256;++i)compare(index,s,std::string(1,char(i)));
 auto scene=fixture();Level level;level.mapW=level.mapH=100;level.hasTerrainCollision=true;GameWorld w;std::string error;need(w.load(scene,level,error),error);const auto cold=encoded(w);
 need(ClassNameIndexTestAccess::find(w,"AMMO")==&scene.classes[1],"World first lookup");need(encoded(w)==cold,"warm index is absent from saves");
 auto originalProperties=scene.classes[1].properties;scene.classes[1].properties.write()[0].children[0].values={"0"};
 const auto* current=ClassNameIndexTestAccess::find(w,"ammo");need(current==&scene.classes[1]&&!propertyBool(attribute(current->properties,"ammo")->children,"Usable-when-invisible"),"current class properties remain dynamic after COW");need(propertyBool(attribute(originalProperties,"ammo")->children,"Usable-when-invisible"),"original property snapshot retained");
 auto before=w.snapshot();need(w.restoreSnapshot(before,error),error);need(ClassNameIndexTestAccess::find(w,"Ammo")==&scene.classes[1],"restore refreshes derived lookup");
 auto invalid=before;invalid.player=999;const auto bytes=encoded(w);need(!w.restoreSnapshot(invalid,error),"invalid restore rejected");need(encoded(w)==bytes&&ClassNameIndexTestAccess::find(w,"ammo")==&scene.classes[1],"rejected restore preserves World");
 auto rebound=scene;std::swap(rebound.classes[0].name,rebound.classes[1].name);GameWorld movedWorld=std::move(w);movedWorld.rebind(rebound,level);need(ClassNameIndexTestAccess::find(movedWorld,"ammo")==&rebound.classes[0],"move/rebind refreshes same-size catalog names");
 need(movedWorld.load(scene,level,error),error);need(ClassNameIndexTestAccess::find(movedWorld,"Ammo")==&scene.classes[1],"load refreshes catalog");
 SaveGame decoded;need(decodeSave(encoded(movedWorld),decoded,error),error);GameWorld disk;auto diskScene=scene;need(disk.load(diskScene,level,error)&&disk.restoreSnapshot(decoded.levels.begin()->second.world,error),error);
 for(unsigned t=0;t<30;++t){movedWorld.tick(1.f/60,{});disk.tick(1.f/60,{});ClassNameIndexTestAccess::find(movedWorld,"ammo");need(encoded(movedWorld)==encoded(disk),"cold/warm disk continuation");}
 std::cout<<"PASS "<<checks<<" class-name first-match/case/COW/catalog/lifecycle/save controls\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
