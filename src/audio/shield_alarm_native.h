#pragma once
#include <bit>
#include <cstdint>

namespace vyruz {
//43e370: HUD modes0/1 run the independent1000ms alarm. Higher modes
//freeze this signed countdown. Sound is channel3/mask4, priority127,
//nonlooping, replacing channel3; Green Info gates only accompanying text.
struct NativeShieldAlarm {
    std::int32_t remainingMs=0;
    bool advance(std::int32_t elapsedMs,int hudMode,float shield,float shieldMaximum) {
        if(hudMode!=0 && hudMode!=1)return false;
        remainingMs=std::bit_cast<std::int32_t>(
            std::bit_cast<std::uint32_t>(remainingMs)-std::bit_cast<std::uint32_t>(elapsedMs));
        if(remainingMs>0)return false;
        //Native x87 keeps division extended until comparison. An unordered
        //comparison also enters the <= path; do not invent a Max>0 condition.
        if(static_cast<long double>(shield)/shieldMaximum>static_cast<long double>(.01f))return false;
        remainingMs=1000;return true;
    }
};
} // namespace vyruz
