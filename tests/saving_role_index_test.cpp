#include "game/world.h"
#include "game/save_file.h"
#include "game/save_state.h"
#include <iostream>
#include <stdexcept>
using namespace vyruz;
namespace vyruz {struct SavingRoleIndexTestAccess {
 static auto select(GameWorld& w){w.updateSaving(0);return w.savePlot_;}
 static size_t spawn(GameWorld& w,ClassId c,ContentPosition p){return w.spawn(c,p);}
 static void remove(GameWorld& w,size_t id){w.destroy(id,std::nullopt);}
 static const auto& roles(const GameWorld& w){return w.savingRoles_;}
};}
namespace {
unsigned checks=0;void need(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
TextNode value(std::string k,std::string v){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(k);n.values={std::move(v)};return n;}
std::optional<size_t> reference(const GameWorld& w,const SceneContent& scene){
 if(w.saveRequest()||!w.player().alive||scene.objects[scene.player->value].flags.invisible.value_or(false))return {};
 const auto p=w.playerPosition();const auto& actors=w.actors();
 for(size_t area=0;area<actors.size();++area){const auto& o=scene.objects[area];if(!actors[area].alive)continue;const double radius=actors[area].radius+w.player().radius;
  if(nativeDistanceSquared(p.x-o.position.x,p.y-o.position.y)>radius*radius)continue;
  if(!propertyBool(o.properties,"Saving-zone"))continue;
  for(size_t id=0;id<actors.size();++id)if(actors[id].alive&&scene.objects[id].position.x==o.position.x&&scene.objects[id].position.y==o.position.y&&propertyBool(scene.objects[id].properties,"Saving-plot"))return id;
 }return {};
}
SceneContent fixture(){SceneContent s;s.mapPath="TEST/SAVING-ROLES.3DG";
 for(unsigned id=0;id<4;++id){ObjectClass c;c.id={id};c.name="Class"+std::to_string(id);c.behaviours={id==0?"Player":"Fixed"};c.properties={value("Managed","1"),value("Bounding-box-size","20"),value("Saving-zone",id==1?"1":"0"),value("Saving-plot",id==2?"1":"0")};s.classes.push_back(c);}
 auto place=[&](unsigned cls,float x,float y){ObjectPlacement o;o.id={uint32_t(s.objects.size())};o.classId={cls};o.position={x,y};o.properties=s.classes[cls].properties;o.behaviours=s.classes[cls].behaviours;s.objects.push_back(o);};
 place(0,200,200);place(1,200,200);place(1,220,200);place(2,220,200);place(2,220,200);place(3,200,200);s.player=ObjectId{0};return s;}
std::vector<uint8_t> disk(const GameWorld& w){SaveGame save;save.currentLevel="TEST/SAVING-ROLES.3DG";save.levels[save.currentLevel].world=w.snapshot();std::vector<uint8_t>b;std::string e;need(encodeSave(save,b,e),"encode fixture");return b;}
void compare(GameWorld& w,const SceneContent& s){const auto expected=reference(w,s);need(SavingRoleIndexTestAccess::select(w)==expected,"exact old full-scan selection");}
}
int main()try{
 auto s=fixture();Level l;l.mapW=l.mapH=100;l.hasTerrainCollision=true;GameWorld w;std::string e;need(w.load(s,l,e),"load role fixture");
 const auto cold=disk(w);SavingRoleIndex separate;separate.ensure(s,w.actors().size());need(disk(w)==cold,"derived cache warming changes no saved state");
 need(separate.zones()==std::vector<size_t>({1,2})&&separate.terminals()==std::vector<size_t>({3,4}),"roles are ordered independently of terminal/zone placement order");
 compare(w,s);need(SavingRoleIndexTestAccess::select(w)==3,"earliest zone lacking terminal does not hide later zone; lowest terminal ID wins");
 for(int x=140;x<281;++x){w.setPlayerPosition(float(x),200);compare(w,s);}
 // Read current geometry and visibility without rebuilding authored membership.
 w.setPlayerPosition(200,200);s.objects[3].position={400,400};compare(w,s);need(SavingRoleIndexTestAccess::select(w)==4,"moved first terminal no longer matches");s.objects[3].position={220,200};
 w.setPlayerPosition(200,200);s.objects[0].flags.invisible=true;compare(w,s);s.objects[0].flags.invisible=false;
 SavingRoleIndexTestAccess::remove(w,3);compare(w,s);need(SavingRoleIndexTestAccess::select(w)==4,"dead terminal remains role member but cannot win");
 const auto spawned=SavingRoleIndexTestAccess::spawn(w,{2},{200,200});compare(w,s);need(SavingRoleIndexTestAccess::select(w)==spawned,"spawned terminal belongs to first zone immediately");
 need(SavingRoleIndexTestAccess::roles(w).terminals().back()==spawned,"incremental birth is reflected in ordered role set");
 // Same slot replacement and boolean parser precedence use exact authored rules.
 auto changed=w.snapshot();changed.objects[5].properties={value("saving-ZONE","1"),value("Saving-zone","0"),value("Saving-PLOT","YES;")};need(w.restoreSnapshot(changed,e),"restore changed properties");compare(w,s);need(SavingRoleIndexTestAccess::roles(w).terminals()==std::vector<size_t>({3,4,5,spawned}),"restored nonmember becomes terminal and duplicate false zone wins");
 const auto before=disk(w);auto broken=w.snapshot();broken.player=99999;need(!w.restoreSnapshot(broken,e),"reject invalid restore");need(disk(w)==before,"rejected restore leaves live World bytes unchanged");compare(w,s);
 SaveGame saved;need(decodeSave(before,saved,e),"decode actual disk continuation");need(w.restoreSnapshot(saved.levels.begin()->second.world,e),"restore disk continuation");compare(w,s);
 auto rebound=s;rebound.objects[5].properties={value("Saving-zone","1")};GameWorld moved=std::move(w);moved.rebind(rebound,l);compare(moved,rebound);need(SavingRoleIndexTestAccess::roles(moved).zones()==std::vector<size_t>({1,2,5}),"move/rebind refreshes role property replacement");
 // Explicit slot refresh must remove old roles, not leave a stale membership.
 separate.ensure(rebound,rebound.objects.size());auto other=rebound.objects[5];other.properties={};separate.refresh(5,other,rebound.objects.size());need(separate.zones()==std::vector<size_t>({1,2}),"slot replacement removes previous role");
 auto row=other;row.id={uint32_t(rebound.objects.size())};row.properties={value("Saving-zone","true")};rebound.objects.push_back(row);separate.refresh(row.id.value,row,rebound.objects.size());need(separate.zones().back()==row.id.value,"appended role updates indexed count without a whole rebuild");
 separate.reset();separate.ensure(rebound,rebound.objects.size());need(separate.zones()==std::vector<size_t>({1,2,5,row.id.value}),"reset rebuilds all current authored roles");
 need(moved.load(s,l,e),"level reload resets membership");compare(moved,s);
 std::cout<<"PASS "<<checks<<" saving role/order/geometry/liveness/spawn/property/restore/move/rebind controls\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
