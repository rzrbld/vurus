#pragma once
#include "game/properties.h"

namespace vyruz {
// Renderer-only authored values: never serialized as simulation state.
class SpritePropertyCache {
public:
    struct Values {
        float altitude=0, orbitAltitude=0, thrustMax=0;
        bool useDeltaPacking=false;
    };
    const Values& resolve(const SharedProperties& properties) {
        if(valid_ && &source_.read()==&properties.read())return values_;
        // Holding the snapshot makes even a formerly unique write detach. It
        // also prevents address reuse from mistaking a replacement for a hit.
        source_=properties;valid_=true;values_={};
        const auto& nodes=source_.read();
        // Drawing historically accepts strtof prefixes/nonfinite values;
        // attached orbits use the stricter propertyNumber parser. Keep both.
        if(const auto* node=contentProperty(nodes,"Altitude");node && !node->values.empty())
            values_.altitude=std::strtof(node->values.front().c_str(),nullptr);
        values_.orbitAltitude=propertyNumber(nodes,"Altitude");
        values_.useDeltaPacking=propertyBool(nodes,"Use-delta-packing");
        if(const auto* thrust=contentProperty(nodes,"Thrust"))
            values_.thrustMax=propertyNumber(thrust->children,"Max");
        return values_;
    }
private:
    SharedProperties source_;
    Values values_;
    bool valid_=false;
};
}
