#pragma once
#include "game/content.h"
#include "game/ordered_inventory.h"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
namespace vyruz {
// Native432bcd: green specular byte, driven by the running game clock.
inline float shieldRegenPulse(double milliseconds) {
    const auto clock=static_cast<uint32_t>(std::fmod(std::max(0.0,milliseconds),4294967296.0));
    return std::abs(std::trunc(std::sin(double(clock)*double(6.28318530718f)*double(.001f))*50.0))/255.f;
}

enum class SurpriseKind { None=0,Random=1,Reverse=2,Trap=3,Jam=4,Darkness=5,Xtc=6,Slow=7,Pinball=8,Banana=9,Motor=10,Destroy=11,Touch=12,Shield=13,Drone=14,Circular=15,TimeWarp=16,Ammo=17,Invulnerable=18,Triple=19,Nop=20 };
struct SurpriseState {
    SurpriseKind kind=SurpriseKind::None;
    float delayMs=0,remainingMs=0;
    bool applied=false;
    OrderedInventory<AmmoStock> savedAmmo;
    ContentPosition droneOrigin{},droneReturnFrom{};
    bool droneReturning=false,originalInvisible=false,originalSolid=false;
    bool jammed=false;
    int circularRemaining=0,timeWarpPhase=0;
    float tripleOffset=0;
    // Active in-memory attachment schedule; disk saves normalize Surprises.
    float bananaTrailNextMs=100;
    std::array<ContentPosition,5> bananaOrbitPositions{};
    bool bananaOrbitInitialized=false;
    // Attachment-time integer heights survive active in-memory checkpoints.
    // Native-compatible disk saves normalize the temporary Surprise itself.
    std::array<float,5> orbitAttachmentAltitudes{};
    bool orbitAltitudeInitialized=false;
    float feedbackMs=0,feedbackStrength=0;
    ContentPosition shakeOffset{};
    bool active(SurpriseKind k) const { return applied && kind==k; }
};
inline int surpriseDuration(SurpriseKind kind) {
    constexpr std::array<int,21> times={0,0,10000,1,10000,20000,10000,20000,20000,15000,5000,1,8000,15000,16000,10000,12000,15000,10000,20000,1};
    const auto id=static_cast<unsigned>(kind);return id<times.size()?times[id]:0;
}
inline const char* surpriseName(SurpriseKind kind) {
    constexpr std::array<const char*,21> names={"","Random Surprise","Reversed Commands !","Trap !","Weapon Jam !","No Light !","XTC !","Thrust Loss !","Pinball Effect !","No Inertia Control !","Blocked Motor !!!","Destroy Around !!!","Touch & Destroy !","Auto Shield Up","Invisible Drone !","Circular Fire !","Time Warp !","All Weapons Available !!!","Invulnerability !","Triple Fire !!!","Nothing Happens"};
    const auto id=static_cast<unsigned>(kind);return id<names.size()?names[id]:"";
}
inline unsigned surpriseIcon(SurpriseKind kind) { const auto id=static_cast<unsigned>(kind);return id>=2&&id<=10?id-1:id>=11&&id<=19?id:0; }
inline SurpriseKind surpriseRandom(unsigned roll) {
    constexpr std::array<unsigned,21> weights={0,0,10,10,5,10,5,10,0,10,1,5,5,10,10,5,5,5,10,10,10};
    roll%=136;for(unsigned i=2;i<weights.size();++i){if(roll<weights[i])return static_cast<SurpriseKind>(i);roll-=weights[i];}return SurpriseKind::Nop;
}
inline SurpriseKind surpriseParse(std::string_view token) {
    constexpr std::array<std::string_view,21> names={"none","random","malus-reverse-command","malus-trap","malus-weapon-jam","malus-darken","malus-extasy","malus-low-speed","malus-pinball","malus-banana","malus-blocked-motor","bonus-destroy-around","bonus-touch-and-destroy","bonus-auto-shield-up","bonus-invisible","bonus-circular-fire","bonus-time-warp","bonus-reload-weapons","bonus-invulnerability","bonus-triple-fire","bonus-nop"};
    for(unsigned i=0;i<names.size();++i)if(token==names[i])return static_cast<SurpriseKind>(i);
    if(token=="malus-reverse-commands")return SurpriseKind::Reverse;
    if(token=="bonus-time-decceleration")return SurpriseKind::TimeWarp;
    return SurpriseKind::None;
}
}
