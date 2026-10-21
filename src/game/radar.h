#pragma once
#include "game/content.h"
#include <optional>
namespace vyruz {
enum class RadarKind { Enemy,Player,Key,Lift };
struct RadarBlip { int x=0,y=0;RadarKind kind=RadarKind::Enemy; };
// Presentation metadata only. A retained snapshot makes ordinary property
// writes detach; the primary sprite path also controls subtype fallback.
class RadarClassificationCache {
public:
    std::optional<RadarKind> resolve(const ObjectPlacement&);
private:
    SharedProperties source_;
    std::string path_;
    std::optional<RadarKind> kind_;
    bool valid_=false;
};
// Native surface-coordinate radius960, scale1/32, truncation toward zero.
std::optional<RadarBlip> radarBlip(const ObjectPlacement&,bool alive,bool player,
                                  ContentPosition center,int viewportHeight=480,
                                  RadarClassificationCache* cache=nullptr);
}
