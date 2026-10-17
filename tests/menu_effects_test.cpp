#include "render/menu_effects.h"
#include <array>
#include <cassert>
#include <cstdio>
int main() {
    using namespace vyruz;
    MenuEffects effects;
    // Golden flash positions from the unchanged454080 original-PE probe.
    constexpr std::array flashes{59,184,338,482,558,691,694,869,916,958,972};
    size_t flash=0;
    for(int step=1;step<=1000;++step) {
        const bool expected=flash<flashes.size() && step==flashes[flash];
        assert(effects.advance(MenuPage::Main,16,60)==unsigned(expected));
        assert(effects.scanline()==step%481);
        assert(effects.takeLogo()==expected);
        assert(!effects.takeLogo()); // A draw consumes the latch.
        if(expected)++flash;
    }
    assert(flash==flashes.size());
    // Native450d50 rounded clock boundaries (404 original-PE cases retained).
    for(int hz:{30,60,75,90})for(unsigned elapsed=0;elapsed<=100;++elapsed) {
        MenuEffects clock;clock.advance(MenuPage::Main,0,hz);
        clock.advance(MenuPage::Main,elapsed,hz);
        const unsigned q=1000/hz;
        assert(clock.scanline()==1+int(std::max(1u,(q/2+elapsed)/q)));
    }
    MenuEffects latch;
    latch.advance(MenuPage::Main,0,60);
    assert(latch.advance(MenuPage::Main,60*16,60)==1);
    assert(latch.scanline()==61 && latch.takeLogo());
    latch.advance(MenuPage::Options,9999,60);
    assert(latch.scanline()==62 && !latch.takeLogo()); // Keep scan, reset descriptor.
    latch.advance(MenuPage::Closed,9999,60);
    assert(latch.scanline()==0 && !latch.takeLogo());
    latch.advance(MenuPage::Game,9999,60);
    assert(latch.scanline()==1); // Fresh menu never catches up gameplay time.
    std::puts("PASS native menu clock, RNG flashes, latch, wrap and submenu/reset lifetime");
}
