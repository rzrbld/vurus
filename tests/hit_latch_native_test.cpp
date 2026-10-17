#include "game/hit_latch_native.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>

static void check(bool ok,const char* description) {
    if(!ok){std::fprintf(stderr,"Hit latch: %s\n",description);std::exit(1);}
}
int main(int argc,char** argv) {
    using namespace vyruz;
    for(std::uint32_t bits=0;bits<16;++bits) {
        const auto flags=0x40000003u|(bits&1?0x200u:0u)|
            (bits&2?0x800000u:0u)|(bits&4?0x200000u:0u)|(bits&8?0x8000000u:0u);
        const auto latched=nativeHitLatchSet(flags);
        check(latched==(flags|((flags&0x800000u)?0u:0x200u)),"producer flags");
        check(nativeHitLatchSet(latched)==latched,"repeated producer is idempotent");
        for(bool player:{false,true})for(bool animationOnly:{false,true})
            for(std::int32_t pass:{-1,0,1,2,3})for(std::uint32_t now:{0u,100u,0xffffffffu}) {
                const auto result=nativeHitLatchRender(latched,0x12345678u,now,player,animationOnly,pass);
                const bool consume=player&&!animationOnly&&pass<=1&&(latched&0x200u);
                check(result.consumed==consume,"render admission");
                check(result.flags==(consume?latched&~0x200u:latched),"render preserves unrelated flags");
                check(result.timestamp==(consume?now:0x12345678u),"render timestamp");
                const auto repeat=nativeHitLatchRender(result.flags,result.timestamp,now+1,player,animationOnly,pass);
                check(!repeat.consumed&&repeat.timestamp==result.timestamp,"no repeated timestamp refresh without Hit");
            }
        check(nativeHitLatchPlayerDeath(flags)==(flags&~0x200u),"player death reset scope");
    }
    // Optional original-instruction oracle, produced by the retained hit-probe.
    // Ordinary CTest does not require proprietary resources or native output.
    if(argc>1) {
        std::ifstream in(argv[1],std::ios::binary);
        std::uint32_t count=0;in.read(reinterpret_cast<char*>(&count),4);
        check(bool(in)&&count==432,"native oracle header");
        for(std::uint32_t i=0;i<count;++i) {
            std::uint32_t row[8];in.read(reinterpret_cast<char*>(row),sizeof(row));
            check(bool(in),"native oracle record");
            const auto [kind,flags,player,pass,now,returned,finalFlags,stamp]=
                std::array<std::uint32_t,8>{row[0],row[1],row[2],row[3],row[4],row[5],row[6],row[7]};
            if(kind==3) {
                const auto result=nativeHitLatchRender(flags,0x12345678u,now,player,false,pass);
                check(result.flags==finalFlags&&result.timestamp==stamp,"native render comparison");
                check(returned==unsigned(player&&pass<=1),"native inner gate branch");
            } else check(nativeHitLatchSet(kind==0?flags|0x40u:flags)==finalFlags,
                         "native radial/contact producer comparison");
        }
        check(in.peek()==std::char_traits<char>::eof(),"native oracle trailing bytes");
        std::printf("432 original-instruction Hit latch records match\n");
    }
}
