#pragma once
#include <cstdint>
namespace vyruz {
//40f920 overlap and40fbc0/40ff00 line queries exclude animated Ammo.
//The bounds remain in the common graph for physical contacts. This filter
//does not exclude Player (native subtype18) or nonanimated decor.
inline bool nativeNavigationAnimatedActorAdmitted(uint32_t kind,uint32_t subtype) {
    return kind!=3 || subtype!=0x10;
}
}
