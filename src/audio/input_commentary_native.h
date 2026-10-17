#pragma once
#include <cstdint>
#include <iterator>
#include <optional>

namespace vyruz {
struct NativeCommentaryCue {
    const char* name;
    unsigned delayMs,holdMs,tailMs;
};

// 4406e0/4410cb..4411b5. Ordinary438540 returns held fire, not movement;
// replay438800 returns its fire edge. These globals are transient process
// state, independent of the speech queue and saved World state.
struct NativeInputCommentary {
    std::uint32_t lastQuiet=0,lastActive=0;
    std::optional<NativeCommentaryCue> advance(std::uint32_t now,bool active) {
        if(active) {
            lastActive=now;
            if(lastQuiet==0 || std::uint32_t(now-lastQuiet)<3000)return {};
            lastQuiet=now; // Silent choices also restart the interval.
            static constexpr const char* names[]={"V-ARIBA.WAV","V-YEEPEE.WAV","V-YEAH.WAV"};
            const auto choice=now&7;
            if(choice<std::size(names))return NativeCommentaryCue{names[choice],300,2000,0};
        } else {
            lastQuiet=now;
            if(lastActive==0 || std::uint32_t(now-lastActive)<20000)return {};
            lastActive=now;
            static constexpr const char* names[]={"V-WAKE-UP.WAV","V-WAITING-FOR.WAV","V-WHAT-DOING.WAV","V-FORGET-MISSION.WAV"};
            const auto choice=now&7;
            if(choice<std::size(names))return NativeCommentaryCue{names[choice],0,3000,0};
        }
        return {};
    }
};
} // namespace vyruz
