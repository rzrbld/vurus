#pragma once
#include "game/native_random.h"
#include "render/animation.h"
#include <algorithm>
#include <array>
#include <vector>

namespace vyruz {
// 421460: screen-space ANMs, never World actors or damaging explosions.
class MenuExitEffect {
public:
    struct Particle {int x,y;unsigned bank;AnimationCursor cursor;};
    void begin() {particles.clear();attempted=0;elapsed=0;active=true;}
    template<class Sample>
    void advance(uint32_t milliseconds,int refreshHz,NativeRandomState& random,
                 const std::array<const GafSequence*,2>& sequences,Sample sample) {
        if(!active)return;
        elapsed+=milliseconds;
        if(elapsed>=10000){active=false;return;}
        const unsigned q=std::max(1,1000/(refreshHz>0?refreshHz:60));
        const uint64_t steps=std::max(uint64_t{1},(uint64_t(milliseconds)+q/2)/q);
        bool visited=false;
        for(uint64_t step=0;step<steps;++step) {
            const unsigned count=attempted<400?unsigned(random.inclusive(0,5)):0;
            attempted+=count; // The last batch may pass400, as in421538.
            for(unsigned i=0;i<count;++i) {
                Particle p{random.inclusive(64,576),random.inclusive(32,448),unsigned(random.inclusive(0,1)),{}};
                if(const auto* s=sequences[p.bank]) {
                    // 413630->45e770 enters the first frame BEFORE417a20
                    // installs the spatial route; its sample is unpositioned.
                    p.cursor.reset(*s,[&](const auto& frame){sample(p,frame);});
                    particles.push_back(std::move(p));
                }
            }
            for(auto& p:particles)if(!(p.cursor.capture().flags&0x18)) {
                visited=true;
                // Native updates all ANMs once per admitted menu step with
                // world101c (frame delta), not the refresh quantum.
                p.cursor.advance(*sequences[p.bank],milliseconds,false,
                    [&](const auto& frame){sample(p,frame);});
            }
        }
        if(!visited&&attempted>=400)active=false;
    }
    // Native deletion is AFTER drawing; preserve the final frame/loop flag.
    void retire() {std::erase_if(particles,[](const auto& p){return bool(p.cursor.capture().flags&0x18);});}
    std::vector<Particle> particles;
    unsigned attempted=0;
    uint64_t elapsed=0;
    bool active=false;
};
}
