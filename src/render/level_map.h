#pragma once
#include "game/level.h"
namespace vyruz {
// Cached all-layer map artwork. No collision-derived geometry or actor sprites.
Image makeLevelMap(const Level&,int width=640,int height=480);
}
