#pragma once
#include "game/properties.h"

namespace vyruz {
// Audited COMMON/THING products. Keep broader moving CREATE controllers out
// until their bounds/propulsion have their own native evidence.
inline bool launchedDeathProduct(const ObjectClass& cls) {
    bool known=false;
    for(const auto* name:{"Final-Explosion-Part","Big-Fire","Big-Fire-2",
        "Big-Particle-1","Big-Particle-2","Small-Particle-1","Small-Particle-2",
        "Small-Particle-3","Small-Particle-4","Small-Particle-5",
        "Red-Spark","Blue-Spark","White-Spark","Laser-Spark"})
        known|=contentNameEqual(cls.name,name);
    return known && propertyNumber(cls.properties,"Bounding-box-size")==0 &&
        propertyBool(cls.properties,"Launched") && propertyBool(cls.properties,"Use-inertia") &&
        std::any_of(cls.behaviours.begin(),cls.behaviours.end(),[](const auto& b){return contentNameIs(b,"straight");});
}
}
