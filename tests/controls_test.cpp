#include "game/controls.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>

namespace { void near(float a,float b,float tolerance=1e-5f) { assert(std::abs(a-b)<tolerance); } }
int main() {
    using namespace vyruz;
    constexpr float dt=1.f/60;
    // Original table is in internal x/z; every authored heading must round-trip
    // through the actual isometric projection, including non-cardinal ones.
    for(int d=0;d<24;++d) {
        const auto h=nativeHeading(d), s=nativeToSurface(h.x,h.y);
        assert(nativeDirectionFromVector(s.x,s.y)==d);
        const auto v=surfaceToNative(s.x,s.y); near(v.x,h.x); near(v.y,h.y);
        PlayerMotion directionMotion;directionMotion.direction=d;
        Displacement travel{};
        for(int step=0;step<60;++step)travel=advanceNativePlayerMotion(directionMotion,{1,0},dt);
        assert(travel.x*s.x+travel.y*s.y>0);
        near(travel.x*s.y-travel.y*s.x,0,.0001f);
        const auto speed=directionMotion.magnitude;
        advanceNativePlayerMotion(directionMotion,{},dt);assert(directionMotion.magnitude>0&&directionMotion.magnitude<speed);

    }
    assert(nativeDirectionFromVector(0,0)==24);
    assert(nativeDirectionFromVector(1,0)==9 && nativeDirectionFromVector(0,1)==3);
    assert(nativeDirectionFromVector(-1,0)==21 && nativeDirectionFromVector(0,-1)==15);
    PlayerMotion m; MovementCommand aim; aim.aimActive=true; aim.aimX=200;
    for(int i=0;i<60;++i) { auto d=advanceNativePlayerMotion(m,aim,dt); assert(d.x==0&&d.y==0); }
    assert(m.direction==9);
    PlayerMotion turning;turning.direction=9;
    MovementCommand north;north.aimActive=true;north.aimY=-100;
    advancePlayerMotion(turning,north,.035f);assert(turning.direction==9);
    advancePlayerMotion(turning,north,.0011f);assert(turning.direction==10);
    assert(turning.velocityX==0 && turning.velocityZ==0);
    // Native Reverse swaps turn decisions, with no turn at the aligned or
    // antipodal heading. A fixed pointer must never produce heading jitter.
    PlayerMotionConfig reversedTurns;reversedTurns.reverseTurns=true;
    for(int target=0;target<24;++target) {
        const auto h=nativeHeading(target),s=nativeToSurface(h.x,h.y);
        MovementCommand pointer;pointer.aimActive=true;pointer.aimX=s.x;pointer.aimY=s.y;
        for(int initial=0;initial<24;++initial) {
            PlayerMotion reversed,normal;reversed.direction=normal.direction=initial;
            advancePlayerMotion(reversed,pointer,.04f,reversedTurns);
            advancePlayerMotion(normal,pointer,.04f);
            const int delta=(target-initial+24)%24;
            if(delta!=0 && delta!=12) {
                const int step=delta<12?1:-1;
                assert(reversed.direction==(initial-step+24)%24);
                assert(normal.direction==(initial+step+24)%24);
            } else assert(reversed.direction==initial);
            for(int tick=0;tick<60;++tick)advancePlayerMotion(reversed,pointer,dt,reversedTurns);
            const int resting=initial==target?target:(target+12)%24;
            assert(reversed.direction==resting);
            for(int tick=0;tick<120;++tick) {
                advancePlayerMotion(reversed,pointer,dt,reversedTurns);
                assert(reversed.direction==resting);
            }
            // Removing the Surprise restores ordinary aiming, without needing
            // a pointer change or reset of movement state.
            for(int tick=0;tick<60;++tick)advancePlayerMotion(reversed,pointer,dt);
            assert(reversed.direction==target);
            // Multiple turn steps in one native update cannot cross the stop.
            PlayerMotion largeStep;largeStep.direction=initial;
            advanceNativePlayerMotion(largeStep,pointer,1.f,reversedTurns);
            assert(largeStep.direction==resting);
        }
    }
    // At rest the first thrust step is below both native stop thresholds.
    auto d=advanceNativePlayerMotion(m,{1,0},dt); near(m.thrust,1); assert(m.magnitude==0&&d.x==0);
    d=advanceNativePlayerMotion(m,{1,0},dt); near(m.thrust,2); assert(d.x==0);
    d=advanceNativePlayerMotion(m,{1,0},dt); near(m.thrust,3);
    near(m.velocityX,.0707107f*3/15*.972f); near(m.velocityZ,-m.velocityX);
    assert(d.x>0); near(d.y,0);
    const float before=m.velocityX;
    advanceNativePlayerMotion(m,{},dt); near(m.thrust,2.5f); near(m.velocityX,before*.972f);
    // Release decays thrust but never adds it to the velocity.
    PlayerMotion coast; coast.direction=9; coast.velocityX=2; coast.velocityZ=-1;
    coast.magnitude=std::sqrt(5.f);coast.thrust=40;
    advanceNativePlayerMotion(coast,{},dt); near(coast.velocityX,1.944f); near(coast.thrust,39.5f);
    PlayerMotion reverse; reverse.direction=9;
    for(int i=0;i<30;++i) advanceNativePlayerMotion(reverse,{-1,0},dt);
    near(reverse.thrust,-10); assert(reverse.velocityX<0&&reverse.velocityZ>0);
    PlayerMotion held;held.direction=9;
    for(int i=0;i<180;++i) advanceNativePlayerMotion(held,{1,0},dt);
    near(held.thrust,45);assert(held.magnitude>4); // no hard velocity clamp
    for(int i=0;i<1000;++i) advanceNativePlayerMotion(held,{},dt);
    assert(held.velocityX==0&&held.velocityZ==0&&held.thrust==0);
    // Both components must be small to stop; one small component survives.
    PlayerMotion one;one.velocityX=.001f;one.velocityZ=1;one.magnitude=1;
    advanceNativePlayerMotion(one,{},dt);assert(one.velocityX>0&&one.velocityZ>0);
    // Gate tests use PREVIOUS magnitude, before grip, and equality blocks thrust.
    PlayerMotionConfig c;c.thresholdX=c.thresholdZ=0;c.grip=.5f;
    PlayerMotion gated;gated.direction=9;gated.velocityX=4.5f;gated.magnitude=4.5f;
    advanceNativePlayerMotion(gated,{1,0},dt,c);near(gated.velocityX,4.5f*.5f*.972f);near(gated.velocityZ,0);
    // Strafe is independent of thrust/weight/gate and diagonal is not normalized.
    PlayerMotion side;side.direction=9;side.magnitude=100;
    d=advanceNativePlayerMotion(side,{0,1},dt);near(side.velocityX,.0707107f*2*.972f);
    near(side.velocityZ,side.velocityX);near(d.x,0);assert(d.y>0);
    PlayerMotion forward,diagonal;forward.direction=diagonal.direction=9;
    forward.thrust=diagonal.thrust=20;
    advanceNativePlayerMotion(forward,{1,0},dt,c);advanceNativePlayerMotion(diagonal,{1,1},dt,c);
    near(diagonal.velocityX-forward.velocityX,.0707107f*2*.972f);
    near(diagonal.velocityZ-forward.velocityZ,.0707107f*2*.972f);
    // Time scales displacement, not acceleration/friction. Native14ms and port
    // 60Hz traces have identical state with disabled time-scaled stop threshold.
    PlayerMotion a,b;a.direction=b.direction=9;
    float distanceA=0,distanceB=0;
    for(int i=0;i<120;++i) {distanceA+=advanceNativePlayerMotion(a,{1,0},.014f,c).x;distanceB+=advanceNativePlayerMotion(b,{1,0},dt,c).x;}
    near(a.velocityX,b.velocityX);near(a.thrust,b.thrust);near(distanceB,distanceA*dt/.014f,.001f);
    const auto saved=a;
    for(float bad:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        d=advanceNativePlayerMotion(a,{1,0},bad);assert(d.x==0&&d.y==0);near(a.thrust,saved.thrust);near(a.velocityX,saved.velocityX);
    }
    c.weight=0;advanceNativePlayerMotion(a,{1,0},dt,c);near(a.thrust,saved.thrust);
    // Live path uses native speed units and projection, not a200 surface clamp.
    for(int direction=0;direction<24;++direction) {
        PlayerMotion restored;restored.direction=direction;
        Displacement step;
        for(int tick=0;tick<180;++tick)step=advancePlayerMotion(restored,{1,0},dt);
        assert(restored.magnitude>4.3f && restored.magnitude<4.8f);
        const auto h=nativeHeading(direction), projected=nativeToSurface(h.x,h.y);
        near(step.x*projected.y-step.y*projected.x,0,.0001f);
        const auto expected=nativeToSurface(restored.velocityX,restored.velocityZ);
        near(step.x,expected.x*dt/.014f);near(step.y,expected.y*dt/.014f);
        MovementCommand mouse;mouse.aimActive=true;mouse.aimX=100;mouse.aimY=30;
        PlayerMotion idle;
        for(int tick=0;tick<60;++tick){step=advancePlayerMotion(idle,mouse,dt);assert(step.x==0&&step.y==0);}
        PlayerMotion close,far;close.direction=far.direction=direction;
        mouse.forward=1;
        for(int tick=0;tick<120;++tick){advancePlayerMotion(close,mouse,dt);auto distant=mouse;distant.aimX*=10;distant.aimY*=10;advancePlayerMotion(far,distant,dt);}
        near(close.velocityX,far.velocityX);near(close.velocityZ,far.velocityZ);
    }
    return 0;
}
