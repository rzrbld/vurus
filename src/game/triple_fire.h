#pragma once

#include "game/combat.h"

namespace vyruz {
// 431d09 / 443ee0: copies and extra projectile origins use the same surface
// table50d440 as Start-distance. This is NOT a Weapons-gap/barrel distance.
inline ShotPositionOffset tripleFireOffset(int bodyDirection,float distance,int side) {
    if(side==0)return {};
    const int heading=((bodyDirection+(side<0?-6:6))%24+24)%24;
    return projectileStartOffset(heading,distance);
}
}
