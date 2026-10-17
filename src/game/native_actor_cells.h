#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace vyruz {
// Runtime cell lists are independent of the navigation edge graph.41a8c0
// prepends;41a970 unlinks and clears next. A same-cell move keeps list order.
// Actor IDs are stable port identities; no native pointer value is serialized.
struct NativeActorCells {
    static constexpr size_t Missing=SIZE_MAX;
    int width=0,height=0;
    std::vector<size_t> heads,next,cell;
    std::vector<uint64_t> arrivalOrder;
    uint64_t nextArrivalOrder=1; // Local fallback; World supplies a shared ordinal.
    void reset(int w,int h) {
        width=w;height=h;heads.assign(size_t(w)*size_t(h),Missing);
        next.clear();cell.clear();arrivalOrder.clear();nextArrivalOrder=1;
    }
    size_t coordinate(float x,float y) const {
        if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||
           x>=double(width)*64||y>=double(height)*32)return Missing;
        return size_t(static_cast<uint32_t>(x)>>6)*size_t(height)+
               (static_cast<uint32_t>(y)>>5);
    }
    void erase(size_t id) {
        if(id>=cell.size()||cell[id]==Missing)return;
        size_t* link=&heads[cell[id]];
        while(*link!=Missing && *link!=id)link=&next[*link];
        if(*link==id)*link=next[id];
        cell[id]=next[id]=Missing;arrivalOrder[id]=0;
    }
    void relocate(size_t id,size_t destination,std::optional<uint64_t> order={}) {
        if(id>=cell.size()){cell.resize(id+1,Missing);next.resize(id+1,Missing);arrivalOrder.resize(id+1,0);}
        if(destination>=heads.size())destination=Missing;
        if(cell[id]==destination)return;
        erase(id);
        if(destination!=Missing){next[id]=heads[destination];heads[destination]=id;cell[id]=destination;
            arrivalOrder[id]=order.value_or(nextArrivalOrder);
            nextArrivalOrder=std::max(nextArrivalOrder,arrivalOrder[id]+1);}
    }
    bool valid(size_t actors) const {
        if(width<0||height<0||uint64_t(width)*uint64_t(height)>10000000||
           heads.size()!=size_t(width)*size_t(height)||cell.size()!=actors||next.size()!=actors||arrivalOrder.size()!=actors)return false;
        std::vector<bool> seen(actors,false);
        for(size_t c=0;c<heads.size();++c) {
            auto previous=UINT64_MAX;
            for(size_t id=heads[c];id!=Missing;id=next[id]){
                if(id>=actors||seen[id]||cell[id]!=c||!arrivalOrder[id]||arrivalOrder[id]>=previous)return false;
                previous=arrivalOrder[id];
                seen[id]=true;
            }
        }
        for(size_t id=0;id<actors;++id)
            if((cell[id]!=Missing)!=seen[id]||(!seen[id]&&(next[id]!=Missing||arrivalOrder[id])))return false;
        return true;
    }
};
}
