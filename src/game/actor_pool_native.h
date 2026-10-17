#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace vyruz {

// Logical physical-slot identities are independent of World actor/projectile
// indices and UIDs. The native pool owns a dense active catalog followed by
// recycled slots; releasing a middle body swaps the final active slot into it.
// 41a010 / 419ac0. Storage capacity/preallocation does not consume a UID.
struct NativeActorPoolAllocation {
    std::size_t slot=0;
    std::uint32_t generation=0;
    friend bool operator==(const NativeActorPoolAllocation&,const NativeActorPoolAllocation&)=default;
};
struct NativeActorPoolState {
    std::uint32_t nextGeneration=1;
    std::size_t liveCount=0;
    std::vector<std::size_t> catalog;
    std::vector<std::uint32_t> generations;
    friend bool operator==(const NativeActorPoolState&,const NativeActorPoolState&)=default;
};

class NativeActorPool {
public:
    NativeActorPool()=default;
    // Primarily useful for controlled native comparison and exact snapshot
    // continuation. Zero is a valid raw next word after UINT32_MAX wraps.
    explicit NativeActorPool(std::uint32_t nextGeneration):nextGeneration_(nextGeneration){}
    NativeActorPoolAllocation allocate() {
        if(liveCount_==catalog_.size()) {
            const auto slot=catalog_.size();
            catalog_.push_back(slot);indices_.push_back(slot);generations_.push_back(0);
        }
        const auto slot=catalog_[liveCount_++];
        const auto generation=nextGeneration_++;
        generations_[slot]=generation;
        return {slot,generation};
    }
    // Caller releases by physical identity, not UID: native wrap through zero
    // is not mistaken for absence, and an actor's generation is cleared only
    // after catalog compaction. Returns false for an already released slot.
    bool release(std::size_t slot) {
        if(slot>=indices_.size() || indices_[slot]>=liveCount_)return false;
        const auto at=indices_[slot],last=--liveCount_;
        const auto moved=catalog_[last];
        std::swap(catalog_[at],catalog_[last]);
        indices_[moved]=at;indices_[slot]=last;generations_[slot]=0;
        return true;
    }
    std::size_t liveCount() const{return liveCount_;}
    bool live(std::size_t slot) const{return slot<indices_.size() && indices_[slot]<liveCount_;}
    std::uint32_t nextGeneration() const{return nextGeneration_;}
    const std::vector<std::size_t>& catalog() const{return catalog_;}
    std::uint32_t generation(std::size_t slot) const{return slot<generations_.size()?generations_[slot]:0;}
    NativeActorPoolState snapshot() const{return {nextGeneration_,liveCount_,catalog_,generations_};}
    bool restore(const NativeActorPoolState& state) {
        if(state.liveCount>state.catalog.size() || state.generations.size()!=state.catalog.size())return false;
        std::vector<std::size_t> indices(state.catalog.size(),state.catalog.size());
        for(std::size_t i=0;i<state.catalog.size();++i) {
            const auto slot=state.catalog[i];
            if(slot>=indices.size() || indices[slot]!=indices.size())return false;
            if(i>=state.liveCount && state.generations[slot]!=0)return false;
            indices[slot]=i;
        }
        auto catalog=state.catalog;auto generations=state.generations;
        nextGeneration_=state.nextGeneration;liveCount_=state.liveCount;
        catalog_=std::move(catalog);generations_=std::move(generations);indices_=std::move(indices);
        return true;
    }
private:
    std::uint32_t nextGeneration_=1;
    std::size_t liveCount_=0;
    std::vector<std::size_t> catalog_,indices_;
    std::vector<std::uint32_t> generations_;
};
}
