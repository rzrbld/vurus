#pragma once
#include <cstdint>
#include <optional>
#include <span>

namespace vyruz {
// Raw native subtype values are hexadecimal (Ammo0x10,FX0x11,Item0x12).
// Cell traversal, bounds and damage-distance tests belong to the caller.
struct NativeRadialAdmissionInput {
    std::uint32_t targetKind=3,targetSubtype=0,targetFlags=0,targetClassFlags20=0;
    float targetEnergy=0;
    bool targetIsSource=false,onlyForPlayerItem=false,sourcePresent=false,sourcePlayerOwned=false;
    std::uint32_t sourceSubtype=0,targetAge=0,sourceAge=0,tickMilliseconds=0;
};
inline bool nativeRadialAdmitted(const NativeRadialAdmissionInput& in) {
    if(in.targetIsSource || in.targetKind!=3 || (in.targetFlags&0x00a00000) ||
       in.targetSubtype==0x11 || !(in.targetEnergy>0))return false;
    if(in.sourcePresent && in.sourceSubtype==0x11 && (in.targetFlags&0x00400000))return false;
    if((in.targetClassFlags20&0x40) && !in.sourcePlayerOwned)return false;
    if(in.targetSubtype==0x12 && in.onlyForPlayerItem)return false;
    if(in.sourcePresent && in.sourceSubtype==0x10 && in.targetSubtype==0x10) {
        // Native wraps the subtraction, tests its sign, negates if negative,
        // then compares unsigned. Avoid signed-overflow/abs(INT_MIN) UB.
        std::uint32_t difference=in.targetAge-in.sourceAge;
        if(difference&0x80000000u)difference=0u-difference;
        if(difference<=in.tickMilliseconds)return false;
    }
    return true;
}

// Native43c990 validates the generation on each creator link and clears the
// first stale node's pointer. This pure form returns that mutation to caller.
// A finite node inventory also bounds corrupt cycles (not authored behavior).
struct NativeCreatorNode {
    std::uint32_t generation=0,creatorGeneration=0;
    std::optional<std::size_t> creator;
};
struct NativeCreatorAncestry {
    bool owned=false;
    std::optional<std::size_t> staleLinkNode;
};
inline NativeCreatorAncestry nativeCreatorAncestry(std::span<const NativeCreatorNode> nodes,
                                                  std::optional<std::size_t> source,
                                                  std::optional<std::size_t> ancestor) {
    if(!source || !ancestor || *source>=nodes.size() || *ancestor>=nodes.size())return {};
    auto current=*source;
    for(std::size_t count=0;count<nodes.size();++count) {
        const auto& node=nodes[current];
        if(!node.creator || *node.creator>=nodes.size())return {};
        const auto creator=*node.creator;
        if(nodes[creator].generation!=node.creatorGeneration)return {false,current};
        if(creator==*ancestor)return {true,{}};
        current=creator;
    }
    return {};
}

struct NativeAmmoKillCreditInput {
    float targetEnergy=0;
    std::uint32_t targetFlags=0,targetClassFlags20=0,targetSubtype=0;
    bool sourcePresent=false,sourceIsAmmo=false,sourcePlayerOwned=false;
    bool immediateOwnerValid=false,immediateOwnerIsPlayer=false,targetCountsAsEnemy=false;
};
struct NativeAmmoKillCredit {
    bool fastKill=false,ownerKill=false,ownerCommentary=false,markCounted=false;
};
inline NativeAmmoKillCredit nativeAmmoKillCredit(const NativeAmmoKillCreditInput& in,bool radial) {
    NativeAmmoKillCredit out;
    if(!in.sourcePresent || in.targetEnergy>0 || (in.targetFlags&0x40) ||
       ((in.targetClassFlags20&2) && (in.targetFlags&0x20)))return out;
    out.fastKill=radial && in.sourcePlayerOwned && in.targetCountsAsEnemy;
    if(in.sourceIsAmmo) {
        // Direct43d6d1 restricts kill credit to subtype22..32 (native bitmask
        // with x86 shift-count masking). Radial43cdeb has no such type gate.
        const bool directType=(0x1ffc0u&(1u<<((in.targetSubtype-0x10u)&31u)))!=0;
        if(!radial && (!in.immediateOwnerValid || !directType))return out;
        out.markCounted=true; // Radial even absent/stale owner:43ce5c.
        out.ownerKill=in.immediateOwnerValid;
        out.ownerCommentary=out.ownerKill && in.immediateOwnerIsPlayer;
    }
    return out;
}
inline std::uint32_t nativeRadialHitFlags(std::uint32_t targetFlags) {
    return targetFlags&0x00800000?targetFlags:targetFlags|0x200;
}
} // namespace vyruz
