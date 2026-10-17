#pragma once
#include <algorithm>
#include <cmath>

namespace vyruz {
// Presentation adapter only. The accepted 24-node motion solver is authoritative.
// Original 438390 supplies the 48-pose steps; 4333f0 supplies the banking banks.
struct PlayerTurnPresentation {
    int heading=-1, pose=0, target=0, lean=0, command=0;
    double poseClock=0, leanClock=0, commandMs=0;
    void reset(int direction) {
        heading=(direction%24+24)%24;pose=target=heading*2;
        lean=command=0;poseClock=leanClock=commandMs=0;circularCounter=-1;
    }
    unsigned bank() const { return lean==0?25:(lean<0?26:29)+(std::abs(lean)-1)/5; }
    // Circular Fire's emitter writes the visible token directly (+2, >48→1).
    // Its pending counter is sampled per world/presentation update; it is not
    // an ordinary steering command and must not enqueue18ms half-pose steps.
    int circularCounter=-1;
    void advance(int direction,double ms,int remaining=-1) {
        direction=(direction%24+24)%24;
        const int previous=circularCounter;
        const bool tracked=remaining>=0&&remaining<=24&&previous>=0&&previous<=24;
        if(heading<0){reset(direction);circularCounter=remaining;return;}
        if(tracked&&previous>0) {
            const int steps=previous-remaining;
            const int logical=(direction-heading+24)%24;
            if(steps>=0&&steps<24&&logical==steps%24){
                for(int n=0;n<steps;++n){pose+=2;if(pose>=48)pose=0;}
                heading=direction;
            }else if(logical!=0)reset(direction); // Debug/restore discontinuity.
            // The early native sweep skips443990/438390, including its pose
            // clock. A failed/jammed sweep also takes this early actor path.
            advanceLean(ms);circularCounter=remaining;return;
        }
        if(tracked&&previous==0&&remaining>0&&remaining<24){
            const int steps=24-remaining;
            advanceOrdinary((direction-steps%24+24)%24,ms);
            for(int n=0;n<steps;++n){pose+=2;if(pose>=48)pose=0;}
            heading=direction;circularCounter=remaining;return;
        }
        advanceOrdinary(direction,ms);circularCounter=remaining;
    }
    void advanceLean(double ms) {
        leanClock+=ms;
        while(leanClock>=1000.0/60) {
            leanClock-=1000.0/60;
            if(commandMs>0)lean=std::clamp(lean+command,-15,15);
            else lean+=lean<0?1:lean>0?-1:0;
            commandMs=std::max(0.0,commandMs-1000.0/60);
        }
    }
    void advanceOrdinary(int direction,double ms) {
        if(heading<0){reset(direction);return;}
        int delta=(direction-heading+36)%24-12;
        // Debug/restore/demo discontinuities must not sweep through unrelated poses.
        if(std::abs(delta)>2 || ms>100){reset(direction);return;}
        if(delta){command=delta>0?-1:1;commandMs=50;heading=direction;}
        poseClock+=ms;
        while(poseClock>=18) {
            poseClock-=18;
            if(pose==target)target=heading*2;
            if(pose!=target) {
                const int distance=(target-pose+48)%48;
                pose=(pose+(distance<24?1:47))%48;
            }
        }
        advanceLean(ms);
    }
};
}
