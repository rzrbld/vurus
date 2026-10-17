#include "game/controls.h"

#include <algorithm>
#include <cmath>

namespace vyruz {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int kDirections = 24;
int wrap(int direction) { return (direction % kDirections + kDirections) % kDirections; }
// Exact decimal float constants from PE table 0050d508. Solver 00438b20
// accesses heading + 3, rather than treating the projected view as a circle.
constexpr Displacement kTable[] = {
    {-.0707107f,.0707107f},{-.05f,.0866025f},{-.0258819f,.0965926f},
    {0,.1f},{.0258819f,.0965926f},{.05f,.0866025f},
    {.0707107f,.0707107f},{.0866025f,.05f},{.0965926f,.0258819f},
    {.1f,0},{.0965926f,-.0258819f},{.0866025f,-.05f},
    {.0707107f,-.0707107f},{.05f,-.0866025f},{.0258819f,-.0965926f},
    {0,-.1f},{-.0258819f,-.0965926f},{-.05f,-.0866025f},
    {-.0707107f,-.0707107f},{-.0866025f,-.05f},{-.0965926f,-.0258819f},
    {-.1f,0},{-.0965926f,.0258819f},{-.0866025f,.05f}
};
}

Displacement nativeToSurface(float x, float z) {
    return {(x-z)*.8944272f,(x+z)*.4472136f};
}
Displacement surfaceToNative(float x, float y) {
    return {.559016994f*x+1.118033989f*y,-.559016994f*x+1.118033989f*y};
}
Displacement nativeHeading(int direction) { return kTable[wrap(wrap(direction)+3)]; }
int nativeDirectionFromVector(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y) || (x == 0 && y == 0)) return kDirections;
    const auto v = surfaceToNative(x,y); // native 00407630 -> 004075c0
    return wrap(static_cast<int>(std::floor((kPi/2-std::atan2(v.y,v.x))/(kPi/12)+.5f)));
}

Displacement advanceNativePlayerMotion(PlayerMotion& motion, const MovementCommand& command,
                                 float dt, const PlayerMotionConfig& c) {
    if (!(dt > 0) || !std::isfinite(dt)) return {};
    for (float value : {c.speedMin,c.speedMax,c.acceleration,c.deceleration,c.weight,
                       c.grip,c.frictionX,c.frictionZ,c.thresholdX,c.thresholdZ,
                       c.speedThreshold,c.turnSeconds,motion.velocityX,motion.velocityZ,
                       motion.magnitude,motion.thrust,motion.turnSeconds,
                       command.forward,command.strafe})
        if (!std::isfinite(value)) return {};
    if (c.weight <= 0 || c.turnSeconds <= 0 || c.speedMin > 0 || c.speedMax < 0 ||
        c.acceleration < 0 || c.deceleration < 0 || c.grip < 0 ||
        c.frictionX < 0 || c.frictionZ < 0 || c.thresholdX < 0 || c.thresholdZ < 0)
        return {};
    auto next = motion;
    next.direction = wrap(next.direction);
    // 443990/438390: Vyruz has two visual poses per propulsion direction.
    // The port stores24 directions, so retain the native36ms steady turn rate;
    // the intermediate visual pose and initial timer phase remain unrepresented.
    const float directionInterval=2*c.turnSeconds;
    if (command.aimActive) {
        const int target = nativeDirectionFromVector(command.aimX,command.aimY);
        // 41d7e0 -> 436600 emits no turn when the target is exactly opposite;
        // 438540 swaps left/right decisions for Reverse Commands. Keep that
        // antipodal stop when adapting the native decisions to absolute aim.
        const auto needsTurn = [&] {
            return target != kDirections && target != next.direction &&
                   !(c.reverseTurns && wrap(target-next.direction)==12);
        };
        if (needsTurn()) {
            next.turnSeconds += dt;
            // At most twelve steps to target even for unusually large dt.
            while (next.turnSeconds >= directionInterval && needsTurn()) {
                const int clockwise = wrap(target-next.direction);
                const int step = clockwise <= 12 ? 1 : -1;
                next.direction = wrap(next.direction+(c.reverseTurns ? -step : step));
                next.turnSeconds -= directionInterval;
            }
        } else next.turnSeconds = 0;
    }
    const float previousMagnitude = c.inertia ? next.magnitude : 0;
    float vx = c.inertia ? next.velocityX*c.grip : 0;
    float vz = c.inertia ? next.velocityZ*c.grip : 0;
    if (command.forward > 0) next.thrust = std::min(c.speedMax,next.thrust+c.acceleration);
    else if (command.forward < 0) next.thrust = std::max(c.speedMin,next.thrust-c.deceleration*3);
    else if (next.thrust > c.deceleration) next.thrust -= c.deceleration;
    else if (next.thrust < -c.deceleration) next.thrust += c.deceleration;
    else next.thrust = 0;
    if (command.forward != 0 && previousMagnitude < c.speedThreshold*.1f && next.thrust != 0) {
        const auto h = nativeHeading(next.direction);
        // Analog stick strength is a port adapter; full digital input is native.
        const float strength = std::min(1.f,std::abs(command.forward));
        vx += h.x*next.thrust/c.weight*strength;
        vz += h.y*next.thrust/c.weight*strength;
    }
    // No diagonal normalization. Native strafe is a separate +/-2 command.
    const float lateral = std::clamp(std::clamp(command.strafe,-1.f,1.f)*2,c.speedMin,c.speedMax);
    const auto side = nativeHeading(next.direction-6);
    vx = (vx+side.x*lateral)*c.frictionX;
    vz = (vz+side.y*lateral)*c.frictionZ;
    const float factor = dt/.014f;
    if (std::abs(vx) <= c.thresholdX*factor && std::abs(vz) <= c.thresholdZ*factor)
        vx = vz = 0;
    next.velocityX = vx; next.velocityZ = vz;
    next.magnitude = std::hypot(vx,vz);
    const auto result = nativeToSurface(vx*factor,vz*factor);
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(next.magnitude)) return {};
    motion = next;
    return result;
}

// Legacy surface-angle mapping remains for enemy policies not audited here.
// Player mouse aiming uses nativeDirectionFromVector inside the native solver.
int directionFromVector(float x,float y) {
    if(!std::isfinite(x)||!std::isfinite(y)||(x==0&&y==0))return 24;
    return wrap(static_cast<int>(std::lround((3*kPi/4-std::atan2(y,x))/(kPi/12))));
}
Displacement advancePlayerMotion(PlayerMotion& motion,const MovementCommand& command,float dt,
                                 const PlayerMotionConfig& config) {
    // Digital input follows 438b20's internal momentum/thrust solver. The host
    // keeps its fixed simulation clock; rendering never advances physics.
    if(dt>.1f)return {};
    auto adapted=config;
    // Analog input is a port extension: retain reduced stick top speed. Digital
    // full-strength commands use the unmodified native admission threshold.
    if(std::abs(command.forward)>0 && std::abs(command.forward)<1)
        adapted.speedThreshold*=std::abs(command.forward);
    return advanceNativePlayerMotion(motion,command,dt,adapted);
}
} // namespace vyruz
