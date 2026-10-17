#include "game/map_view.h"
#include "game/radar.h"
#include "game/map_markers.h"
#include "render/level_map.h"
#include <cstdlib>
#include <iostream>
using namespace vyruz;
static void check(bool b,const char* message){if(!b){std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    MapView view;
    check(!view.toggle(20999)&&!view.open(),"map requires threshold");
    check(view.toggle(21000)&&view.open(),"threshold admits without debit");
    check(view.advance(1000,21000)==1800,"stable one-second map cost");
    view.close();check(view.advance(1000,21000)==0,"closed map never charges");
    view.toggle(21000);int64_t total=0;
    for(int i=0;i<100;++i)total+=view.advance(10,21000-total);
    check(total==1800,"cost independent of update subdivisions");
    view.close();view.toggle(21000);
    check(view.advance(1000,65)==60&&!view.open(),"insufficient balance closes without overdraft");
    view.toggle(21000);check(view.advance(1000.0/60,30)==30 && view.open(),"final affordable quantum remains open");
    check(view.advance(1000.0/60,0)==0 && !view.open(),"next visit closes");
    view.toggle(21000);check(view.advance(1,29)==0&&!view.open(),"subquantum balance closes");
    view.toggle(21000);check(view.toggle(0)&&!view.open(),"closing needs no money");
    view.toggle(21000);check(view.advance(-1,21000)==0,"invalid delta does not debit");

    ObjectPlacement object;object.sprite.path="COMMON/DATA/OBJECT/COP/COP.GAF";
    object.position={64,-63};auto blip=radarBlip(object,true,false,{0,0});
    check(blip&&blip->x==74&&blip->y==420&&blip->kind==RadarKind::Enemy,"signed truncation and center");
    object.position={960,0};check(!radarBlip(object,true,false,{0,0}),"strict radar radius");
    object.position={959,0};check(radarBlip(object,true,false,{0,0}).has_value(),"inside radius");
    check(!radarBlip(object,false,false,{0,0}),"dead actors disappear");
    object.flags.invisible=true;check(!radarBlip(object,true,false,{0,0}),"invisible actors omitted");
    object.flags.invisible=false;object.position={0,0};
    for(auto kind:{"AMMO","FX","GATE","THING","ITEM"}) {
        object.sprite.path=std::string("OBJECT/")+kind+"/x.GAF";
        check(!radarBlip(object,true,false,{0,0}),"nonradar component filtered");
    }
    TextNode key;key.key="TYPE";key.arguments={"Key"};object.properties.push_back(key);
    check(radarBlip(object,true,false,{0,0})->kind==RadarKind::Key,"key item included");
    object.sprite.path="OBJECT\\LIFT\\LIFT.GAF";
    check(radarBlip(object,true,false,{0,0})->kind==RadarKind::Lift,"Lift included");
    check(radarBlip(object,true,true,{0,0})->kind==RadarKind::Player,"player marker");

    TextNode subtype;subtype.key="Sub-type";subtype.values={"FX"};object.properties.push_back(subtype);
    check(!radarBlip(object,true,false,{0,0}),"explicit subtype overrides Lift path");
    object.properties.back().values={"Item"};
    check(radarBlip(object,true,false,{0,0})->kind==RadarKind::Key,"explicit Item uses Key attribute");
    SceneContent scene;scene.objects.resize(5);
    for(auto& o:scene.objects)o.sprite.path="OBJECT/LIFT/x.GAF";
    TextNode saving;saving.key="Saving-plot";saving.values={"1"};
    scene.objects[1].sprite.path="OBJECT/ITEM/x.GAF";scene.objects[1].properties={saving};
    scene.objects[2].sprite.path="OBJECT/COP/x.GAF";scene.objects[2].properties={saving};
    scene.objects[4].properties={saving}; // Lift takes precedence.
    NativeActorCells cells;cells.reset(2,2);
    cells.relocate(0,0);cells.relocate(1,0);cells.relocate(2,3);cells.relocate(3,1);cells.relocate(4,2);
    MapMarkers markers;markers.rebuild(scene,cells);
    check(markers.lifts==std::vector<size_t>({3,4,0}) && markers.saves==std::vector<size_t>({1}),"native descendingY/X enrollment and subtype mask");
    cells.relocate(0,3);check(markers.lifts.back()==0,"membership stays cached between rebuilds");
    Level level;level.mapW=2;level.mapH=1;level.tileW=2;level.tileH=1;
    level.tiles={{2,1,{100,0,0,255,110,0,0,255}},{2,1,{0,200,0,255,0,0,0,0}}};
    level.layers={{1,1},{2,0}};
    auto map=makeLevelMap(level,4,1);
    check(map.rgba==std::vector<uint8_t>({0,208,0,255,110,8,0,255,100,8,0,255,110,8,0,255}),"all-layer map honors transparent patches and top-left cells");
    map=makeLevelMap(level,2,1);
    check(map.rgba[1]==208&&map.rgba[4]==110,"overview samples both map cells");
    level.layers.clear();map=makeLevelMap(level,4,1);
    for(size_t p=3;p<map.rgba.size();p+=4)check(map.rgba[p]==0,"unpainted overview leaves live gameplay visible");
    level.layers={{1,1}};level.tiles[0].rgba={0,0,0,255,0,0,0,255};map=makeLevelMap(level,4,1);
    for(size_t p=3;p<map.rgba.size();p+=4)check(map.rgba[p]==0,"native black key is rejected before green tint");
    check(makeLevelMap(level,0,1).rgba.empty(),"invalid output size");
}
