#include "game/enemy_motion.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <bit>
#include <cstdint>
#include <limits>
using namespace vyruz;
namespace {
bool near(float a,float b,float tolerance=1e-6f){return std::abs(a-b)<tolerance;}
EnemyMotionCommand forward(){EnemyMotionCommand command;command.heading=6;command.throttle=EnemyThrottle::Forward;return command;}
}
int main() {
    // 42cfa0 performs one pose step even when elapsed time spans many timeouts.
    {int pose=0;float timer=0;advanceSpiderBodyPose(pose,timer,12,16,50);
     assert(pose==23 && near(timer,34));
     advanceSpiderBodyPose(pose,timer,12,10,50);assert(pose==23 && near(timer,24));
     advanceSpiderBodyPose(pose,timer,12,200,50);assert(pose==22 && timer==0);
     pose=12;timer=0;advanceSpiderBodyPose(pose,timer,0,16,50);assert(pose==13);
     pose=6;timer=0;advanceSpiderBodyPose(pose,timer,6,16,50);assert(pose==6 && near(timer,34));}

    // Resolved original Tank-Empty: Thrust500, Weight100, friction.97,
    // Speed-max35. Native heading6 is table9=(.1,0).
    EnemyMotionConfig tank{-500,500,5,5,35,100,1,.97f,.97f,.01f,.01f,true,false,false};
    EnemyMotionState state{{},500,0,false};
    auto command=forward();
    auto delta=advanceEnemyMotion(state,command,.014f,tank);
    assert(near(state.velocity.x,.485f) && near(state.velocity.z,0));
    assert(near(delta.x,.43379718f) && near(delta.y,.21689859f));
    assert(near(state.thrust,500) && near(state.magnitude,.485f));

    // Speed-max is a prior-magnitude propulsion gate, never a velocity cap.
    state={{10,0},500,10,false};advanceEnemyMotion(state,command,.014f,tank);
    assert(near(state.velocity.x,9.7f));
    // The native cached magnitude can differ from collision-modified velocity.
    state={{0,0},500,10,false};advanceEnemyMotion(state,command,.014f,tank);
    assert(near(state.velocity.x,0));
    state={{10,0},500,0,false};advanceEnemyMotion(state,command,.014f,tank);
    assert(near(state.velocity.x,10.185f,2e-6f));

    // Releasing intent decays thrust while keeping inertial momentum.
    command.throttle=EnemyThrottle::Neutral;
    state={{3,2},20,std::sqrt(13.f),false};advanceEnemyMotion(state,command,.014f,tank);
    assert(near(state.velocity.x,2.91f) && near(state.velocity.z,1.94f));
    assert(near(state.thrust,15));
    command.throttle=EnemyThrottle::Reverse;
    state={{},-100,0,false};advanceEnemyMotion(state,command,.014f,tank);
    assert(near(state.thrust,-115) && near(state.velocity.x,-.11155f));

    // An impact disables forward throttle but does not erase the impulse.
    command=forward();state={{3,0},20,3,true};
    advanceEnemyMotion(state,command,.014f,tank);
    assert(near(state.velocity.x,2.91f) && state.impactLatch && near(state.thrust,15));
    state={{1,0},20,1,true};advanceEnemyMotion(state,command,.014f,tank);
    assert(near(state.velocity.x,.97f) && !state.impactLatch);
    // Launched8000 is separate: authored inertia keeps it suppressing throttle
    // even below the impact100 release threshold.
    tank.launched=true;state={{.5f,0},20,.5f,false};
    advanceEnemyMotion(state,command,.014f,tank);
    assert(near(state.velocity.x,.485f) && near(state.thrust,15));tank.launched=false;

    // Walker's no-inertia and weight bypass produce1.5 per native call;
    // impulse override still retains its velocity until the impact latch ends.
    EnemyMotionConfig walker{0,15,15,4,15,90,0,1,1,0,0,false,false,true};
    state={{50,50},15,70,false};advanceEnemyMotion(state,command,.014f,walker);
    assert(near(state.velocity.x,1.5f) && near(state.velocity.z,0));
    state={{2,1},15,std::sqrt(5.f),true};advanceEnemyMotion(state,command,.014f,walker);
    assert(near(state.velocity.x,2) && near(state.velocity.z,1));

    // Original Spider acceleration.04 ignores its authored Weight60.
    EnemyMotionConfig spider{0,4,.04f,3.5f,3.5f,60,1,1,1,0,0,true,false,true};
    state={};advanceEnemyMotion(state,command,.014f,spider);
    assert(near(state.thrust,.04f) && near(state.velocity.x,.004f));

    // Both components must satisfy their dt-scaled threshold to stop.
    auto stop=tank;stop.thresholdX=stop.thresholdZ=.1f;
    command.throttle=EnemyThrottle::Neutral;
    state={{.15f,.15f},0,.212132f,false};advanceEnemyMotion(state,command,.028f,stop);
    assert(state.velocity.x==0 && state.velocity.z==0);
    state={{.25f,.15f},0,.291548f,false};advanceEnemyMotion(state,command,.028f,stop);
    assert(state.velocity.x>0 && state.velocity.z>0);

    // Route42cc60 intentionally uses absolute projection: a point before the
    // segment's start gets a positive along-axis correction, not zero.
    const auto perpendicular=enemyRouteCorrection({5,10},{0,0},{10,0},false);
    assert(near(perpendicular.x,0) && near(perpendicular.z,-.10000000894069672f,1e-8f));
    const auto before=enemyRouteCorrection({-5,0},{0,0},{10,0},false);
    assert(near(before.x,.10000000894069672f,1e-8f) && near(before.z,0));
    const auto small=enemyRouteCorrection({5,10},{0,0},{10,0},true);
    assert(near(small.z,perpendicular.z*.01f,1e-12f));
    assert(enemyRouteCorrection({1,2},{0,0},{0,0},false).x==0);
    // Recorded unchanged original42c9d0 ->42cc60 ->438b20 results, rather
    // than a second transcription. Includes asymmetric x87 spill controls.
    struct NativeCorrection {ContactVelocity p,a,b,expected;bool small;};
    for(const auto& r:{
        NativeCorrection{{5588.76123046875f,-6105.92138671875f},{-3204.07177734375f,-1533.332275390625f},{-3055.306396484375f,3713.33056640625f},{-86.70347595214844f,88.92390441894531f},false},
        NativeCorrection{{6099.2548828125f,4338.2685546875f},{482.22186279296875f,4516.41650390625f},{-1935.060302734375f,250.9874725341797f},{-69.0607681274414f,-20.964405059814453f},false},
        NativeCorrection{{4147.8984375f,-6168.0205078125f},{610.2464599609375f,580.86376953125f},{2249.875f,4269.75f},{-.16163882613182068f,1.107140064239502f},true},
        NativeCorrection{{3855.05908203125f,-764.3204345703125f},{-3333.82470703125f,29.259239196777344f},{-1968.146728515625f,-6069.2548828125f},{-.6676369905471802f,-.1495080590248108f},true}}){
        const auto actual=enemyRouteCorrection(r.p,r.a,r.b,r.small);
        assert(std::bit_cast<uint32_t>(static_cast<float>(actual.x))==std::bit_cast<uint32_t>(r.expected.x));
        assert(std::bit_cast<uint32_t>(static_cast<float>(actual.z))==std::bit_cast<uint32_t>(r.expected.z));
    }
    struct NativeAddition {ContactVelocity p,a,b,velocity,expected;float friction;bool small;};
    for(const auto& r:{
        NativeAddition{{3907.8984375f,99.62955474853516f},{1489.9989013671875f,842.9623413085938f},{1154.0732421875f,-3841.478759765625f},{-.1f,.03f},{-23.94504737854004f,1.7392655611038208f},.97f,false},
        NativeAddition{{-5384.6875f,-6047.15625f},{-4461.478515625f,409.99969482421875f},{3223.9482421875f,-471.48114013671875f},{-.1f,.03f},{.010033962316811085f,.653438925743103f},.97f,true}}){
        EnemyMotionCommand nativeCommand;nativeCommand.route=EnemyRouteMotion::Patrol;
        nativeCommand.routeCorrection=enemyRouteCorrection(r.p,r.a,r.b,r.small);
        EnemyMotionConfig nativeConfig;nativeConfig.frictionX=nativeConfig.frictionZ=r.friction;
        EnemyMotionState nativeState{r.velocity};advanceEnemyMotion(nativeState,nativeCommand,.014f,nativeConfig);
        assert(std::bit_cast<uint32_t>(nativeState.velocity.x)==std::bit_cast<uint32_t>(r.expected.x));
        assert(std::bit_cast<uint32_t>(nativeState.velocity.z)==std::bit_cast<uint32_t>(r.expected.z));
    }
    command.routeCorrection={2,3};command.route=EnemyRouteMotion::None;
    state={};advanceEnemyMotion(state,command,.014f,tank);assert(state.velocity.x==0);
    command.route=EnemyRouteMotion::Patrol;
    state={};advanceEnemyMotion(state,command,.014f,tank);
    assert(near(state.velocity.x,1.94f) && near(state.velocity.z,2.91f));

    const auto preserved=state;
    advanceEnemyMotion(state,command,std::numeric_limits<float>::quiet_NaN(),tank);
    assert(state.velocity.x==preserved.velocity.x && state.thrust==preserved.thrust);
}
