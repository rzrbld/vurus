#pragma once
#include "game/content.h"
namespace vyruz {
enum GateFlag : uint32_t { GateActive=1, GateOpened=2, GateClosing=4,
    GateStayOpened=8, GateStayClosed=16, GateSensor=32,
    GateKeyAlwaysNeeded=64, GateDestroyKey=128, GateNeedsCode=256 };
struct GateConfig { uint32_t flags=0;float delayMs=0;int keyId=0;SequenceId baseSequence{}; };
struct GateState { uint32_t flags=0;float remainingMs=0;int keyId=0;SequenceId sequence{}; };
struct GateInput {
    float dtMs=0;
    bool animationFinished=false;
    bool nearbyAuthorized=false; // native +/-2 tile search, visible eligible actor
    bool occupied=false;         // exact overlap supplied by the world
};
struct GateOutput {
    bool blocking=true;
    bool restartAnimation=false; // select state.sequence and begin at subframe 2
    int consumeKeyId=0;          // remove all matching carried objects/parts
    bool keyAccepted=false;
};
std::optional<GateConfig> makeGateConfig(const ObjectPlacement&);
// Renderer initializes this sequence on its LAST authored subframe.
GateState initializeGate(const GateConfig&);
GateOutput advanceGate(const GateConfig&,GateState&,const GateInput&);
} // namespace vyruz
