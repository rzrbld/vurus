#pragma once
#include "game/properties.h"

namespace vyruz {
// 444ca0 / 4452c0: attached bonus-invulnerability FX own the protection
// timer. Expiry resets the opposite authored timeout, discarding overshoot.
struct TimedProtectionConfig { float onMs=0,offMs=0; };
inline TimedProtectionConfig timedProtectionConfig(const SceneContent& scene,const ObjectPlacement& object) {
    for(const auto& add:object.properties) {
        if(!contentNameIs(add.key,"add") || add.arguments.empty() || !contentNameIs(add.arguments.front(),"fx"))continue;
        const auto* cls=findClass(scene,propertyText(add.children,"Name"));
        const auto* fx=cls?attribute(cls->properties,"fx"):nullptr;
        if(!fx || !contentNameIs(propertyText(fx->children,"check-event"),"bonus-invulnerability"))continue;
        const float on=propertyNumber(fx->children,"on-timeout"),off=propertyNumber(fx->children,"off-timeout");
        if(on>0 && off>0)return {on,off};
    }
    return {};
}
struct TimedProtectionState {
    TimedProtectionConfig config;
    float onRemainingMs=0,offRemainingMs=0;
    bool active=false;
    void reset(TimedProtectionConfig value) {
        config=value;onRemainingMs=value.onMs;offRemainingMs=value.offMs;active=false;
    }
    void advance(float milliseconds) {
        if(milliseconds<=0 || config.onMs<=0 || config.offMs<=0)return;
        if(onRemainingMs>0) {
            active=true;onRemainingMs-=milliseconds;
            if(onRemainingMs<=0){offRemainingMs=config.offMs;active=false;}
        } else if(offRemainingMs>0) {
            active=false;offRemainingMs-=milliseconds;
            if(offRemainingMs<=0){onRemainingMs=config.onMs;active=true;}
        }
    }
};
}
