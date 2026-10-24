#include "render/menu_marquee.h"
#include "render/menu_effects.h"
#include "render/menu_exit.h"
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
    assert(saveLabelScrollOffset(5000,300,365)==0);
    assert(saveLabelScrollOffset(999,445,365)==0);
    assert(saveLabelScrollOffset(2000,445,365)==40);
    assert(saveLabelScrollOffset(3000,445,365)==80);
    assert(saveLabelScrollOffset(3999,445,365)==80);
    assert(saveLabelScrollOffset(5000,445,365)==40);
    assert(saveLabelScrollOffset(6000,445,365)==0);
    //421460 is finite even for looping/missing authored effects. The audible
    //menu adaptation retains entered-frame callbacks without World actors.
    GafSequence blast;blast.subframes={{0,0,0,2,7,0,0},{1,0,0,2,7,1,1}};
    MenuExitEffect exit;NativeRandomState random;unsigned starts=0,frames=0;
    std::array<const GafSequence*,2> sequences{&blast,&blast};
    exit.begin();
    while(exit.active&&frames++<625) {
        exit.advance(16,60,random,sequences,[&](const auto& p,const auto& frame){
            assert(p.x>=64&&p.x<=576&&p.y>=32&&p.y<=448&&p.bank<2);
            if(frame.command&1){assert(frame.sample==1);++starts;}
        });
        exit.retire();
    }
    assert(!exit.active&&frames<625&&exit.attempted>=400&&exit.attempted<=404&&starts==exit.attempted);
    assert(exit.particles.empty());
    blast.flags=2;exit.begin();
    exit.advance(9999,60,random,sequences,[](const auto&,const auto&){});
    assert(exit.active);
    exit.advance(1,60,random,sequences,[](const auto&,const auto&){});assert(!exit.active);
    exit.begin();sequences={};
    while(exit.active)exit.advance(16,60,random,sequences,[](const auto&,const auto&){assert(false);});
    assert(exit.attempted>=400&&exit.particles.empty());
    std::puts("PASS native menu clock, RNG flashes, latch, wrap and submenu/reset lifetime");
}
