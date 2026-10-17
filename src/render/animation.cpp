#include "render/animation.h"
#include <algorithm>
#include <cmath>

namespace vyruz {
bool AnimationCursor::restore(const GafSequence& s,const AnimationCursorState& state) {
    if(!state.cursor && state.flags==8 && state.remaining==0) { cursor_=0;flags_=8;remaining_=0;return true; }
    if(state.cursor>s.subframes.size() || !std::isfinite(state.remaining) ||
       ((state.flags^s.flags)&~uint32_t{0x48}) || (!state.cursor && !(state.flags&8))) return false;
    if(!(state.flags&8) && (state.remaining<=0 || state.remaining>static_cast<double>(s.subframes[state.cursor-1].duration)*14))return false;
    cursor_=state.cursor;flags_=state.flags;remaining_=state.remaining;return true;
}
void AnimationCursor::reset(const GafSequence& sequence,const Entered& entered) {
    cursor_=0; flags_=sequence.flags; remaining_=0;
    advance(sequence,0,false,entered);
}
void AnimationCursor::advance(const GafSequence& s, double milliseconds,bool stopAtLast,const Entered& entered) {
    if ((flags_&8) || !std::isfinite(milliseconds) || milliseconds<0) return;
    remaining_-=milliseconds;
    size_t zeroSteps=0;
    const uint32_t count=static_cast<uint32_t>(s.subframes.size());
    if(stopAtLast&&cursor_==count){flags_|=8;return;}
    while (remaining_<=0 && !(flags_&8)) {
        if (!(flags_&0x40)) {
            if (++cursor_>count) {
                if (flags_&0x20) { cursor_=s.loopStart>1?s.loopStart-1:1; flags_|=0x40; }
                else if (flags_&2) cursor_=s.loopStart;
                else cursor_=0;
            }
        } else if (cursor_ && --cursor_<=s.loopStart) {
            if (!(flags_&2)) cursor_=0;
            else if (flags_&0x20) { cursor_=std::min(s.loopStart+1,count); flags_&=~0x40u; }
            else cursor_=count;
        }
        if (!cursor_ || cursor_>count) { cursor_=0; flags_|=8; break; }
        if(entered)entered(s.subframes[cursor_-1]);
        if(stopAtLast&&cursor_==count){flags_|=8;break;}
        const double duration=static_cast<double>(s.subframes[cursor_-1].duration)*14;
        remaining_+=duration;
        if (duration>0) zeroSteps=0;
        else if (++zeroSteps>2*s.subframes.size()+1) { cursor_=0; flags_|=8; }
    }
}
const GafSubframe* AnimationCursor::frame(const GafSequence& s) const {
    return cursor_ && cursor_<=s.subframes.size()?&s.subframes[cursor_-1]:nullptr;
}
void AnimationCursor::holdLast(const GafSequence& s) {
    cursor_=static_cast<uint32_t>(s.subframes.size());flags_=s.flags|8;remaining_=0;
}
void AnimationCursor::startGateTransition(const GafSequence& s,const Entered& entered) {
    flags_=s.flags;remaining_=0;
    if(s.subframes.size()<=2){cursor_=0;flags_|=8;return;}
    cursor_=(flags_&0x40)?4:2;
    advance(s,0,false,entered);
}
void AnimationCursor::startTerrain(const GafSequence& s) {
    flags_=s.flags;remaining_=0;
    if(s.subframes.size()<2){reset(s);cursor_=0;flags_|=8;return;}
    // Native +e4 = (reverse ? 2 : 0)+1, then45e7e0(+d8).
    cursor_=(flags_&0x40)?3:1;
    advance(s,0);
}
bool AnimationCursor::atLast(const GafSequence& s) const {
    return s.subframes.empty()||cursor_==s.subframes.size();
}
}
