#pragma once
#include "game/content.h"
#include <algorithm>
#include <cmath>
namespace vyruz {
inline ContentPosition presentationPosition(ContentPosition previous,ContentPosition current,float alpha) {
    // Teleports and spawn/level changes must never sweep through the map.
    if((previous.x!=current.x || previous.y!=current.y) &&
       std::hypot(current.x-previous.x,current.y-previous.y)>64)return current;
    alpha=std::clamp(alpha,0.f,1.f);
    return {previous.x+(current.x-previous.x)*alpha,previous.y+(current.y-previous.y)*alpha};
}
}
