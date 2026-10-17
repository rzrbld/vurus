#pragma once
#include <cstdint>

namespace vyruz {
// Original actor+264/+268/+26c and raw actor+12c words. Increments wrap;
// decrements test +12c as signed-positive. This is not a count of surviving
// port Projectile entries: a direct impact and later release can both reduce it.
struct NativeAmmoOwnerCounters {
    std::uint32_t misses=0,hits=0,kills=0,activeAmmo=0;
};
inline void nativeAmmoOwnerDecrement(NativeAmmoOwnerCounters& counters) {
    if(counters.activeAmmo && !(counters.activeAmmo&0x80000000u))--counters.activeAmmo;
}
inline void nativeAmmoOwnerLaunched(NativeAmmoOwnerCounters& counters) {
    ++counters.activeAmmo; //444076..444087, successful emitter construction.
}
inline void nativeAmmoOwnerDirectContact(NativeAmmoOwnerCounters& counters,
                                         bool validOwner,bool actorTarget) {
    //43d734/43d754, reached only after the caller's Hit-fx admission permits
    //the direct phase. actorTarget means nonnull target of native kind3.
    if(!validOwner)return;
    if(actorTarget)++counters.hits;else ++counters.misses;
    nativeAmmoOwnerDecrement(counters);
}
inline void nativeAmmoOwnerReleased(NativeAmmoOwnerCounters& counters,
                                    bool validOwner,bool sourceIsAmmo,
                                    bool keepSource,bool keepExplosion) {
    //437e05..437e46. Both keep arguments independently suppress the physical
    //release path. Native generation validation is the caller's responsibility.
    if(validOwner && sourceIsAmmo && !keepSource && !keepExplosion)
        nativeAmmoOwnerDecrement(counters);
}
} //namespace vyruz
