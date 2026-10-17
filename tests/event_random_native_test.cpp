#include "game/event_random_native.h"
#include <array>
#include <cassert>
#include <cstdint>
using namespace vyruz;
int main() {
    // Original437f80,41ba00,43d100,43cb20 instructions,64 consecutive
    // interleaved visits per seed/demo/disabled combination. Hooks and scope:
    // work/quirk-audit/n3-rng-remaining/README.md.
    constexpr std::array<uint32_t,5> seeds={0xfeabcefe,0,1,0xdeadbeef,0xffffffff};
    constexpr std::array<uint32_t,5> counters={0xabcdefda,1,0xffffffff,123,0xffffffff};
    constexpr std::array<float,8> damage={0,199.99f,200,299.99f,300,2399.99f,2400,10000};
    constexpr std::array<uint64_t,20> nativeChecksums={
        0x337cb4519d993f42ull,
        0xd4bfe69e471bea4dull,
        0xcddb0876f236750eull,
        0x93adcbb3d92d9a3dull,
        0x98779967678de41eull,
        0x625c9de89b8ed365ull,
        0x31bc3ab44b054292ull,
        0x6e5d14d9027e6f75ull,
        0x577956c935122bc9ull,
        0x21fad8bedc04b3a0ull,
        0xc0d248248518fb2aull,
        0x66250b649b7cbcb9ull,
        0xc167c202ffe6c6b1ull,
        0xe984a8a0ac56560bull,
        0xa501242134afae42ull,
        0x18f8978917dd35b3ull,
        0xae240f82a0fb3351ull,
        0x9f94242bd6027b52ull,
        0x9bb598561ee80b00ull,
        0x600ad808aa53ffa5ull,

    };
    size_t control=0;
    for(unsigned s=0;s<5;++s)for(unsigned demoMode=0;demoMode<2;++demoMode)
    for(unsigned disabled=0;disabled<2;++disabled) {
        NativeRandomState global{seeds[s],counters[s]};
        NativeRandomState world{seeds[(s+1)%5],counters[(s+1)%5]};
        NativeRandomState demo{seeds[(s+2)%5],counters[(s+2)%5]};
        uint64_t checksum=14695981039346656037ull;
        const auto append=[&](uint32_t value){checksum=(checksum^value)*1099511628211ull;};
        for(unsigned step=0;step<64;++step) {
            append(unsigned(nativeRandomSurprise(demoMode?demo:global)));
            uint32_t mask=0;
            //438096 skips RANDOM entirely under replay/demo flags18000.
            if(!demoMode)for(unsigned n=0;n<4;++n)
                if(nativeDeathRandomDrop(global,n==0?0:n==1?1:n==2?100:step%7+1,step%7+1)){mask|=1u<<n;world.inclusive(0,23);}
            append(mask);
            const auto ordinary=nativeRandomShake(global,step%2?16:32,{71,72},disabled);
            append(ordinary[0]);append(ordinary[1]);
            // FX-source admission rejects protected targets; health/shake
            // never execute for those controls. Other admissions are true.
            const auto radial=nativeRadialDamageShake(world,damage[step%8],step%3!=0,{81,82},disabled);
            append(radial[0]);append(radial[1]);
            append(global.seed);append(global.counter);append(world.seed);append(world.counter);append(0);
            append(demo.seed);append(demo.counter);append(0);
        }
        assert(checksum==nativeChecksums[control++]);
    }
    NativeRandomState random;const auto old=random;
    assert((nativeRadialDamageShake(random,1000,false,{4,5})==std::array<int,2>{4,5}));
    assert(random.seed==old.seed && random.counter==old.counter);
}
