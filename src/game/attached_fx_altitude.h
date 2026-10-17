#pragma once
#include "game/properties.h"

namespace vyruz {
struct AttachedFxAltitude {
    float initial=0,amplitude=0,pulsation=0,phase=0;
    bool onlyWhenVisible=false;
};
inline AttachedFxAltitude attachedFxAltitudeConfig(const ObjectClass& cls) {
    AttachedFxAltitude result;result.initial=std::trunc(propertyNumber(cls.properties,"Altitude"));
    if(const auto* fx=attribute(cls.properties,"fx")) {
        result.amplitude=propertyNumber(fx->children,"vertical-amplitude");
        result.pulsation=propertyNumber(fx->children,"vertical-pulsation");
        result.phase=propertyNumber(fx->children,"vertical-phase");
        result.onlyWhenVisible=propertyBool(fx->children,"only-when-visible");
    }
    return result;
}
// 41c180 adds the two integer altitude fields before storing attachment height.
inline float initialAttachedFxAltitude(const AttachedFxAltitude& c,float parent) {
    return std::trunc(parent)+c.initial;
}
// 437b00 copies parent height for only-when-visible;444f42..444f72 otherwise
// leaves zero-amplitude height untouched. Nonzero amplitude overwrites it,
// adding the integer parent height before truncating (never abs/round).
inline float advanceAttachedFxAltitude(const AttachedFxAltitude& c,float retained,
        float parent,double timeMs) {
    if(c.amplitude==0)return c.onlyWhenVisible?std::trunc(parent):retained;
    const long double time=static_cast<float>(timeMs); // native444f0e float store
    const long double angle=time*c.pulsation+c.phase;
    return static_cast<float>(std::trunc(std::sin(angle)*c.amplitude+std::trunc(parent)));
}
}
