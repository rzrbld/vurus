#ifdef NDEBUG
#undef NDEBUG
#endif
#include "game/spider_turn_native.h"
#include "game/native_random.h"
#include <cassert>
#include <tuple>
using namespace vyruz;
namespace {
auto fields(const EnemyTurnState& s) {
    return std::tuple{s.bodyPose,s.bodyDesiredPose,s.bodyPreviousPose,s.bodyPeriodMs,
        s.bodyRemainingMs,s.logicalHeading,s.orthogonalHeading,s.turnCommand,s.fastTurn};
}
}
int main() {
    const EnemyTurnConfig config{0x1f,false,true,42,0};
    // Native initialization retains an authored non-cardinal ring heading;
    // rendering and forward admission use its separately floored visible pose.
    auto state=initializeSpiderTurn(config,3);
    assert(state.bodyPose==1 && state.logicalHeading==3 && state.bodyDesiredPose==1);
    assert(spiderTurnAllowsForward(state) && !spiderTurnAllowsFire(state,3));

    // AI route turns have a single step and the native half-turn tie.
    state=initializeSpiderTurn(config,0);
    beginSpiderTurnUpdate(state);
    advanceSpiderRouteTurn(config,state,12,16,0,35);
    assert(state.bodyPose==24 && state.bodyPreviousPose==1 && state.bodyDesiredPose==13);
    assert(state.bodyRemainingMs==26 && state.logicalHeading==18 && state.turnCommand==205);
    assert(!spiderTurnAllowsForward(state) && !spiderTurnAllowsFire(state,18));

    // A not-due route pass leaves the cleared command zero and the desired pose
    // retained. The subsequent stationary idle pass uses that SAME countdown.
    beginSpiderTurnUpdate(state);
    advanceSpiderRouteTurn(config,state,6,16,0,35);
    assert(state.bodyRemainingMs==10 && state.bodyDesiredPose==13 && state.turnCommand==0);
    SpiderIdleTurnAdmission admission{true,10,20,10,20,0};
    assert(spiderIdleTurnAdmitted(config,state,admission));
    int draws=0;
    advanceSpiderIdleTurn(config,state,16,[&](int,int){++draws;return 1;});
    assert(draws==0 && state.bodyPose==23 && state.bodyDesiredPose==13);
    assert(state.bodyRemainingMs==36 && state.logicalHeading==18);

    // Original RNG: aligned frames draw once, the 75 event draws an inclusive
    // target index, and mid-turn frames draw nothing. A random endpoint may be
    // an intermediate pose; idle turning must not unconditionally snap it.
    state=initializeSpiderTurn(config,12);state.bodyRemainingMs=100;
    NativeRandomState random{1,2118850534u};
    advanceSpiderIdleTurn(config,state,16,[&](int low,int high){++draws;return random.inclusive(low,high);});
    assert(draws==2 && state.bodyPose==13 && state.bodyDesiredPose==14);
    assert(state.bodyRemainingMs==84 && state.logicalHeading==12 && random.counter==2118850536u);
    advanceSpiderIdleTurn(config,state,100,[&](int,int){assert(false);return 1;});
    assert(state.bodyPose==14 && state.bodyDesiredPose==14 && state.bodyRemainingMs==26);
    assert(state.logicalHeading==12 && !spiderTurnAllowsForward(state));
    beginSpiderTurnUpdate(state);
    advanceSpiderIdleTurn(config,state,16,[](int low,int high){assert(low==1 && high==150);return 1;});
    assert(state.bodyPose==14 && state.bodyDesiredPose==14 && state.logicalHeading==12);
    assert(state.turnCommand==0 && !spiderTurnAllowsFire(state,12));

    // Actual movement history and final command admission are independent of
    // speed, active routes, or being a Fixed/Random/Patrol base behavior.
    state=initializeSpiderTurn(config,0);
    auto input=admission;
    input.currentX+=.001f;assert(!spiderIdleTurnAdmitted(config,state,input));input=admission;
    input.currentZ+=.001f;assert(!spiderIdleTurnAdmitted(config,state,input));input=admission;
    input.seekerOrTracker=false;assert(!spiderIdleTurnAdmitted(config,state,input));input=admission;
    input.movementCommand=200;assert(!spiderIdleTurnAdmitted(config,state,input));
    state.turnCommand=203;assert(!spiderIdleTurnAdmitted(config,state,admission));state.turnCommand=0;
    auto cannotTurn=config;cannotTurn.canTurnOnPlace=false;
    assert(!spiderIdleTurnAdmitted(cannotTurn,state,admission));

    // Copying shared actor/RNG state preserves future route/idle continuation.
    auto restored=state;auto rngRestored=random;
    for(int tick=0;tick<200;++tick) {
        for(auto* s:{&state,&restored}) {
            beginSpiderTurnUpdate(*s);
            if(tick%3==0)advanceSpiderRouteTurn(config,*s,(tick/20%4)*6,16,0,35);
        }
        advanceSpiderIdleTurn(config,state,16,[&](int l,int h){return random.inclusive(l,h);});
        advanceSpiderIdleTurn(config,restored,16,[&](int l,int h){return rngRestored.inclusive(l,h);});
        assert(fields(state)==fields(restored));
        assert(random.seed==rngRestored.seed && random.counter==rngRestored.counter);
    }
}
