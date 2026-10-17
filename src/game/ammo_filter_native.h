#pragma once
#include <cstdint>
namespace vyruz {
// Stable native pointer/generation identity, distinct from class identity.
// The port's live owner indices are stable; pool recycling is limited to inert
// trails. Keep the full native contract here for future graph contact queries.
struct NativeAmmoContactObject {
    std::uintptr_t identity=0,creator=0;
    std::uint32_t generation=0,creatorGeneration=0,kind=3,subtype=0,shape=5,ageMs=0;
    bool ignoreOwner=false;
};
inline bool nativeAmmoOwnedBy(const NativeAmmoContactObject& ammo,
                              const NativeAmmoContactObject& other) {
    return ammo.creator==other.identity && ammo.creatorGeneration==other.generation;
}
// 41a570: only a TARGET Ammo's Ignore-owner flag participates in admission.
inline bool nativeAmmoIgnoresOwner(const NativeAmmoContactObject& other,
                                  const NativeAmmoContactObject& ammo) {
    return ammo.kind==3 && ammo.subtype==0x10 && ammo.ignoreOwner && nativeAmmoOwnedBy(ammo,other);
}
// Complete non-geometric 41a470 admission, before4069d0. Do not turn this
// into a symmetric source/target owner exclusion: native intentionally differs.
inline bool nativeAmmoContactAdmitted(const NativeAmmoContactObject& source,
                                     const NativeAmmoContactObject& target) {
    if(!source.identity || !target.identity || source.identity==target.identity)return false;
    if(source.shape==2 && target.shape==2)return false;
    if(source.subtype==0x10 && target.subtype==0x10)return false;
    if(target.subtype==0x10 &&
       ((target.ageMs<=499 && nativeAmmoOwnedBy(target,source)) || nativeAmmoIgnoresOwner(source,target)))return false;
    if(source.kind==3 && source.ageMs<=499 && nativeAmmoOwnedBy(source,target))return false;
    return true;
}
}
