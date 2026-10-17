#pragma once
#include "assets/gaf_sprite.h"
#include <functional>

namespace vyruz {
struct AnimationCursorState { uint32_t cursor=0,flags=8; double remaining=0; };
// Native one-based cursor; zero represents a stopped/empty channel.
class AnimationCursor {
public:
    using Entered = std::function<void(const GafSubframe&)>;
    void reset(const GafSequence& sequence, const Entered& entered={});
    void advance(const GafSequence& sequence, double milliseconds, bool stopAtLast=false, const Entered& entered={});
    const GafSubframe* frame(const GafSequence& sequence) const;
    void holdLast(const GafSequence& sequence);
    void startGateTransition(const GafSequence& sequence, const Entered& entered={});
    //419580 kind4 keeps subframe1 in the baked base and starts at2 (reverse:2).
    void startTerrain(const GafSequence& sequence);
    bool atLast(const GafSequence& sequence) const;
    AnimationCursorState capture() const { return {cursor_,flags_,remaining_}; }
    bool restore(const GafSequence&,const AnimationCursorState&);
private:
    uint32_t cursor_ = 0, flags_ = 8;
    double remaining_ = 0;
};
}
