#pragma once
#include "game/ammo_filter_native.h"
#include "game/motion_contact.h"
#include <span>
namespace vyruz {
//4419e3/441a6b: the impact-abort skips441d11's ordinary unscale.
inline ContactVelocity nativeActorAmmoAbortVelocity(std::uint32_t subtype,
        std::uint32_t behavior,ContactVelocity current,ContactVelocity proposed,
        ContactVelocity step,float factor) {
    const bool unscale=((1u<<((subtype-0x10u)&31u))&0x1eu)==0 &&
        ((behavior&0x1cu)==0xcu || (behavior&3u)==1u || (behavior&3u)==2u);
    return unscale?ContactVelocity{float(static_cast<long double>(factor)*step.x),
                                  float(static_cast<long double>(factor)*step.z)}:
        ContactVelocity{float(static_cast<long double>(proposed.x)-current.x),
                        float(static_cast<long double>(proposed.z)-current.z)};
}
struct NativeActorAmmoAttribute { std::uint32_t tag=0,flags=0; };
// Outcomes at the boundaries of original441d61..441e24, after the common
// graph query has selected exactly one contact. Do not run this on every Ammo
// candidate or on a separate post-movement proximity list.
enum class NativeActorAmmoAction : std::uint32_t {
    PhysicalResponse=0,ContinueInteraction=1,ClearMovement=2,ImpactAndAbortActor=3
};
struct NativeActorAmmoDispatch {
    NativeActorAmmoAction action=NativeActorAmmoAction::PhysicalResponse;
    std::uint32_t sourceFlags=0;
    size_t attribute=SIZE_MAX;
};
inline NativeActorAmmoDispatch nativeActorAmmoDispatch(
        const NativeAmmoContactObject& source,const NativeAmmoContactObject& selected,
        std::uint32_t sourceFlags,std::span<const NativeActorAmmoAttribute> attributes) {
    NativeActorAmmoDispatch result;result.sourceFlags=sourceFlags;
    if(source.kind!=3)return result;
    const auto shift=(source.subtype-0x10u)&31u;
    if(((std::uint32_t(1)<<shift)&0x1fu)!=0 || selected.kind!=3 || selected.subtype!=0x10){
        result.action=NativeActorAmmoAction::ContinueInteraction;return result;
    }
    if((sourceFlags&0x800000u)==0)result.sourceFlags|=0x200u;
    for(size_t n=0;n<attributes.size();++n)if(attributes[n].tag==0xb){
        result.attribute=n;
        // This late check is separate from41a470's candidate admission. On-hit
        // and On-object-hit bits do not gate actor-moving-into-Ammo impact.
        if((attributes[n].flags&4u)!=0 && nativeAmmoOwnedBy(selected,source)){
            result.action=NativeActorAmmoAction::ClearMovement;return result;
        }
        result.action=NativeActorAmmoAction::ImpactAndAbortActor;return result;
    }
    return result;
}
}
