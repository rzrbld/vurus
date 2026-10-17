#pragma once
#include "render/animation.h"
#include <vector>

namespace vyruz {
//434d40 initializes PANEL.GAF sequence1 at its first420ms frame. In435000,
//HUD modes>1 advance it only while the shield ratio is low. Above threshold
//the cursor is held, not reset. The shipped sequence enters sample1 after
//840ms, then every840ms (loopStart2). Presentation state, never a World save.
class NativeFullHudShieldAlarm {
public:
    void reset(const GafSequence& sequence={}) {
        sequence_=sequence;cursor_.reset(sequence_);
    }
    std::vector<std::uint32_t> advance(double elapsedMs,int hudMode,
                                      float shield,float shieldMaximum) {
        std::vector<std::uint32_t> samples;
        if(hudMode<=1 || static_cast<long double>(shield)/shieldMaximum>static_cast<long double>(.01f))return samples;
        cursor_.advance(sequence_,elapsedMs,false,[&](const GafSubframe& frame){
            if((frame.command&1) && frame.sample)samples.push_back(frame.sample);
        });
        return samples;
    }
    AnimationCursorState state() const {return cursor_.capture();}
private:
    GafSequence sequence_;
    AnimationCursor cursor_;
};
} // namespace vyruz
