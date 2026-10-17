#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vyruz {
// Sparse, stable bare-Ammo identities share the actor lists' insertion clock.
// Graph-node references and compacted Projectile vector indices are not IDs.
struct NativeAmmoCellLink {
    std::uint64_t next=UINT64_MAX,arrivalOrder=0;
    std::size_t cell=SIZE_MAX;
};
struct NativeAmmoCells {
    static constexpr std::uint64_t Missing=UINT64_MAX;
    int width=0,height=0;
    std::vector<std::uint64_t> heads;
    std::unordered_map<std::uint64_t,NativeAmmoCellLink> links;
    void reset(int w,int h) {
        width=w;height=h;heads.assign(std::size_t(w)*std::size_t(h),Missing);links.clear();
    }
    void erase(std::uint64_t id) {
        const auto found=links.find(id);if(found==links.end())return;
        auto* link=&heads[found->second.cell];
        while(*link!=Missing && *link!=id) {
            const auto next=links.find(*link);if(next==links.end())return;
            link=&next->second.next;
        }
        if(*link==id)*link=found->second.next;
        links.erase(found);
    }
    void relocate(std::uint64_t id,std::size_t destination,std::uint64_t order) {
        const auto found=links.find(id);
        if(found!=links.end() && found->second.cell==destination)return;
        erase(id);
        if(id==Missing || destination>=heads.size())return;
        links.emplace(id,NativeAmmoCellLink{heads[destination],order,destination});
        heads[destination]=id;
    }
    bool valid() const {
        if(width<0||height<0||std::uint64_t(width)*std::uint64_t(height)>10000000||
           heads.size()!=std::size_t(width)*std::size_t(height))return false;
        std::unordered_set<std::uint64_t> seen;
        for(std::size_t cell=0;cell<heads.size();++cell) {
            auto previous=UINT64_MAX;
            for(auto id=heads[cell];id!=Missing;) {
                const auto at=links.find(id);
                if(at==links.end()||!seen.insert(id).second||at->second.cell!=cell||
                   !at->second.arrivalOrder||at->second.arrivalOrder>=previous)return false;
                previous=at->second.arrivalOrder;id=at->second.next;
            }
        }
        return seen.size()==links.size();
    }
};
//41a8c0's head insertion is reconstructed by merging independently retained
//actor and bare-Ammo heads on the shared insertion ordinal. No source scan.
inline bool nativeBareAmmoVisitsFirst(std::uint64_t actorOrder,std::uint64_t ammoOrder,
                                      bool actorPresent,bool ammoPresent) {
    return ammoPresent && (!actorPresent || ammoOrder>actorOrder);
}
} //namespace vyruz
