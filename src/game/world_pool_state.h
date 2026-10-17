#pragma once
#include "game/actor_pool_native.h"
#include <cstdint>
#include <vector>
#include <set>
namespace vyruz {
struct NativePoolTileBody {
    std::uint32_t x=0,y=0,source=0,layer=0,generation=0;
    std::size_t poolSlot=SIZE_MAX;
    std::uint64_t arrivalOrder=0;
    friend bool operator==(const NativePoolTileBody&,const NativePoolTileBody&)=default;
};
struct NativeWorldPoolState {
    NativeActorPoolState pool;
    std::vector<NativePoolTileBody> tiles;
    // Deterministic continuation is available even when a legacy checkpoint
    // lacks the original prior physical-slot history. Never invent that proof.
    bool originalHistoryKnown=true;
    friend bool operator==(const NativeWorldPoolState&,const NativeWorldPoolState&)=default;
};
struct NativePoolReplacementContext {
    NativeWorldPoolState state;
    std::vector<std::size_t> releaseOrder; //Transient old-context destructor order.
};
template<class Snapshot> bool validNativeWorldPoolSnapshot(const Snapshot& saved) {
    if(!saved.nativePool)return true;
    const auto& state=*saved.nativePool;NativeActorPool pool;
    if(!pool.restore(state.pool) || pool.nextGeneration()!=saved.nextNativeGeneration)return false;
    std::set<std::size_t> owned;
    const auto admit=[&](std::size_t slot,std::uint32_t generation,bool alias=false) {
        if(slot==SIZE_MAX)return generation==0;
        if(!pool.live(slot)||pool.generation(slot)!=generation)return false;
        return owned.insert(slot).second || alias;
    };
    for(const auto& actor:saved.actors)if(!admit(actor.nativePoolSlot,actor.nativeGeneration))return false;
    for(const auto& shot:saved.projectiles) {
        if(shot.visual){if(shot.nativePoolSlot!=SIZE_MAX)return false;continue;}
        if(!admit(shot.nativePoolSlot,shot.nativeGeneration))return false;
    }
    for(const auto& held:saved.heldItems) {
        // A local retained original has both a dormant ActorState and an
        // inventory copy; those records alias exactly one native body.
        const auto id=held.object.id.value;
        const bool alias=id<saved.actors.size() && id<saved.objects.size() &&
            !saved.actors[id].alive && saved.objects[id].classId==held.object.classId &&
            saved.actors[id].nativePoolSlot==held.actor.nativePoolSlot &&
            saved.actors[id].nativeGeneration==held.actor.nativeGeneration;
        if(!admit(held.actor.nativePoolSlot,held.actor.nativeGeneration,alias))return false;
    }
    std::set<std::uint64_t> arrivals;
    if(saved.actorCells)for(const auto order:saved.actorCells->arrivalOrder)if(order)arrivals.insert(order);
    for(const auto& [id,link]:saved.bareAmmoCells.links)if(link.arrivalOrder)arrivals.insert(link.arrivalOrder);
    for(const auto& tile:state.tiles) {
        if(!saved.actorCells || tile.x>=std::uint32_t(saved.actorCells->width)||tile.y>=std::uint32_t(saved.actorCells->height)||
            tile.layer>=4 || !tile.source || !tile.arrivalOrder || tile.arrivalOrder>=saved.nextNativeCellOrder ||
            !arrivals.insert(tile.arrivalOrder).second || !admit(tile.poolSlot,tile.generation))return false;
    }
    //4199d0 skips UID0 kind3/4 bodies after wrap, leaving a physical live
    //slot with no scene owner. No other unowned live generation is legal.
    for(std::size_t i=0;i<pool.liveCount();++i) {
        const auto slot=pool.catalog()[i];if(!owned.contains(slot) && pool.generation(slot)!=0)return false;
    }
    return true;
}
}
