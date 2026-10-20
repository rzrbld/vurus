#include "render/sprite_properties.h"
#include <cmath>
#include <iostream>
#include <utility>
using namespace vyruz;
static TextNode field(std::string key,std::string value) {
    TextNode n;n.key=std::move(key);n.values={std::move(value)};return n;
}
static bool same(float a,float b) {return a==b || (std::isnan(a)&&std::isnan(b));}
static bool check(SpritePropertyCache& cache,const SharedProperties& properties) {
    const auto& nodes=properties.read();const auto& result=cache.resolve(properties);
    const auto* altitude=contentProperty(nodes,"Altitude");
    const float drawn=altitude&&!altitude->values.empty()?std::strtof(altitude->values.front().c_str(),nullptr):0;
    const auto* thrust=contentProperty(nodes,"Thrust");
    return same(result.altitude,drawn) && same(result.orbitAltitude,propertyNumber(nodes,"Altitude")) &&
        result.useDeltaPacking==propertyBool(nodes,"Use-delta-packing") &&
        same(result.thrustMax,thrust?propertyNumber(thrust->children,"Max"):0);
}
int main() {
    SpritePropertyCache cache;
    SharedProperties properties;
    if(!check(cache,properties))return 1;
    for(const char* text:{"12.5","-3;","7trailing","NaN","inf","","nonsense"}) {
        TextNode thrust;thrust.key="Thrust";thrust.children={field("Max",text)};
        properties={field("Altitude",text),field("Use-delta-packing","YeS;"),thrust};
        if(!check(cache,properties))return 2;
        if(!check(cache,properties))return 3;
    }
    properties={field("Altitude","12"),field("Use-delta-packing","1;")};
    if(!check(cache,properties))return 4;
    auto retained=properties;
    properties[0].values={"42"}; // COW from class/snapshot.
    if(!check(cache,properties)||!check(cache,retained))return 5;
    retained={field("Altitude","9")};
    if(!check(cache,retained))return 6;
    retained[0].values={"19"}; // Cache alone must also force COW.
    if(!check(cache,retained)||cache.resolve(retained).altitude!=19)return 7;
    retained.push_back(field("Only-base","1"));
    if(!check(cache,retained)||cache.resolve(retained).altitude!=19)return 8;
    for(int i=0;i<1000;++i) { // Replacement / allocator reuse / vector movement.
        properties={field("Altitude",std::to_string(i))};
        if(!check(cache,properties)||cache.resolve(properties).altitude!=i)return 9;
        auto moved=std::move(cache);cache=std::move(moved);
    }
    properties.clear();
    if(!check(cache,properties)||cache.resolve(properties).useDeltaPacking)return 10;
    std::cout<<"sprite property parsing and snapshot invalidation pass\n";
}
