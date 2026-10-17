#pragma once
#include <cstdint>
#include "game/content.h"
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vyruz {

struct VitalStats {
    float energy = 0, energyMin = 0, energyMax = 0;
    float shield = 0, shieldMin = 0, shieldMax = 0;
    int64_t money = 0, lifetimeMoney = 0;
};
struct PickupEffect {
    float energy = 0, shield = 0;
    int64_t money = 0;
};
struct HeldCode {
    ObjectPlacement object;
    std::string mapPath,className;
    float remainingMs=0,classTimeoutMs=0;
};
struct KeyPiece { int id=0; bool code=false; int part=1,total=1; std::string text; std::optional<HeldCode> held; };
bool hasKey(std::span<const KeyPiece> keys,int id);
void consumeKey(std::vector<KeyPiece>& keys,int id);

// Shield absorbs damage before energy. Returns a new energy<=0 transition.
// Protection and contact eligibility beyond indestructibility belong to the caller.
bool applyDamage(VitalStats& stats, float amount, bool indestructible = false);
// Positive ordinary resource increments only; true means some effect was applied.
// Caller removes the ordinary pickup only on success. Keys/ammo are separate.
bool applyPickup(VitalStats& stats, const PickupEffect& effect);

double nativeDistanceSquared(double dx, double dy);
// Screen coordinates and a combined radius in native collision units.
// Existing overlaps can escape/tangent; inward motion contacts at zero.
std::optional<float> sweptCircleContact(float fromX, float fromY, float toX, float toY,
                                        float centerX, float centerY, float combinedRadius);

} // namespace vyruz
