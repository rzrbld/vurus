#pragma once
#include "game/content.h"
#include <cmath>

namespace vyruz {
// Moving Ammo441b41..441d11: On-object-hit selects physical response for
// non-actor contacts even when both presentation bounce flags are clear.
inline bool nativeAmmoTerrainResponse(bool onHit,bool onObjectHit) {
    return !onHit && onObjectHit;
}
struct NativeBounceFxPoint {
    ContentPosition projected{},placement{};
    unsigned quadrant=0; // Up-left, Up-right, Down-left, Down-right.
};
// 43d1af..43d2ab: projected contact determines the directional class before
// truncation; construction truncates X and (Y-8) independently.
inline NativeBounceFxPoint nativeBounceFxPoint(ContentPosition source,ContentPosition nativePoint) {
    NativeBounceFxPoint out;
    out.projected={static_cast<float>((static_cast<long double>(nativePoint.x)-nativePoint.y)*.8944271802902222f),
                   static_cast<float>((static_cast<long double>(nativePoint.x)+nativePoint.y)*.4472135901451111f)};
    out.quadrant=(out.projected.x>source.x?1u:0u)+(out.projected.y>source.y?2u:0u);
    out.placement={std::trunc(out.projected.x),static_cast<float>(std::trunc(static_cast<long double>(out.projected.y)-8.f))};
    return out;
}
}
