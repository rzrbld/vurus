#pragma once
#include "game/content.h"
#include <cstddef>
#include <optional>
namespace vyruz {
struct GrenadeNativeContactResult {
    bool used=false,terminal=false;
    float proposedX=0,proposedY=0;
    std::optional<size_t> target;
    std::optional<ContentPosition> impactSurface;
    bool publicationPending=false,motionHandled=false;
    std::optional<size_t> contactHint;
};
}
