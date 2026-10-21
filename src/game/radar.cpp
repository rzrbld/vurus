#include "game/radar.h"
#include "game/properties.h"
#include <cmath>
namespace vyruz {
namespace {
std::optional<RadarKind> classify(const ObjectPlacement& object) {
    const auto kind=objectSubtype(object);
    if(kind=="lift")return RadarKind::Lift;
    if(kind=="item")return attribute(object.properties,"key")?
        std::optional<RadarKind>(RadarKind::Key):std::nullopt;
    if(kind=="ammo"||kind=="fx"||kind=="gate"||kind=="thing"||kind.empty())return {};
    return RadarKind::Enemy;
}
}
std::optional<RadarKind> RadarClassificationCache::resolve(const ObjectPlacement& object) {
    if(valid_ && &source_.read()==&object.properties.read() && path_==object.sprite.path)return kind_;
    valid_=false;source_=object.properties;path_=object.sprite.path;
    kind_=classify(object);valid_=true;
    return kind_;
}
std::optional<RadarBlip> radarBlip(const ObjectPlacement& object,bool alive,bool player,
                                  ContentPosition center,int height,RadarClassificationCache* cache) {
    if(!alive || object.flags.invisible.value_or(false))return {};
    const float dx=object.position.x-center.x,dy=object.position.y-center.y;
    if(!std::isfinite(dx)||!std::isfinite(dy)||static_cast<long double>(dx)*dx+static_cast<long double>(dy)*dy>=960.L*960.L)return {};
    auto color=cache?cache->resolve(object):classify(object);
    if(player)color=RadarKind::Player;
    if(!color)return {};
    return RadarBlip{72+static_cast<int>(dx/32),height-59+static_cast<int>(dy/32),*color};
}
}
