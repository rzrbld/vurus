#include "game/interactions.h"
#include "game/health_damage_native.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace vyruz {
bool hasKey(std::span<const KeyPiece> keys,int id) {
    if (!id) return true;
    int count=0,total=0;bool first=false;
    for (const auto& key:keys) if (key.id==id) {
        if (!key.code) return true;
        if (total && total!=key.total) return false;
        total=key.total;++count;first|=key.part==1;
    }
    // Native consumer counts matching objects, not distinct part numbers.
    return total>0 && count==total && first;
}
void consumeKey(std::vector<KeyPiece>& keys,int id) {
    std::erase_if(keys,[&](const auto& key){return key.id==id;});
}

bool applyDamage(VitalStats& stats, float amount, bool indestructible) {
    if (indestructible || !(amount > 0) || !std::isfinite(amount)) return false;
    const bool alive = stats.energy > 0;
    NativeHealthDamageInput input;
    input.energy=stats.energy;input.shield=stats.shield;input.shieldMinimum=stats.shieldMin;
    const auto result=nativeHealthDamage(input,amount);
    stats.energy=result.energy;stats.shield=result.shield;
    return alive && stats.energy <= 0;
}

bool applyPickup(VitalStats& stats, const PickupEffect& effect) {
    bool applied = false;
    if (effect.energy > 0 && std::isfinite(effect.energy) &&
        stats.energy < stats.energyMax * 0.95f) {
        stats.energy = std::min(stats.energyMax, stats.energy + effect.energy);
        applied = true;
    }
    if (effect.money > 0) {
        // Port counters are wide and saturating rather than allowing signed overflow.
        const auto add = [&](int64_t value) {
            return value > std::numeric_limits<int64_t>::max() - effect.money
                ? std::numeric_limits<int64_t>::max() : value + effect.money;
        };
        stats.money = add(stats.money);
        stats.lifetimeMoney = add(stats.lifetimeMoney);
        applied = true;
    }
    if (effect.shield > 0 && std::isfinite(effect.shield) && stats.shield < stats.shieldMax) {
        stats.shield = std::min(stats.shieldMax, stats.shield + effect.shield);
        applied = true;
    }
    return applied;
}

double nativeDistanceSquared(double dx, double dy) {
    return 0.625 * dx * dx + 2.5 * dy * dy;
}

std::optional<float> sweptCircleContact(float fromX, float fromY, float toX, float toY,
                                        float centerX, float centerY, float combinedRadius) {
    if (!(combinedRadius > 0) || !std::isfinite(combinedRadius)) return std::nullopt;
    const double x = double(fromX) - centerX, y = double(fromY) - centerY;
    const double dx = double(toX) - fromX, dy = double(toY) - fromY;
    const double a = nativeDistanceSquared(dx, dy);
    const double b = 0.625 * x * dx + 2.5 * y * dy;
    const double c = nativeDistanceSquared(x, y) - double(combinedRadius) * combinedRadius;
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || a == 0)
        return std::nullopt;
    if (c <= 0) return b < 0 ? std::optional<float>(0) : std::nullopt;
    if (b >= 0) return std::nullopt;
    const double discriminant = b * b - a * c;
    if (discriminant < 0) return std::nullopt;
    const double t = c / (-b + std::sqrt(discriminant));
    if (t < 0 || t > 1) return std::nullopt;
    return static_cast<float>(t);
}

} // namespace vyruz
