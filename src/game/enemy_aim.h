#pragma once
#include "game/content.h"
#include "game/spider_contact_native.h"
#include <cmath>

namespace vyruz {
struct EnemyFireAim {bool inRange=false,fire=false;};
// 42ce40/42caa0/42d2d0: call after turning, before locomotion. Inputs use
// the selected body/gun heading. Route admission and weapon cooldown remain
// caller responsibilities; Tracker ammunition bypasses terminal-route/desired
// heading admission. The return/in-range state is distinct from the fire bit.
inline EnemyFireAim enemyFireAim(ContentPosition shooter,ContentPosition target,
        int desired,int facing,float minimumDistance,float weaponRange,
        bool direct,bool guidedAmmo=false) {
    if(facing<0 || facing>=24 || minimumDistance<0 || weaponRange<=0 ||
       !std::isfinite(minimumDistance) || !std::isfinite(weaponRange) ||
       !std::isfinite(shooter.x) || !std::isfinite(shooter.y) ||
       !std::isfinite(target.x) || !std::isfinite(target.y))return {};
    if(!guidedAmmo && (!direct || desired!=facing))return {};
    const auto from=spiderSurfaceToNative(shooter.x,shooter.y);
    const auto to=spiderSurfaceToNative(target.x,target.y);
    const float dx=to.x-from.x,dz=to.y-from.y;
    const long double distance=static_cast<long double>(dx)*dx+static_cast<long double>(dz)*dz;
    if(distance>static_cast<long double>(minimumDistance)*minimumDistance ||
       std::trunc(distance)>static_cast<long double>(weaponRange)*weaponRange)return {};
    return {true,spiderContactHeading({dx,dz})==facing};
}
}
