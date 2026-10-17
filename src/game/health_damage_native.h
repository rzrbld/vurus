#pragma once
#include <cstdint>

namespace vyruz {
// Original43ca30. The caller supplies resolved runtime/class fields; this
// helper never destroys, converts an upper piece, credits a kill or sets Hit.
struct NativeHealthDamageInput {
    float energy=0,shield=0,shieldMinimum=0;
    std::uint32_t actorFlags=0,behaviorFlags=0,classFlags1c=0,classFlags20=0;
    bool worldPresent=true,managerPresent=true,targetPresent=true,classPresent=true;
};
struct NativeHealthDamageResult {
    float energy=0,shield=0;
    bool awakeRequested=false,healthApplied=false,energyUpdated=false;
    // Exact original return1: not a generic newly-dead transition. Only the
    // energy path can return1, and only for class+20 bit0x20. A shield-only hit
    // returns0 even when the actor started with nonpositive Energy.
    bool lethalClassCallback=false;
};
inline NativeHealthDamageResult nativeHealthDamage(const NativeHealthDamageInput& in,float damage) {
    NativeHealthDamageResult out;out.energy=in.energy;out.shield=in.shield;
    if(!in.worldPresent || !in.managerPresent || !in.targetPresent)return out;
    if((in.actorFlags&8) && (in.behaviorFlags&3)==3)return out;
    out.awakeRequested=in.classPresent && !(in.classFlags1c&0x10) &&
                       !(in.actorFlags&0x08000000);
    // Native awake admission deliberately precedes protection rejection.
    if(in.actorFlags&0x00a00000)return out;
    out.healthApplied=true;
    long double overflow=damage;
    if(in.shield>0) {
        const long double remaining=static_cast<long double>(in.shield)-damage;
        out.shield=static_cast<float>(remaining);
        // Original tests x87 C0, so unordered follows its overflow branch.
        if(remaining>=0)return out;
        out.shield=in.shieldMinimum;
        overflow=-remaining;
    }
    out.energyUpdated=true;
    const long double energy=static_cast<long double>(in.energy)-overflow;
    out.energy=static_cast<float>(energy);
    // Comparison uses the extended value, not the stored float. Unordered
    // also follows the original<=0 result. Native does not reject damage<=0
    // or a target whose Energy was already nonpositive.
    out.lethalClassCallback=!(energy>0) && in.classPresent && (in.classFlags20&0x20);
    return out;
}
} // namespace vyruz
