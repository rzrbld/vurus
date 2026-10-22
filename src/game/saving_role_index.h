#pragma once
#include "game/content.h"
#include "game/properties.h"
#include <algorithm>
#include <vector>

namespace vyruz {
// Derived authored roles only: never cache position, liveness or a save decision.
// Bound property-role replacement requires refresh, load/restore or World rebind.
class SavingRoleIndex {
    std::vector<size_t> zones_,terminals_;
    size_t count_=0;
    bool valid_=false;
    static void set(std::vector<size_t>& ids,size_t id,bool present) {
        const auto at=std::lower_bound(ids.begin(),ids.end(),id);
        if(present){if(at==ids.end()||*at!=id)ids.insert(at,id);}
        else if(at!=ids.end()&&*at==id)ids.erase(at);
    }
public:
    void reset(){valid_=false;}
    void ensure(const SceneContent& scene,size_t count) {
        if(valid_&&count_==count)return;
        zones_.clear();terminals_.clear();
        for(size_t id=0;id<count;++id){
            const auto& properties=scene.objects[id].properties;
            if(propertyBool(properties,"Saving-zone"))zones_.push_back(id);
            if(propertyBool(properties,"Saving-plot"))terminals_.push_back(id);
        }
        count_=count;valid_=true;
    }
    void refresh(size_t id,const ObjectPlacement& object,size_t count) {
        if(!valid_)return;
        if(id>=count || (count!=count_ && (count!=count_+1 || id!=count_))){reset();return;}
        set(zones_,id,propertyBool(object.properties,"Saving-zone"));
        set(terminals_,id,propertyBool(object.properties,"Saving-plot"));
        count_=count;
    }
    const auto& zones()const{return zones_;}
    const auto& terminals()const{return terminals_;}
};
}
