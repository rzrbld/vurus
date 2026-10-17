#include "game/gates.h"
#include "game/properties.h"
#include <algorithm>
#include <cmath>
namespace vyruz {
std::optional<GateConfig> makeGateConfig(const ObjectPlacement& o) {
    const auto* gate=attribute(o.properties,"gate");if(!gate)return {};
    GateConfig c;c.baseSequence=o.sprite.sequence;
    c.delayMs=std::max(0.f,propertyNumber(gate->children,"Delay"));
    c.keyId=static_cast<int>(propertyNumber(gate->children,"Id"));
    for(const auto& [name,flag]:{std::pair{"Active",GateActive},{"Opened",GateOpened},{"Closing",GateClosing},
        {"Stay-Opened",GateStayOpened},{"Stay-Closed",GateStayClosed},{"Sensor",GateSensor},
        {"Key-always-needed",GateKeyAlwaysNeeded},{"Destroy-key",GateDestroyKey},{"Needs-Code",GateNeedsCode}})
        if(propertyBool(gate->children,name))c.flags|=flag;
    return c;
}
GateState initializeGate(const GateConfig& c) {
    return {c.flags,c.delayMs,c.keyId,{c.baseSequence.value+((c.flags&GateOpened)?0u:1u)}};
}
GateOutput advanceGate(const GateConfig& c,GateState& s,const GateInput& in) {
    GateOutput out;out.blocking=(s.flags&GateOpened)==0;
    if(!(s.flags&GateActive)||!std::isfinite(in.dtMs)||in.dtMs<0)return out;
    if(s.remainingMs==c.delayMs&&!in.animationFinished)return out;
    auto consume=[&] {
        if(!in.nearbyAuthorized||s.keyId==0)return;
        out.keyAccepted=true;
        if(s.flags&GateDestroyKey)out.consumeKeyId=s.keyId;
        if(!(s.flags&GateKeyAlwaysNeeded))s.keyId=0;
    };
    auto restart=[&](bool opening) {
        s.remainingMs=c.delayMs;s.sequence.value=c.baseSequence.value+(opening?0u:1u);
        out.restartAnimation=true;
    };
    if(s.flags&GateOpened) {
        if(in.dtMs<s.remainingMs){s.remainingMs-=in.dtMs;return out;}
        // Native keeps the expired countdown value until a transition resets it.
        if(in.nearbyAuthorized){consume();return out;}
        if(in.occupied||(s.flags&GateStayOpened))return out;
        s.flags&=~(GateOpened|GateClosing);out.blocking=true;restart(false);return out;
    }
    if(s.flags&GateClosing) {
        s.flags|=GateOpened;out.blocking=false;s.remainingMs=std::max(0.f,s.remainingMs-in.dtMs);return out;
    }
    if(!(s.flags&GateSensor)||in.nearbyAuthorized) {
        s.remainingMs=0;s.flags&=~GateStayClosed;
        // 443620 skips the actor lookup for automatic opening. A nearby key
        // holder is only accepted here when the gate actually uses its sensor.
        if(s.flags&GateSensor)consume();
    } else s.flags|=GateStayClosed;
    if(!(s.flags&GateStayClosed)) {
        if(in.dtMs<s.remainingMs){s.remainingMs-=in.dtMs;return out;}
        s.flags|=GateClosing;restart(true);
    }
    return out;
}
} // namespace vyruz
