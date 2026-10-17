#pragma once
#include "game/content.h"
#include "game/native_actor_cells.h"
#include "game/properties.h"
namespace vyruz {
//43c700 initial enrollment only. Lists contain stable port IDs for shipped
//Fixed, indestructible landmarks; native raw-slot reuse is a separate boundary.
struct MapMarkers {
    std::vector<size_t> lifts,saves;
    void rebuild(const SceneContent& scene,const NativeActorCells& cells) {
        lifts.clear();saves.clear();
        for(int y=cells.height-1;y>=0;--y)for(int x=cells.width-1;x>=0;--x)
            for(size_t id=cells.heads[size_t(x)*cells.height+y];id!=NativeActorCells::Missing;id=cells.next[id]) {
                if(id>=scene.objects.size())continue;
                const auto& o=scene.objects[id];const auto type=objectSubtype(o);
                if(type!="fx" && type!="item" && type!="lift" && type!="gate")continue;
                if(type=="lift")lifts.push_back(id);
                else if(propertyBool(o.properties,"Saving-plot"))saves.push_back(id);
            }
    }
};
inline int mapMarkerCoordinate(float position,float extent,int mapExtent) {
    const float scale=extent/static_cast<float>(mapExtent);
    return static_cast<int>(static_cast<long double>(position)*scale);
}
}
