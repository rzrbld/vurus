#include "game/lifts.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
using namespace vyruz;
int main() {
    LiftConfig c{LiftActive|LiftPresent|LiftGoingUp|LiftDestroyKey,100,3,{5}};
    auto s=initializeLift(c);
    assert(s.passable&&s.sequence.value==5&&s.remainingMs==100&&s.keyId==3);
    // Startup and each fresh transition wait for the native animation end.
    auto o=advanceLift(c,s,{100,false,true,true,1});
    assert(!o.departureReady&&!o.consumeKeyId&&s.remainingMs==100&&s.keyId==3);
    // Authorization is before expiry, occupancy and usage admission.
    o=advanceLift(c,s,{40,true,true,false,0});
    assert(o.consumeKeyId==3&&s.keyId==0&&s.remainingMs==60&&s.passable);
    o=advanceLift(c,s,{60,true,false,false,0});
    assert(!o.restartAnimation&&!o.departureReady&&s.remainingMs==60);
    o=advanceLift(c,s,{60,true,false,false,1});
    assert(o.restartAnimation&&!s.passable&&s.sequence.value==6&&s.remainingMs==100);
    assert(!(s.flags&(LiftPresent|LiftGoingUp)));
    o=advanceLift(c,s,{100,false,true,true,1});
    assert(!o.restartAnimation&&!s.passable&&s.remainingMs==100);
    o=advanceLift(c,s,{40,true,true,true,1});
    assert(s.remainingMs==60&&!o.restartAnimation&&!s.passable);
    o=advanceLift(c,s,{60,true,false,false,1});
    assert(o.restartAnimation&&(s.flags&LiftGoingUp)&&!(s.flags&LiftPresent));
    assert(s.sequence.value==5&&s.remainingMs==100&&!s.passable);
    o=advanceLift(c,s,{100,false,false,true,1});assert(!s.passable&&!(s.flags&LiftPresent));
    o=advanceLift(c,s,{40,true,false,true,1});
    assert(s.passable&&(s.flags&LiftPresent)&&s.remainingMs==60&&!o.departureReady);
    o=advanceLift(c,s,{60,true,false,true,1});assert(o.departureReady&&s.remainingMs==60);
    // Native preserves an expired Present remainder; a later shorter dt waits.
    o=advanceLift(c,s,{10,true,false,true,1});assert(!o.departureReady&&s.remainingMs==50);

    // Unauthorized arrival is Present but still blocking; an already-passable
    // lift does not regain blocking merely because its key holder leaves.
    c.flags=LiftActive|LiftGoingUp|LiftDestroyKey;s=initializeLift(c);
    o=advanceLift(c,s,{100,true,false,true,1});
    assert((s.flags&LiftPresent)&&!s.passable&&s.remainingMs==0&&!o.departureReady&&s.keyId==3);
    o=advanceLift(c,s,{10,true,false,true,1});assert(!o.departureReady&&!s.passable);
    o=advanceLift(c,s,{10,true,true,true,1});assert(o.departureReady&&o.consumeKeyId==3&&s.passable&&s.keyId==0);
    c.flags=LiftActive|LiftPresent|LiftKeyAlwaysNeeded|LiftDestroyKey;s=initializeLift(c);
    o=advanceLift(c,s,{100,true,true,true,1});assert(o.departureReady&&o.consumeKeyId==3&&s.keyId==3);
    o=advanceLift(c,s,{100,true,false,true,1});assert(!o.departureReady&&s.passable&&s.keyId==3);
    c.flags=LiftActive|LiftPresent;s=initializeLift(c);
    o=advanceLift(c,s,{1,true,true,false,1});assert(!o.consumeKeyId&&s.keyId==0);

    c.flags=LiftActive|LiftStayDown;s=initializeLift(c);
    o=advanceLift(c,s,{100,true,true,true,1});assert(!o.restartAnimation&&!s.passable&&s.remainingMs==100&&s.keyId==3);
    c.flags=LiftActive|LiftPresent|LiftStayUp;s=initializeLift(c);
    o=advanceLift(c,s,{100,true,false,false,1});assert(!o.restartAnimation&&s.passable);
    // Full-load has no occupancy-count or delay meaning in this controller.
    c.flags=LiftActive|LiftPresent|LiftFullLoad;c.keyId=0;s=initializeLift(c);
    o=advanceLift(c,s,{100,true,false,true,1});assert(o.departureReady);
    c.flags=LiftPresent|LiftDestroyKey;c.keyId=3;s=initializeLift(c);
    o=advanceLift(c,s,{100,true,true,true,1});assert(!o.departureReady&&!o.consumeKeyId&&s.keyId==3&&s.passable);
    c.flags=LiftActive|LiftPresent;s=initializeLift(c);
    o=advanceLift(c,s,{-1,true,true,true,1});assert(s.remainingMs==100&&s.keyId==3);
    o=advanceLift(c,s,{std::numeric_limits<float>::quiet_NaN(),true,true,true,1});assert(s.remainingMs==100&&s.keyId==3);

    ObjectPlacement object;object.sprite.sequence={5};TextDocument doc;std::string error;
    assert(parseContentText("TYPE Lift [\nActive = 1\nPresent = 1\nGoing-Up = 1\nStay-Up = 1\nStay-Down = 1\nFull-load = 1\nKey-always-needed = 1\nDestroy-key = 1\nNeeds-code = 1\nDelay = 5000\nId = 3\n]\n","test",doc,error));
    object.properties=doc.nodes;auto parsed=makeLiftConfig(object);
    assert(parsed&&parsed->flags==511&&parsed->delayMs==5000&&parsed->keyId==3&&parsed->baseSequence.value==5);
    object.properties.clear();assert(!makeLiftConfig(object));
}
