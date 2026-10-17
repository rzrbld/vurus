#pragma once
#include <cstdint>

namespace vyruz {
// Actor+128 bit200 is a persistent latch, not a damage amount or flash timer.
// Call only after the native radial/contact caller has admitted the body.
inline constexpr std::uint32_t nativeHitLatchSet(std::uint32_t flags) {
    //43ce6a,441b9a,441d92. Indestructible200000 does not gate these setters.
    return (flags&0x00800000u)?flags:(flags|0x200u);
}

struct NativeHitRenderResult {
    std::uint32_t flags=0;
    std::uint32_t timestamp=0;
    bool consumed=false;
};

//431ae0 outer animation-only/player guards, then432b7f..432bb4.
// The stamp is World+2588; now is the wrapping World+1020 clock.
// Enemy rendering and simulation animation-only visits leave the latch intact.
inline constexpr NativeHitRenderResult nativeHitLatchRender(
        std::uint32_t flags,std::uint32_t previousTimestamp,std::uint32_t now,
        bool controlledPlayer,bool animationOnly=false,std::int32_t pass=0) {
    if(!controlledPlayer || animationOnly || pass>1 || !(flags&0x200u))
        return {flags,previousTimestamp,false};
    return {flags&~0x200u,now,true};
}

//440ca5..440cb7 clears this bit as part of its broader player-death mask.
// This helper owns only Hit; the caller owns the other lifecycle flags.
inline constexpr std::uint32_t nativeHitLatchPlayerDeath(std::uint32_t flags) {
    return flags&~0x200u;
}
} // namespace vyruz
