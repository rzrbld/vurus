#include "game/world.h"
#include "game/navigation_item_cache.h"
#include "game/save_state.h"
#include "game/save_file.h"
#include "snapshot_fixture.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace vyruz;
namespace vyruz {struct NavigationItemMetadataTestAccess {
 static bool resolve(GameWorld& w,size_t id){return w.navigationItemOnlyForPlayer(id);}
 static size_t entries(const GameWorld& w){return w.navigationItemMetadata_.size();}
};}
namespace {
unsigned checks=0;
void need(bool v,const std::string& s){++checks;if(!v)throw std::runtime_error(s);}
TextNode value(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=key;n.values={text};return n;}
TextNode item(std::vector<TextNode> children,std::string key="TYPE",std::string type="Item"){TextNode n;n.kind=TextKind::Block;n.key=key;n.arguments={type};n.children=std::move(children);return n;}
bool reference(const SharedProperties& p){const auto* n=attribute(p,"item");return n&&propertyBool(n->children,"Only-for-player");}
void compare(NavigationItemCache& c,const SharedProperties& p){const auto expected=reference(p);for(unsigned repeat=0;repeat<4;++repeat)need(c.resolve(p)==expected,"exact Item parser/cache result");}
SceneContent fixture(){
 SceneContent s;s.mapPath="TEST/ITEM-METADATA.3DG";
 for(unsigned id=0;id<3;++id){ObjectClass c;c.id={id};c.name=id==0?"Player":id==1?"Restricted":"Other";c.behaviours={id==0?"Player":"Fixed"};c.properties={value("Managed","1"),value("Sub-type",id==0?"Vyruz":"Item"),value("Bounding-box-size","10"),item({value("Only-for-player",id==1?"1":"0")})};if(id==1)c.properties.push_back(value("Saving-zone","1"));s.classes.push_back(c);}
 for(unsigned id=0;id<3;++id){ObjectPlacement o;o.id={id};o.classId={id};o.position={float(200+id*200),200};o.properties=s.classes[id].properties;o.behaviours=s.classes[id].behaviours;s.objects.push_back(o);}s.player=ObjectId{0};return s;
}
std::vector<uint8_t> encoded(const GameWorld& w){SaveGame s;s.currentLevel="TEST/ITEM-METADATA.3DG";s.levels[s.currentLevel].world=w.snapshot();std::vector<uint8_t> b;std::string e;need(encodeSave(s,b,e),e);return b;}
}
int main()try{
 NavigationItemCache cache;SharedProperties properties;compare(cache,properties);
 const char* values[]={"","1","0","true","false","TRUE;","YeS;","no;","invalid","2"," true","1;;"};
 for(const auto* a:values)for(const auto* b:values){
  properties={value("Only-for-player","1"),item({value("Only-for-player",a),value("Only-for-player",b)}),item({value("Only-for-player","1")})};compare(cache,properties);
  auto snap=properties;properties.write()[1].children.push_back(value("ONLY-FOR-PLAYER",a));compare(cache,properties);compare(cache,snap);
 }
 properties={item({value("Only-for-player","0")}),item({value("Only-for-player","1")})};need(!cache.resolve(properties),"first matching TYPE Item wins");properties.write().erase(properties.write().begin());need(cache.resolve(properties),"COW removal exposes next Item block");
 properties={item({value("Only-for-player","1"),value("Only-for-player","0")})};need(!cache.resolve(properties),"last matching child wins");
 properties={item({value("Only-for-player","YES;")},"tYpE","iTeM")};need(cache.resolve(properties),"case and terminal semicolon retained");
 auto copied=cache;compare(copied,properties);auto moved=std::move(cache);compare(moved,properties);compare(cache,SharedProperties{});compare(cache,properties);cache={};compare(cache,properties);
 std::mt19937 random(0x158124);for(unsigned n=0;n<10000;++n){std::vector<TextNode> nodes;for(unsigned j=0,end=random()%12;j<end;++j){auto node=item({value("Only-for-player",values[random()%12]),value("ONLY-FOR-PLAYER",values[random()%12])},random()%3?"TYPE":"Other",random()%3?"Item":"Ammo");node.kind=TextKind(random()%3);nodes.push_back(std::move(node));}properties=std::move(nodes);compare(cache,properties);if(n%13==0){properties.clear();compare(cache,properties);}}
 // Retaining the property snapshot prevents source-address reuse after row
 // replacement; unknown fields and subtype/path decisions remain elsewhere.
 for(unsigned n=0;n<1000;++n){SharedProperties row={item({value("Only-for-player",n%2?"1":"0")})};compare(cache,row);}
 auto scene=fixture();Level level;level.mapW=level.mapH=100;level.hasTerrainCollision=true;GameWorld w;std::string error;need(w.load(scene,level,error),error);
 need(!w.actors()[1].pickup,"Saving-zone intentionally clears pickup role");const auto cold=encoded(w);need(NavigationItemMetadataTestAccess::resolve(w,1),"query Item flag independent of pickup role");need(!NavigationItemMetadataTestAccess::resolve(w,2),"second row retains false");need(encoded(w)==cold,"warming metadata changes no disk bytes");
 auto before=w.snapshot();scene.objects[1].properties.write()[3].children.push_back(value("Only-for-player","0"));need(!NavigationItemMetadataTestAccess::resolve(w,1),"live ordinary COW edit refreshes metadata");need(w.restoreSnapshot(before,error),error);need(NavigationItemMetadataTestAccess::entries(w)==0,"accepted restore discards metadata");need(NavigationItemMetadataTestAccess::resolve(w,1),"restored properties rederive true");
 auto corrupt=w.snapshot();corrupt.player=9999;const auto unchanged=encoded(w);need(!w.restoreSnapshot(corrupt,error),"corrupt restore rejected");need(encoded(w)==unchanged&&NavigationItemMetadataTestAccess::resolve(w,1),"rejected restore leaves warm World intact");
 auto replacement=w.snapshot();replacement.objects[1].properties=scene.objects[2].properties;need(w.restoreSnapshot(replacement,error),error);need(!NavigationItemMetadataTestAccess::resolve(w,1),"same row replacement does not retain old flag");
 GameWorld movedWorld=std::move(w);auto rebound=scene;rebound.objects[1].properties={item({value("Only-for-player","1")})};movedWorld.rebind(rebound,level);need(NavigationItemMetadataTestAccess::entries(movedWorld)==0&&NavigationItemMetadataTestAccess::resolve(movedWorld,1),"World move/rebind discards derived metadata");
 need(movedWorld.load(scene,level,error),error);need(NavigationItemMetadataTestAccess::entries(movedWorld)==0,"level load clears metadata");
 std::cout<<"PASS "<<checks<<" Item metadata parser/COW/order/reuse/restore/World controls; entry bytes="<<sizeof(NavigationItemCache)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
