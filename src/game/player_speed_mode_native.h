#pragma once
#include <cmath>
#include <optional>

namespace vyruz {
// These are live mutable native fields, not a low-mode
// boolean: XTC can change the comparison and later restore its saved threshold.
struct NativePlayerSpeedModeState {
    float speedThreshold=45;
    int turnPeriodMs=18;
    std::optional<float> surpriseBaselineSpeedThreshold;
};
enum class NativePlayerSpeedVoice { High,Low };
// Original43b830 arithmetic for finite, valid body/class values. The live input
// caller separately rejects active Surprise7 and applies edge-trigger admission.
inline std::optional<NativePlayerSpeedVoice> toggleNativePlayerSpeedMode(
        NativePlayerSpeedModeState& state,float authoredSpeed,int authoredPeriodMs) {
    if(!std::isfinite(state.speedThreshold)||!std::isfinite(authoredSpeed)||
       state.turnPeriodMs<0||state.turnPeriodMs>1431655765||authoredPeriodMs<0)return {};
    if(state.speedThreshold<authoredSpeed) {
        state.speedThreshold=authoredSpeed;
        state.turnPeriodMs=authoredPeriodMs;
        return NativePlayerSpeedVoice::High;
    }
    state.speedThreshold=authoredSpeed*0x1.555556p-2f; // Original float32 constant5095dc.
    state.turnPeriodMs=static_cast<int>(static_cast<double>(state.turnPeriodMs)*1.5);
    return NativePlayerSpeedVoice::Low;
}
}
