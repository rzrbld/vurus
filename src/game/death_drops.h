#pragma once
#include "game/content.h"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace vyruz {
// 40a010 stores absolute signed integers in the RANDOM template actor.
inline std::optional<uint32_t> deathDropInteger(std::string_view text) {
    if(!text.empty() && text.front()=='+')text.remove_prefix(1);
    int64_t value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() ||
       value<-INT32_MAX || value>INT32_MAX)return {};
    return static_cast<uint32_t>(value<0?-value:value);
}
// 43809c calls 4540c0(1, denominator), then compares <= numerator.
// 4540c0 uses unsigned32 * width * -2^-32, truncates toward zero and
// subtracts from the lower endpoint. Keep the port's saved RNG stream;
// this reproduces its native range mapping, not the native global seed.
inline uint32_t deathDropRoll(uint32_t raw,uint32_t denominator) {
    return 1+static_cast<uint32_t>((uint64_t(raw)*denominator)>>32);
}
// 437f80 shares scatter state across HAVE, RANDOM, then held objects.
// Unselected RANDOM entries do not consume a position. Coordinates are
// surface pixels, truncated before offsets; they are not native X/Z bounds.
struct DeathDropScatter {
    bool placed=false;
    int x=0,y=0,radius=32,left=-32;
    void beginList(){left=-radius;}
    ContentPosition next(ContentPosition origin,bool held=false) {
        const bool first=!placed;
        if(placed) {
            if(x==0)x=left;
            else if(x<0)x=-x;
            else {
                if(!held)radius+=10;
                left-=10;x=0;
                if(y==0)y=-10;
                else if(y<0)y=-y;
                else {y=0;x=left;}
            }
        }
        placed=true;
        if(first && held)return origin;
        return {std::trunc(origin.x)+x,std::trunc(origin.y)+y};
    }
};
}
