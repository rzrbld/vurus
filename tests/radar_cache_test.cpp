#include "game/radar.h"
#include "game/properties.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
using namespace vyruz;
static void need(bool b,const char* message){if(!b)throw std::runtime_error(message);}
// Retained pre-cache radar implementation: independent classification oracle.
static std::optional<RadarBlip> oracle(const ObjectPlacement& object,bool alive,bool player,ContentPosition center,int height) {
    if(!alive || object.flags.invisible.value_or(false))return {};
    const float dx=object.position.x-center.x,dy=object.position.y-center.y;
    if(!std::isfinite(dx)||!std::isfinite(dy)||static_cast<long double>(dx)*dx+static_cast<long double>(dy)*dy>=960.L*960.L)return {};
    const auto kind=objectSubtype(object);
    RadarKind color=RadarKind::Enemy;
    if(player)color=RadarKind::Player;
    else if(kind=="lift")color=RadarKind::Lift;
    else if(kind=="item") {
        if(!attribute(object.properties,"key"))return {};
        color=RadarKind::Key;
    } else if(kind=="ammo"||kind=="fx"||kind=="gate"||kind=="thing"||kind.empty())return {};
    return RadarBlip{72+static_cast<int>(dx/32),height-59+static_cast<int>(dy/32),color};
}
static bool same(std::optional<RadarBlip> a,std::optional<RadarBlip> b){return bool(a)==bool(b)&&(!a||(a->x==b->x&&a->y==b->y&&a->kind==b->kind));}
static TextNode subtype(std::string value){TextNode n;n.key="Sub-type";n.values={std::move(value)};return n;}
static TextNode type(std::string value){TextNode n;n.key="TYPE";n.arguments={std::move(value)};return n;}
static void compare(RadarClassificationCache& cache,ObjectPlacement& o) {
    for(bool alive:{false,true})for(bool player:{false,true})for(int height:{240,480,720}) {
        const ContentPosition center{2.25f,-31.5f};
        const auto expected=oracle(o,alive,player,center,height);
        need(same(expected,radarBlip(o,alive,player,center,height,&cache)),"cached radar differs from pre-cache parser");
        need(same(expected,radarBlip(o,alive,player,center,height)),"uncached radar differs from pre-cache parser");
    }
}
int main(){try {
    RadarClassificationCache cache;ObjectPlacement object;object.position={4,-36};
    for(const char* path:{"","file.gaf","/file.gaf","OBJECT/UNKNOWN/x.GAF","OBJECT\\LIFT\\x.GAF","OBJECT/ITEM/x.GAF","OBJECT/FX/x.GAF"})
    for(const char* explicitType:{"","FX","Item","Lift","Unknown","Thing","Ammo","Gate","COP"," ;"})
    for(const char* key:{"","Key","Code","kEy"}) {
        object.sprite.path=path;object.properties={subtype(explicitType)};
        if(*key)object.properties.push_back(type(key));
        for(auto invisible:{std::optional<bool>{},std::optional<bool>{false},std::optional<bool>{true}}){object.flags.invisible=invisible;compare(cache,object);compare(cache,object);}
    }
    object.flags.invisible=false;object.sprite.path="OBJECT/ITEM/x.GAF";object.properties={type("Key")};
    compare(cache,object);const auto* before=&object.properties.read();
    object.properties[0].arguments={"Code"};need(&object.properties.read()!=before,"cache snapshot forces ordinary writes to detach");compare(cache,object);
    auto snapshot=object;object.properties={subtype("Unknown")};compare(cache,object);object=snapshot;compare(cache,object);
    object.properties.clear();compare(cache,object);object.sprite.path="OBJECT/LIFT/x.GAF";compare(cache,object);
    object.sprite.path="OBJECT/ITEM/x.GAF";object.properties={subtype("Item"),type("Key"),subtype("FX")};compare(cache,object);
    object.properties.erase(object.properties.read().end()-1);compare(cache,object);
    const float inf=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN();
    for(ContentPosition pos: {ContentPosition{962.25f,-31.5f},{std::nextafter(962.25f,0.f),-31.5f},{2.25f,928.5f},{inf,0},{nan,0},{0,-inf},{-0.f,0.f}}){object.position=pos;compare(cache,object);}
    std::mt19937 random(3941);
    for(unsigned n=0;n<10000;++n) {
        if(n%7==0)object.properties={subtype(n%2?"UNKNOWN":"Item"),type(n%3?"Key":"Code")};
        if(n%11==0)object.sprite.path=n%2?"OBJECT/LIFT/a.GAF":"OBJECT/ITEM/b.GAF";
        object.position={float(int(random()%240000)-120000)/100.f,float(int(random()%240000)-120000)/100.f};compare(cache,object);
        if(n%13==0){auto copy=cache;cache=std::move(copy);compare(cache,object);}
    }
    cache={};compare(cache,object); // Map/restore reset; same row may now hold another object.
    std::cout<<"PASS radar parser/cache equivalence, 10000 positions, dynamic flags/player, radius, COW/path/reuse/restore; entry bytes="<<sizeof(cache)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
