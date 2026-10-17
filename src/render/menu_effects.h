#pragma once
#include "game/menu.h"
#include <algorithm>
#include <bit>
#include <cstdint>

namespace vyruz {
// 421050/421780: menu-only updates, independent of the paused world clock.
class MenuEffects {
public:
    unsigned advance(MenuPage page,uint32_t elapsedMs,int refreshHz) {
        if(page==MenuPage::Closed) {page_=page;scanline_=0;logo_=false;updates_=0;return 0;}
        if(page!=page_) {
            if(page_==MenuPage::Closed)scanline_=0;
            logo_=false;page_=page;elapsedMs=0;
        }
        // 450d50 rounds each frame separately; no fractional remainder. The
        // original refresh period is integer milliseconds, with at least one
        // update before drawing, even when the elapsed timer reads zero.
        const unsigned quantum=std::max(1,1000/(refreshHz>0?refreshHz:60));
        const uint64_t updates=std::max(uint64_t{1},(uint64_t(elapsedMs)+quantum/2)/quantum);
        updates_=static_cast<unsigned>(updates);
        unsigned flashes=0;
        for(uint64_t i=0;i<updates;++i) {
            // 454080 multiply/rotate generator. Keep menu randomness separate
            // from the gameplay generator and save state.
            const uint32_t random=counter_++*random_;
            random_=std::rotl(random_,int(random&31));
            if(random%87==0){logo_=true;++flashes;}
            scanline_=scanline_<480?scanline_+1:0;
        }
        return flashes;
    }
    bool takeLogo() {const bool result=logo_;logo_=false;return result;}
    int scanline() const {return scanline_;}
    unsigned updates() const {return updates_;}
private:
    MenuPage page_=MenuPage::Closed;
    int scanline_=0;
    bool logo_=false;
    unsigned updates_=0;
    uint32_t random_=0xfeabcefe,counter_=0xabcdefda;
};
}
