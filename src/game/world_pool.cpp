#include "game/world.h"
#include "game/save_state.h"
#include "game/properties.h"
#include <algorithm>
#include <functional>
#include <set>
#include <unordered_map>

namespace vyruz {
namespace {
std::string mapName(std::string name){name=folded(name);std::replace(name.begin(),name.end(),'\\','/');return name;}
struct CellBody {std::size_t cell=0,slot=SIZE_MAX,actor=SIZE_MAX;std::uint64_t order=0;};
}
NativeActorPoolAllocation GameWorld::allocateNativePoolBody() {
    const auto body=nativePool_.allocate();nextNativeGeneration_=nativePool_.nextGeneration();return body;
}
void GameWorld::releaseNativePoolBody(std::size_t& slot) {
    if(slot!=SIZE_MAX)nativePool_.release(slot);
    slot=SIZE_MAX;
}
void GameWorld::resetNativePoolForLoad() {
    nativePool_=NativeActorPool{};nativeTileBodies_.clear();nativePoolOriginalHistoryKnown_=true;nativeRetainedHeldIdentities_.clear();
    nextNativeGeneration_=nativePool_.nextGeneration();
}
void GameWorld::initializeNativeTileBodies() {
    if(!nativeTileCatalog_ || !nativeTileBodies_.empty())return;
    visitNativeTileBodyPlacements(*nativeTileCatalog_,[&](const auto& placement){
        const auto allocation=allocateNativePoolBody();
        nativeTileBodies_.push_back({placement.x,placement.y,placement.source,placement.slot,
                                    allocation.generation,allocation.slot,nextNativeCellOrder_++});
        return true;
    });
}
NativeWorldPoolState GameWorld::captureNativePool() const {
    return {nativePool_.snapshot(),nativeTileBodies_,nativePoolOriginalHistoryKnown_};
}
NativePoolReplacementContext GameWorld::nativePoolReplacementContext() const {
    auto pool=nativePool_;bool history=nativePoolOriginalHistoryKnown_;
    std::vector<std::size_t> releaseOrder;
    std::vector<CellBody> cells;cells.reserve(actors_.size()+projectiles_.size()+nativeTileBodies_.size());
    for(std::size_t id=0;id<actors_.size();++id)
        if(id<actorCells_.cell.size() && actorCells_.cell[id]!=SIZE_MAX && actors_[id].nativePoolSlot!=SIZE_MAX)
            cells.push_back({actorCells_.cell[id],actors_[id].nativePoolSlot,id,actorCells_.arrivalOrder[id]});
    for(const auto& shot:projectiles_)if(!shot.visual && shot.nativePoolSlot!=SIZE_MAX) {
        const auto cell=bareAmmoCells_.links.find(shot.nativeBodyIdentity);
        if(cell!=bareAmmoCells_.links.end())cells.push_back({cell->second.cell,shot.nativePoolSlot,SIZE_MAX,cell->second.arrivalOrder});
    }
    for(const auto& tile:nativeTileBodies_)cells.push_back({std::size_t(tile.x)*std::size_t(actorCells_.height)+tile.y,
                                                          tile.poolSlot,SIZE_MAX,tile.arrivalOrder});
    //41a360: X descending, Y descending; follow each mixed head. Each
    //4199d0 destructor recursively visits the newest held original first.
    std::sort(cells.begin(),cells.end(),[](const auto& a,const auto& b){return a.cell!=b.cell?a.cell>b.cell:a.order>b.order;});
    std::set<std::size_t> visiting;
    std::function<void(std::size_t,std::size_t)> release=[&](std::size_t slot,std::size_t actor) {
        if(slot==SIZE_MAX || !pool.live(slot) || !visiting.insert(slot).second)return;
        if(actor!=SIZE_MAX)for(auto held=heldItems_.rbegin();held!=heldItems_.rend();++held)
            if(held->holder==actor)release(held->actor.nativePoolSlot,SIZE_MAX);
        if(pool.generation(slot)!=0){releaseOrder.push_back(slot);pool.release(slot);} //4199dc..4199f0.
    };
    for(const auto& body:cells)release(body.slot,body.actor);
    // An isolated synthetic/checkpoint body can have no spatial/held producer.
    // Release it deterministically, while retaining the missing-history marker.
    const auto remaining=pool.snapshot();
    for(std::size_t i=remaining.liveCount;i-->0;) {
        const auto slot=remaining.catalog[i];
        if(pool.generation(slot)!=0){history=false;releaseOrder.push_back(slot);pool.release(slot);}
    }
    return {{nativePool_.snapshot(),{},history},std::move(releaseOrder)};
}
NativeWorldPoolState GameWorld::nativePoolForReplacement() const {
    auto context=nativePoolReplacementContext();NativeActorPool pool;pool.restore(context.state.pool);
    for(const auto slot:context.releaseOrder)pool.release(slot);
    return {pool.snapshot(),{},context.state.originalHistoryKnown};
}
void GameWorld::releaseNativeHeldItem(HeldItem& held) {
    const auto slot=held.actor.nativePoolSlot;
    if(scene_ && mapName(held.mapPath)==mapName(scene_->mapPath) && held.object.id.value<actors_.size()) {
        auto& actor=actors_[held.object.id.value];
        if(actor.nativePoolSlot==slot && actor.nativeGeneration==held.actor.nativeGeneration) {
            releaseNativeAwakeActor(held.object.id.value);
            if(actor.nativeGeneration)releaseNativePoolBody(actor.nativePoolSlot);else actor.nativePoolSlot=SIZE_MAX;
            actor.nativeGeneration=0;
        }
    }
    if(held.actor.nativeGeneration)releaseNativePoolBody(held.actor.nativePoolSlot);else held.actor.nativePoolSlot=SIZE_MAX;
    held.actor.nativeGeneration=0;
}
void GameWorld::releaseNativeHeldItemsForReplacement(const std::vector<HeldItem>& incoming) {
    nativeRetainedHeldIdentities_.clear();
    for(auto& held:heldItems_) {
        const bool retained=std::any_of(incoming.begin(),incoming.end(),[&](const auto& item){
            return item.actor.nativePoolSlot==held.actor.nativePoolSlot && item.actor.nativeGeneration==held.actor.nativeGeneration &&
                   mapName(item.mapPath)==mapName(held.mapPath) && item.object.id==held.object.id;
        });
        if(!retained)releaseNativeHeldItem(held);
        else if(held.actor.nativePoolSlot!=SIZE_MAX && nativePool_.live(held.actor.nativePoolSlot) &&
                nativePool_.generation(held.actor.nativePoolSlot)==held.actor.nativeGeneration)
            nativeRetainedHeldIdentities_.insert({held.actor.nativePoolSlot,held.actor.nativeGeneration});
    }
}
void GameWorld::adoptNativeHeldItems() {
    for(auto& held:heldItems_) {
        const auto id=held.object.id.value;
        const bool local=mapName(held.mapPath)==mapName(scene_->mapPath) && id<actors_.size() && !actors_[id].alive &&
                         contentNameEqual(scene_->classes[scene_->objects[id].classId.value].name,held.className);
        //41fac2 discards the old context's awake manager. Foreign carry is
        //a retained port inventory record, never an alias of that new manager.
        if(!local){held.actor.nativeAwake=false;held.actor.nativeAwakeIndex=0;held.actor.nativeHeldOriginal=false;}
        if(held.actor.nativePoolSlot!=SIZE_MAX && nativePool_.live(held.actor.nativePoolSlot) &&
           nativePool_.generation(held.actor.nativePoolSlot)==held.actor.nativeGeneration &&
           (nativeRetainedHeldIdentities_.contains({held.actor.nativePoolSlot,held.actor.nativeGeneration}) ||
            (local && actors_[id].nativePoolSlot==held.actor.nativePoolSlot && actors_[id].nativeGeneration==held.actor.nativeGeneration)))continue;
        if(local) {
            // Replace a stale retained local original, rather than leaving a
            // second live pool identity for the same removed placement.
            if(actors_[id].nativeGeneration)releaseNativePoolBody(actors_[id].nativePoolSlot);else actors_[id].nativePoolSlot=SIZE_MAX;
            const auto allocation=allocateNativePoolBody();
            actors_[id].nativePoolSlot=held.actor.nativePoolSlot=allocation.slot;
            actors_[id].nativeGeneration=held.actor.nativeGeneration=allocation.generation;
        } else {
            const auto allocation=allocateNativePoolBody();held.actor.nativePoolSlot=allocation.slot;held.actor.nativeGeneration=allocation.generation;
        }
    }
    nativeRetainedHeldIdentities_.clear();rebuildNativeHeldOriginals();
}
bool GameWorld::restoreNativePool(const std::optional<NativeWorldPoolState>& saved,std::string& error) {
    if(saved) {
        // Root validates the complete snapshot before publishing any state.
        // This local restore only validates the same structural pool image.
        NativeActorPool pool;if(!pool.restore(saved->pool)){error="invalid native actor pool";return false;}
        nativePool_=std::move(pool);nativeTileBodies_=saved->tiles;
        nativePoolOriginalHistoryKnown_=saved->originalHistoryKnown;
        nextNativeGeneration_=nativePool_.nextGeneration();return true;
    }
    NativeActorPoolState rebuilt;rebuilt.nextGeneration=nextNativeGeneration_;
    const auto append=[&](std::uint32_t generation){const auto slot=rebuilt.catalog.size();
        rebuilt.catalog.push_back(slot);rebuilt.generations.push_back(generation);++rebuilt.liveCount;return slot;};
    for(auto& actor:actors_) {
        actor.nativePoolSlot=SIZE_MAX;
        if(actor.nativeGeneration || actor.alive || actor.reactivateMs>=0)actor.nativePoolSlot=append(actor.nativeGeneration);
    }
    for(auto& shot:projectiles_) {
        shot.nativePoolSlot=SIZE_MAX;
        if(!shot.visual && (shot.alive || shot.nativeGeneration))shot.nativePoolSlot=append(shot.nativeGeneration);
    }
    for(auto& held:heldItems_) {
        held.actor.nativePoolSlot=SIZE_MAX;
        const auto id=held.object.id.value;
        if(mapName(held.mapPath)==mapName(scene_->mapPath) && id<actors_.size() &&
           actors_[id].nativeGeneration==held.actor.nativeGeneration && actors_[id].nativePoolSlot!=SIZE_MAX)
            held.actor.nativePoolSlot=actors_[id].nativePoolSlot;
        else held.actor.nativePoolSlot=append(held.actor.nativeGeneration);
    }
    NativeActorPool pool;if(!pool.restore(rebuilt)){error="invalid reconstructed native actor pool";return false;}
    nativePool_=std::move(pool);nativeTileBodies_.clear();nativePoolOriginalHistoryKnown_=false;
    initializeNativeTileBodies();return true;
}
bool GameWorld::reconstructNativePoolForReplacement(const NativeWorldPoolState& seed,std::string& error,bool checkpoint) {
    return reconstructNativePoolForReplacement(NativePoolReplacementContext{seed,{}},error,checkpoint);
}
bool GameWorld::reconstructNativePoolForReplacement(const NativePoolReplacementContext& context,std::string& error,bool checkpoint) {
    const auto& seed=context.state;
    NativeActorPool pool;if(!pool.restore(seed.pool)){error="invalid replacement actor pool";return false;}
    std::set<std::size_t> cleanup;
    for(const auto slot:context.releaseOrder)if(!pool.live(slot)||!pool.generation(slot)||!cleanup.insert(slot).second) {
        error="invalid replacement cleanup order";return false;
    }
    for(std::size_t i=0;i<pool.liveCount();++i)if(pool.generation(pool.catalog()[i])!=0 && !cleanup.contains(pool.catalog()[i])) {
        error="replacement pool still contains owned live bodies";return false;
    }
    const auto previous=nativePool_.snapshot();
    std::vector<std::uint32_t> generations;generations.reserve(actors_.size());
    std::vector<std::size_t> oldSlots;oldSlots.reserve(actors_.size());
    for(const auto& actor:actors_){generations.push_back(actor.nativeGeneration);oldSlots.push_back(actor.nativePoolSlot);}
    std::vector<std::size_t> oldAmmoSlots;for(const auto& shot:projectiles_)oldAmmoSlots.push_back(shot.nativePoolSlot);
    nativePool_=std::move(pool);nativeTileBodies_.clear();nativePoolOriginalHistoryKnown_=seed.originalHistoryKnown;
    for(auto& actor:actors_)actor.nativePoolSlot=SIZE_MAX;
    for(auto& shot:projectiles_)shot.nativePoolSlot=SIZE_MAX;
    std::set<std::size_t> allocatedActors;
    std::function<void(std::size_t)> actor=[&](std::size_t id) {
        if(id>=actors_.size()||!allocatedActors.insert(id).second)return;
        //416730 writes held original records before their holder's record.
        for(auto held=heldItems_.rbegin();held!=heldItems_.rend();++held)if(held->holder==id) {
            const auto local=held->object.id.value;
            if(mapName(held->mapPath)==mapName(scene_->mapPath) && local<actors_.size() && oldSlots[local]!=SIZE_MAX &&
               generations[local]==held->actor.nativeGeneration) {
                actor(local);held->actor.nativePoolSlot=actors_[local].nativePoolSlot;
                held->actor.nativeGeneration=actors_[local].nativeGeneration;
            } else {
                const auto body=allocateNativePoolBody();held->actor.nativePoolSlot=body.slot;held->actor.nativeGeneration=body.generation;
            }
        }
        const auto body=allocateNativePoolBody();actors_[id].nativePoolSlot=body.slot;actors_[id].nativeGeneration=body.generation;
    };
    const auto ammo=[&](std::size_t id){const auto body=allocateNativePoolBody();projectiles_[id].nativePoolSlot=body.slot;projectiles_[id].nativeGeneration=body.generation;};
    if(!checkpoint) {
        // Private fresh construction already established413800/CREATE order.
        std::unordered_map<std::size_t,std::pair<bool,std::size_t>> bodies;
        for(std::size_t i=0;i<oldSlots.size();++i)if(oldSlots[i]!=SIZE_MAX)bodies[oldSlots[i]]={false,i};
        for(std::size_t i=0;i<oldAmmoSlots.size();++i)if(oldAmmoSlots[i]!=SIZE_MAX)bodies[oldAmmoSlots[i]]={true,i};
        for(std::size_t i=0;i<previous.liveCount;++i)if(const auto at=bodies.find(previous.catalog[i]);at!=bodies.end())
            if(at->second.first)ammo(at->second.second);else actor(at->second.second);
    } else {
        std::vector<CellBody> cells;
        for(std::size_t id=0;id<actors_.size();++id)if(oldSlots[id]!=SIZE_MAX && id<actorCells_.cell.size() && actorCells_.cell[id]!=SIZE_MAX)
            cells.push_back({actorCells_.cell[id],oldSlots[id],id,actorCells_.arrivalOrder[id]});
        for(std::size_t id=0;id<projectiles_.size();++id)if(oldAmmoSlots[id]!=SIZE_MAX)
            if(const auto at=bareAmmoCells_.links.find(projectiles_[id].nativeBodyIdentity);at!=bareAmmoCells_.links.end())
                cells.push_back({at->second.cell,oldAmmoSlots[id],SIZE_MAX-id-1,at->second.arrivalOrder});
        const auto xy=[&](std::size_t cell){return std::pair{cell%std::size_t(actorCells_.height),cell/std::size_t(actorCells_.height)};};
        std::sort(cells.begin(),cells.end(),[&](const auto& a,const auto& b){return xy(a.cell)!=xy(b.cell)?xy(a.cell)>xy(b.cell):a.order>b.order;});
        for(const auto& body:cells)if(body.actor<actors_.size())actor(body.actor);else ammo(SIZE_MAX-body.actor-1);
    }
    for(std::size_t id=0;id<actors_.size();++id)if(oldSlots[id]!=SIZE_MAX && actors_[id].nativePoolSlot==SIZE_MAX) {
        nativePoolOriginalHistoryKnown_=false;actor(id);
    }
    for(std::size_t id=0;id<projectiles_.size();++id)if(oldAmmoSlots[id]!=SIZE_MAX && projectiles_[id].nativePoolSlot==SIZE_MAX) {
        nativePoolOriginalHistoryKnown_=false;ammo(id);
    }
    const auto remap=[&](std::uint32_t captured,std::size_t owner) {
        if(owner>=actors_.size())return captured;
        const auto current=actors_[owner].nativeGeneration;
        if(captured==generations[owner])return current;
        // A port snapshot retains stale links omitted by original text saves.
        // Relocation preserves invalidity even if its old raw word collides
        // with the new namespace; ordinary runtime capture stays untouched.
        if(captured==current){nativePoolOriginalHistoryKnown_=false;return captured^1u;}
        return captured;
    };
    for(std::size_t id=0;id<actors_.size();++id)if(const auto owner=scene_->objects[id].owner)
        actors_[id].nativeCreatorGeneration=remap(actors_[id].nativeCreatorGeneration,owner->value);
    for(auto& shot:projectiles_) {
        shot.nativeOwnerGeneration=remap(shot.nativeOwnerGeneration,shot.owner);
        if(shot.visual && *shot.visual<actors_.size())shot.nativeGeneration=actors_[*shot.visual].nativeGeneration;
    }
    for(auto& held:heldItems_)if(mapName(held.mapPath)==mapName(scene_->mapPath) && held.object.owner)
        held.actor.nativeCreatorGeneration=remap(held.actor.nativeCreatorGeneration,held.object.owner->value);
    //41fa49 parses destination factories while the old catalog is still
    //live;41fab6 cleans that scene;41f0b0 then produces animated terrain.
    for(const auto slot:context.releaseOrder)nativePool_.release(slot);
    initializeNativeTileBodies();nextNativeGeneration_=nativePool_.nextGeneration();error.clear();return true;
}
}
