#include "game/world.h"
#include "game/death_drops.h"
#include "game/properties.h"
#include <algorithm>
namespace vyruz {
namespace {
std::string mapKey(std::string path){path=folded(path);std::replace(path.begin(),path.end(),'\\','/');return path;}
bool sameSource(const HeldItem& item,const HeldCode& code){return mapKey(item.mapPath)==mapKey(code.mapPath)&&item.object.id==code.object.id;}
}
void GameWorld::rebuildNativeHeldOriginals() {
    resetSpatialPublications();
    for(auto& actor:actors_)actor.nativeHeldOriginal=false;
    for(auto& held:heldItems_) {
        const auto id=held.object.id.value;
        if(mapKey(held.mapPath)==mapKey(scene_->mapPath) && id<actors_.size() &&
           actors_[id].nativePoolSlot!=SIZE_MAX && actors_[id].nativePoolSlot==held.actor.nativePoolSlot &&
           actors_[id].nativeGeneration==held.actor.nativeGeneration)actors_[id].nativeHeldOriginal=true;
    }
}
void GameWorld::retainItem(size_t id,bool surprise) {
    actors_[id].nativeHeldOriginal=true;
    markSpatialDirty(id);
    heldItems_.push_back({scene_->objects[id],actors_[id],scene_->mapPath,
        scene_->classes[scene_->objects[id].classId.value].name,player_,surprise});
}
void GameWorld::migrateHeldItems() {
    // Only recover legacy entries with an explicit retained source. A dead
    // Carry decoration alone cannot distinguish collection from destruction.
    for(const auto& key:player().heldKeys)if(key.held &&
        std::none_of(heldItems_.begin(),heldItems_.end(),[&](const auto& h){return sameSource(h,*key.held);})) {
        const auto& source=*key.held;
        if(mapKey(source.mapPath)!=mapKey(scene_->mapPath) || source.object.id.value>=actors_.size())continue;
        const auto id=source.object.id.value;
        if(actors_[id].alive || !contentNameEqual(scene_->classes[scene_->objects[id].classId.value].name,source.className))continue;
        auto actor=actors_[id];actor.alive=true;
        heldItems_.push_back({source.object,std::move(actor),source.mapPath,source.className,player_,false});
    }
    for(const auto& held:heldItems_)if(mapKey(held.mapPath)==mapKey(scene_->mapPath) && held.object.id.value<actors_.size()) {
        const auto id=held.object.id.value;
        if(contentNameEqual(scene_->classes[scene_->objects[id].classId.value].name,held.className)) {
            actors_[id].alive=false;actors_[id].reactivateMs=-1;actors_[id].nativeHeldOriginal=true;
            synchronizeActorCell(id);
        }
    }
}
std::optional<size_t> GameWorld::materializeHeldItem(const HeldItem& held,ContentPosition position) {
    const auto* cls=findClass(*scene_,held.className);
    if(!cls){diagnostics_.push_back("Cannot release held class in this map: "+held.className);return {};}
    size_t id=scene_->objects.size();
    const bool local=mapKey(held.mapPath)==mapKey(scene_->mapPath);
    if(local && held.object.id.value<actors_.size() && !actors_[held.object.id.value].alive &&
       scene_->objects[held.object.id.value].classId==cls->id)id=held.object.id.value;
    if(id==scene_->objects.size()) {
        if(id>=100000)return {};
        scene_->objects.emplace_back();actors_.emplace_back();
    }
    scene_->objects[id]=held.object;auto& object=scene_->objects[id];
    object.id={static_cast<uint32_t>(id)};object.classId=cls->id;object.position=position;
    if(!local)object.owner.reset();
    actors_[id]=held.actor;actors_[id].alive=true;actors_[id].reactivateMs=-1;actors_[id].nativeHeldOriginal=false;
    actors_[id].nativeSubtype=nativeObjectSubtype(object);
    actors_[id].hasAmmoAttribute=attribute(object.properties,"ammo")!=nullptr;
    actors_[id].visibleWhenFiring=propertyBool(object.properties,"Visible-when-firing");
    registerTransientRecordCandidate(id);
    if(!local) {
        // Awake indices are context-local even though the retained body/UID
        //survives carry. Never reuse an index captured in the source World.
        actors_[id].nativeAwake=false;actors_[id].nativeAwakeIndex=0;
    }
    if(!actors_[id].createsInitialized)configureActorCreates(actors_[id],object,true);
    else if(!local) {
        // Class IDs are map-local; re-resolve products using retained authored
        // names, preserving each record's timer and prior trail point.
        const auto retained=actors_[id].creates;
        configureActorCreates(actors_[id],object,true);
        if(retained.size()==actors_[id].creates.size())for(size_t n=0;n<retained.size();++n) {
            const auto product=actors_[id].creates[n].classId;
            actors_[id].creates[n]=retained[n];actors_[id].creates[n].classId=product;
        }
    }
    ++actors_[id].visualRevision;
    synchronizeActorCell(id);
    return id;
}
bool GameWorld::consumeHeldKey(size_t holder,int key) {
    const auto pieces=actors_[holder].heldKeys.size();
    bool removed=actors_[holder].keys.erase(key)>0;
    consumeKey(actors_[holder].heldKeys,key);
    removed=removed || actors_[holder].heldKeys.size()!=pieces;
    std::vector<HeldItem> consumed;
    for(size_t i=heldItems_.size();i-->0;) {
        const auto* attr=attribute(heldItems_[i].object.properties,"key");
        if(heldItems_[i].holder!=holder || !attr || int(propertyNumber(attr->children,"Id"))!=key)continue;
        consumed.push_back(std::move(heldItems_[i]));heldItems_.erase(heldItems_.begin()+i);
    }
    removed=removed || !consumed.empty();
    for(const auto& item:consumed)if(auto id=materializeHeldItem(item,item.object.position))destroy(*id,std::nullopt);
    return removed;
}
void GameWorld::releaseHeldItems(size_t holder,DeathDropScatter& scatter) {
    const auto position=scene_->objects[holder].position;
    std::vector<HeldItem> released;
    for(size_t i=heldItems_.size();i-->0;)if(heldItems_[i].holder==holder) {
        released.push_back(std::move(heldItems_[i]));heldItems_.erase(heldItems_.begin()+i);
    }
    actors_[holder].heldKeys.clear();actors_[holder].keys.clear();scatter.beginList();
    for(const auto& item:released)materializeHeldItem(item,scatter.next(position,true));
}
}
