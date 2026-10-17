#include "game/ammo_owner_counters_native.h"
#include "game/native_actor_cells.h"
#include "game/native_ammo_cells.h"
#include "fixtures/native_mixed_ammo_cells.h"
#include "fixtures/native_ammo_owner_counters.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 for(const auto& r:NativeAmmoOwnerCountersObserved){
    NativeAmmoOwnerCounters c{UINT32_MAX,UINT32_MAX,UINT32_MAX,r[1]};
    nativeAmmoOwnerDirectContact(c,r[0]==1||r[0]==3,r[2]==0);
    need(c.misses==r[3]&&c.hits==r[4]&&c.kills==r[5]&&c.activeAmmo==r[6],"observed owner hit/miss/wrap/signed-positive contract");
 }
 for(unsigned sequence=0;sequence<NativeMixedAmmoCellsObserved.size();++sequence){
    NativeActorCells actors;actors.reset(1,2);actors.relocate(2,SIZE_MAX);
    NativeAmmoCells bare;bare.reset(1,2);std::uint64_t order=1;
    std::array<size_t,3> cell{SIZE_MAX,SIZE_MAX,SIZE_MAX};auto code=sequence;
    for(int step=0;step<4;++step){const auto operation=code%9;code/=9;const unsigned id=operation/3;
        const auto destination=operation%3==2?SIZE_MAX:size_t(operation%3);
        if(cell[id]!=destination){
            const auto ordinal=destination==SIZE_MAX?0:order++;
            if(id==1)bare.relocate(9,destination,ordinal);
            else actors.relocate(id,destination,ordinal);
            cell[id]=destination;
        }
    }
    need(actors.valid(3)&&bare.valid(),"split lists retain strict native order");
    std::array<unsigned,2> head{3,3};std::array<unsigned,3> next{3,3,3};
    for(size_t c=0;c<2;++c){auto a=actors.heads[c];auto b=bare.heads[c];unsigned previous=3;
        while(a!=SIZE_MAX||b!=NativeAmmoCells::Missing){const auto at=bare.links.find(b);
            const bool first=nativeBareAmmoVisitsFirst(a==SIZE_MAX?0:actors.arrivalOrder[a],at==bare.links.end()?0:at->second.arrivalOrder,a!=SIZE_MAX,at!=bare.links.end());
            const unsigned current=first?1:unsigned(a);
            if(previous==3)head[c]=current;else next[previous]=current;previous=current;
            if(first)b=at->second.next;else a=actors.next[a];
        }
    }
    std::uint16_t packed=head[0]|(head[1]<<2);
    for(unsigned i=0;i<3;++i){packed|=next[i]<<(4+2*i);packed|=(cell[i]==SIZE_MAX?3:cell[i])<<(10+2*i);}
    need(packed==NativeMixedAmmoCellsObserved[sequence],"mixed actor/bare cell order matches unchanged original producer");
 }
 NativeAmmoOwnerCounters doubleRelease;doubleRelease.activeAmmo=3;
 nativeAmmoOwnerDirectContact(doubleRelease,true,true);
 nativeAmmoOwnerReleased(doubleRelease,true,true,false,false);
 need(doubleRelease.activeAmmo==1&&doubleRelease.hits==1,"direct bookkeeping and later release independently decrement");
 nativeAmmoOwnerReleased(doubleRelease,true,true,true,false);
 nativeAmmoOwnerReleased(doubleRelease,true,true,false,true);
 need(doubleRelease.activeAmmo==1,"either native keep flag suppresses release decrement");
 nativeAmmoOwnerLaunched(doubleRelease);need(doubleRelease.activeAmmo==2,"successful launch increments raw counter");
 std::cout<<"native owner84 observed cases and mixed-cell6561 unchanged-code sequences passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
