#pragma once
#include "game/content.h"

namespace vyruz {
// Native 435885 speed readout: magnitude per14ms, threshold substitutions,
// multiply100 then truncate and signed remainder1000. Not surface pixels/sec.
int nativeSpeedReadout(float magnitude);
// Authored-data equivalent of native 4371b0/437240; liveness is caller-owned.
bool countsAsEnemy(const ObjectPlacement&, const SceneContent&);
// Native combat-family GAF directory mapping (subtypes 0x16..0x20).
// This is the direct-hit predicate, not the native radial-credit special case.
bool killCreditEligible(const ObjectPlacement&);
}
