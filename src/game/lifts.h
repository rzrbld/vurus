#pragma once
#include "game/content.h"
#include "game/properties.h"
#include <algorithm>
#include <cmath>
namespace vyruz {
enum LiftFlag : uint32_t { LiftActive=1,LiftPresent=2,LiftGoingUp=4,LiftStayUp=8,
    LiftStayDown=16,LiftFullLoad=32,LiftKeyAlwaysNeeded=64,LiftDestroyKey=128,LiftNeedsCode=256 };
struct LiftConfig { uint32_t flags=0;float delayMs=0;int keyId=0;SequenceId baseSequence{}; };
struct LiftState { uint32_t flags=0;float remainingMs=0;int keyId=0;SequenceId sequence{};bool passable=false; };
struct LiftInput { float dtMs=0;bool animationFinished=false,nearbyAuthorized=false,occupied=false;int usageCount=0; };
struct LiftOutput { bool restartAnimation=false,departureReady=false;int consumeKeyId=0; };
inline std::optional<LiftConfig> makeLiftConfig(const ObjectPlacement& object) {
    const auto* type=attribute(object.properties,"lift");if(!type)return {};
    LiftConfig c;c.baseSequence=object.sprite.sequence;
    c.delayMs=std::max(0.f,propertyNumber(type->children,"Delay"));c.keyId=static_cast<int>(propertyNumber(type->children,"Id"));
    for(const auto& [name,flag]:{std::pair{"Active",LiftActive},{"Present",LiftPresent},{"Going-Up",LiftGoingUp},
        {"Stay-Up",LiftStayUp},{"Stay-Down",LiftStayDown},{"Full-load",LiftFullLoad},
        {"Key-always-needed",LiftKeyAlwaysNeeded},{"Destroy-key",LiftDestroyKey},{"Needs-code",LiftNeedsCode}})
        if(propertyBool(type->children,name))c.flags|=flag;
    return c;
}
inline LiftState initializeLift(const LiftConfig& c) {
    return {c.flags,c.delayMs,c.keyId,{c.baseSequence.value+((c.flags&LiftPresent)?0u:1u)},bool(c.flags&LiftPresent)};
}
// Original 442bf0. Authorization and passability are distinct from Present.
inline LiftOutput advanceLift(const LiftConfig& c,LiftState& s,const LiftInput& in) {
    LiftOutput out;
    if(!(s.flags&LiftActive)||!std::isfinite(in.dtMs)||in.dtMs<0)return out;
    if(s.remainingMs==c.delayMs&&!in.animationFinished)return out;
    auto authorize=[&] {
        if(s.keyId&&!in.nearbyAuthorized)return false;
        s.passable=true;
        if(s.keyId) {
            if(s.flags&LiftDestroyKey)out.consumeKeyId=s.keyId;
            if(!(s.flags&LiftKeyAlwaysNeeded))s.keyId=0;
        }
        return true;
    };
    auto restart=[&](bool up) {
        s.remainingMs=c.delayMs;s.sequence.value=c.baseSequence.value+(up?0u:1u);out.restartAnimation=true;
    };
    if(!(s.flags&LiftPresent)) {
        if(s.flags&LiftGoingUp) {
            authorize();s.flags|=LiftPresent;s.remainingMs=std::max(0.f,s.remainingMs-in.dtMs);return out;
        }
        if(s.flags&LiftStayDown)return out;
        if(in.dtMs<s.remainingMs){s.remainingMs-=in.dtMs;return out;}
        restart(true);s.flags|=LiftGoingUp;return out;
    }
    const bool authorized=authorize();
    if(in.dtMs<s.remainingMs){s.remainingMs-=in.dtMs;return out;}
    if(in.usageCount<1)return out;
    if(!in.occupied) {
        if(s.flags&LiftStayUp)return out;
        s.flags&=~(LiftPresent|LiftGoingUp);s.passable=false;restart(false);return out;
    }
    out.departureReady=authorized;return out;
}
}
