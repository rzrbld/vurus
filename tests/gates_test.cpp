#include "game/gates.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace vyruz;
int main() {
    GateConfig c{GateActive|GateOpened|GateClosing|GateSensor|GateDestroyKey,100,3,{5}};
    auto s=initializeGate(c);assert(s.sequence.value==5&&s.remainingMs==100);
    auto o=advanceGate(c,s,{100,false,false,false});assert(!o.blocking&&!o.restartAnimation&&s.remainingMs==100);
    o=advanceGate(c,s,{50,true,false,false});assert(s.remainingMs==50&&!o.blocking);
    o=advanceGate(c,s,{50,true,false,true});assert(!o.restartAnimation&&!o.blocking);
    o=advanceGate(c,s,{50,true,false,false});assert(o.restartAnimation&&o.blocking&&s.sequence.value==6);
    o=advanceGate(c,s,{100,false,true,false});assert(!o.restartAnimation&&o.blocking&&!o.consumeKeyId);
    o=advanceGate(c,s,{100,true,false,false});assert(o.blocking&&(s.flags&GateStayClosed)&&!o.restartAnimation);
    o=advanceGate(c,s,{100,true,true,false});assert(o.restartAnimation&&o.blocking&&o.consumeKeyId==3&&s.keyId==0&&s.sequence.value==5);
    o=advanceGate(c,s,{100,false,true,false});assert(o.blocking);
    o=advanceGate(c,s,{100,true,true,false});assert(!o.blocking&&(s.flags&GateOpened));
    c.flags|=GateKeyAlwaysNeeded;s=initializeGate(c);
    o=advanceGate(c,s,{100,true,true,false});assert(o.consumeKeyId==3&&s.keyId==3&&!o.blocking);
    c.flags=GateActive|GateOpened|GateStayOpened;s=initializeGate(c);o=advanceGate(c,s,{100,true,false,false});assert(!o.blocking&&!o.restartAnimation);
    c.flags=GateSensor;s=initializeGate(c);o=advanceGate(c,s,{100,true,true,false});assert(o.blocking&&!o.restartAnimation);
    c.flags=GateActive;s=initializeGate(c);o=advanceGate(c,s,{100,true,false,false});assert(o.restartAnimation&&o.blocking);
    // Automatic opening must not spend a nearby actor's key. Native lookup
    // only happens for Sensor here, but still happens later while opened.
    c.flags=GateActive|GateDestroyKey;s=initializeGate(c);
    o=advanceGate(c,s,{100,true,true,false});
    assert(o.restartAnimation&&o.blocking&&!o.keyAccepted&&!o.consumeKeyId&&s.keyId==3);
    o=advanceGate(c,s,{100,true,true,false});
    assert(!o.blocking&&!o.keyAccepted&&!o.consumeKeyId&&s.keyId==3);
    o=advanceGate(c,s,{100,true,true,false});
    assert(!o.blocking&&o.keyAccepted&&o.consumeKeyId==3&&s.keyId==0);
    ObjectPlacement object;object.sprite.sequence={5};TextDocument doc;std::string error;
    assert(parseContentText("TYPE Gate [\nOpened = 1\nActive = 1\nDelay = 5000\nId = 3\nSensor = 1\nDestroy-key = 1\n]\n","test",doc,error));object.properties=doc.nodes;
    auto parsed=makeGateConfig(object);assert(parsed&&parsed->delayMs==5000&&parsed->keyId==3&&parsed->baseSequence.value==5&&(parsed->flags&GateSensor));
}
