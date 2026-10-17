#include "game/radar.h"
#include "game/properties.h"
#include <cmath>
namespace vyruz {
std::optional<RadarBlip> radarBlip(const ObjectPlacement& object,bool alive,bool player,
                                  ContentPosition center,int height) {
    if(!alive || object.flags.invisible.value_or(false))return {};
    const float dx=object.position.x-center.x,dy=object.position.y-center.y;
    if(!std::isfinite(dx)||!std::isfinite(dy)||static_cast<long double>(dx)*dx+static_cast<long double>(dy)*dy>=960.L*960.L)return {};
    const auto kind=objectSubtype(object);
    RadarKind color=RadarKind::Enemy;
    if(player)color=RadarKind::Player;
    else if(kind=="lift")color=RadarKind::Lift;
    else if(kind=="item") {
        if(!attribute(object.properties,"key"))return {};
        color=RadarKind::Key;
    } else if(kind=="ammo"||kind=="fx"||kind=="gate"||kind=="thing"||kind.empty())return {};
    return RadarBlip{72+static_cast<int>(dx/32),height-59+static_cast<int>(dy/32),color};
}
}
