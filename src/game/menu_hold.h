#pragma once
#include <cstdint>
namespace vyruz {
// Native held-slider cadence421780: one step/150ms, no catch-up burst,
// left before right. Initial port taps remain immediate; UI context changes
// cancel the hold rather than applying it to a newly selected setting.
class MenuAdjustmentHold {
public:
    static constexpr uint64_t IntervalMs=150;
    void reset(){held_=0;due_=0;context_=-1;}
    void context(int value){if(context_!=value){reset();context_=value;}}
    void press(bool controller,int direction,uint64_t now){held_|=bit(controller,direction);due_=now+IntervalMs;}
    void release(bool controller,int direction){held_&=~bit(controller,direction);}
    int repeat(uint64_t now){
        if(!held_||now<due_)return 0;
        due_=now+IntervalMs;return held_&5?-1:1;
    }
private:
    static unsigned bit(bool controller,int direction){return (direction<0?1u:2u)<<(controller?2:0);}
    unsigned held_=0;
    uint64_t due_=0;
    int context_=-1;
};
}
