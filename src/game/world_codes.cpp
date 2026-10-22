#include "game/world.h"
#include "game/properties.h"
#include <algorithm>
#include <cstdlib>

namespace vyruz {
namespace {
std::string normalizedMap(std::string path) {
    path=folded(path);std::replace(path.begin(),path.end(),'\\','/');return path;
}
bool matches(const KeyPiece& key,const ObjectPlacement& object) {
    const auto* k=attribute(object.properties,"key");
    return k && propertyBool(k->children,"Is-code") && int(propertyNumber(k->children,"Id"))==key.id &&
        int(propertyNumber(k->children,"Code-part-id",1))==key.part &&
        int(propertyNumber(k->children,"Code-part-count",1))==key.total && propertyText(k->children,"Code-String")==key.text;
}
std::string formatInteger(const std::string& format,int number) {
    std::string result;
    for(size_t i=0;i<format.size();++i) {
        if(format[i]=='%' && i+1<format.size()) {
            if(format[i+1]=='d'){result+=std::to_string(number);++i;continue;}
            if(format[i+1]=='%'){result+='%';++i;continue;}
        }
        result+=format[i];
    }
    return result;
}
}
HeldCode GameWorld::retainCode(size_t index) const {
    HeldCode held;held.object=scene_->objects[index];held.mapPath=scene_->mapPath;
    held.className=scene_->classes[held.object.classId.value].name;
    const auto* item=attribute(held.object.properties,"item");
    const auto* cls=attribute(scene_->classes[held.object.classId.value].properties,"item");
    held.remainingMs=actors_[index].itemTimeoutMs.value_or(item?propertyNumber(item->children,"Timeout"):0);
    held.classTimeoutMs=cls?propertyNumber(cls->children,"Timeout"):0;
    return held;
}
void GameWorld::migrateHeldCodes() {
    std::set<size_t> used;
    for(const auto& key:player().heldKeys)if(key.held && normalizedMap(key.held->mapPath)==normalizedMap(scene_->mapPath))used.insert(key.held->object.id.value);
    for(auto& key:actors_[player_].heldKeys)if(key.code && !key.held) {
        std::optional<size_t> found;bool ambiguous=false;
        for(size_t i=0;i<actors_.size();++i)if(!actors_[i].alive && !used.contains(i) && matches(key,scene_->objects[i])) {
            if(found){ambiguous=true;break;}found=i;
        }
        if(found && !ambiguous) {
            key.held=retainCode(*found);used.insert(*found);actors_[*found].reactivateMs=-1;
            diagnostics_.push_back("Legacy held code recovered from unique dead item; elapsed timeout unavailable, restarted authored timer");
        } else diagnostics_.push_back("Legacy held code has no unique source identity; preserved untimed inventory entry");
    }
    for(const auto& key:player().heldKeys)if(key.held && normalizedMap(key.held->mapPath)==normalizedMap(scene_->mapPath)) {
        const auto i=key.held->object.id.value;
        if(i<actors_.size() && matches(key,scene_->objects[i])){actors_[i].alive=false;actors_[i].reactivateMs=-1;synchronizeActorCell(i);}
    }
}
void GameWorld::updateHeldCodes(float milliseconds) {
    // 4406e0: player-held timers run before movement/contacts, even in TimeWarp.
    // Port vectors append pickups; native held lists prepend. Reverse traversal
    // reproduces that scan; the oldest matching part1 owns the timer.
    std::vector<int> scans;
    for(auto it=player().heldKeys.rbegin();it!=player().heldKeys.rend();++it)if(it->code && it->part==1)scans.push_back(it->id);
    for(int id:scans) {
        auto& keys=actors_[player_].heldKeys;
        size_t count=0,total=0;std::optional<size_t> first;bool valid=true;
        for(size_t i=0;i<keys.size();++i)if(keys[i].code && keys[i].id==id) {
            if(total && total!=size_t(keys[i].total))valid=false;
            total=keys[i].total;++count;if(keys[i].part==1 && !first)first=i;
        }
        if(!valid || count!=total || !first || !keys[*first].held)continue;
        auto& held=*keys[*first].held;
        const auto* item=attribute(held.object.properties,"item");if(!item)continue;
        if(folded(propertyText(item->children,"Surprise-type","none"))!="none" && !propertyText(item->children,"Surprise-type").empty())continue;
        if(held.remainingMs>0)held.remainingMs=std::max(0.f,held.remainingMs-milliseconds);
        for(auto& retained:heldItems_)if(retained.holder==player_ && normalizedMap(retained.mapPath)==normalizedMap(held.mapPath) && retained.object.id==held.object.id)
            retained.actor.itemTimeoutMs=held.remainingMs;
        auto notice=[&](const HeldCode& piece,const std::string& text) {
            ContentPosition p{};const auto* attr=attribute(piece.object.properties,"item");
            if(attr)if(const auto* n=contentProperty(attr->children,"Warn-message-position")) {
                if(n->values.size()>0)p.x=std::strtof(n->values[0].c_str(),nullptr);
                if(n->values.size()>1)p.y=std::strtof(n->values[1].c_str(),nullptr);
            }
            notifications_.add({NoticeKind::Pickup,text,p.x,p.y,0,2000,0,false,false,6,7});
        };
        const int seconds=static_cast<int>(held.remainingMs/1000),cadence=static_cast<int>(propertyNumber(item->children,"Warn-timeout")/1000);
        if(seconds>0 && cadence>0 && seconds%cadence==0)
            notice(held,formatInteger(propertyText(item->children,"Warn-message-format"),seconds));
        if(!propertyBool(item->children,"Has-timeout") || held.remainingMs!=0)continue;
        const bool returns=propertyNumber(item->children,"Reactivate-after")>0;
        const bool random=propertyBool(item->children,"Random-Location");
        std::vector<KeyPiece> expired;
        for(auto it=keys.rbegin();it!=keys.rend();++it)if(it->code && it->id==id)expired.push_back(*it);
        std::erase_if(keys,[&](const auto& key){return key.code && key.id==id;});
        for(const auto& key:expired) {
            if(!key.held)continue;
            std::optional<HeldItem> retained;
            for(auto held=heldItems_.begin();held!=heldItems_.end();) {
                if(held->holder!=player_ || normalizedMap(held->mapPath)!=normalizedMap(key.held->mapPath) ||
                   held->object.id!=key.held->object.id){++held;continue;}
                if(!returns)releaseNativeHeldItem(*held);
                else retained=*held;
                held=heldItems_.erase(held);
            }
            const auto& source=*key.held;
            std::optional<size_t> target;
            if(normalizedMap(source.mapPath)==normalizedMap(scene_->mapPath) && source.object.id.value<actors_.size() &&
                !actors_[source.object.id.value].alive && matches(key,scene_->objects[source.object.id.value]))target=source.object.id.value;
            if(!returns){if(target){actors_[*target].reactivateMs=-1;actors_[*target].vitals.energy=0;markSpatialDirty(*target);}continue;}
            //4398db reinserts the retained original; return is not a factory
            //birth. Foreign carry resolves class IDs without consuming a UID.
            if(!target && retained) {
                target=materializeHeldItem(*retained,source.object.position);
                if(!target){releaseNativeHeldItem(*retained);continue;}
            }
            if(!target) {
                const auto* cls=findClass(*scene_,source.className);
                if(!cls){diagnostics_.push_back("Cannot return carried code: source class missing "+source.className);continue;}
                auto object=source.object;object.id={static_cast<uint32_t>(scene_->objects.size())};object.classId=cls->id;object.owner.reset();
                target=scene_->objects.size();scene_->objects.push_back(std::move(object));actors_.push_back(makeActor(scene_->objects.back()));
                registerTransientRecordCandidate(*target);
                initializeNativeIdentity(*target);
            }
            auto& object=scene_->objects[*target];auto& actor=actors_[*target];
            object.position=source.object.position;
            if(random) {
                std::vector<size_t> cells;
                for(size_t i=0;i<level_->collisionMask.size();++i)if(level_->collisionMask[i]==0xff)cells.push_back(i);
                if(!cells.empty()) {
                    const auto cell=cells[std::uniform_int_distribution<size_t>(0,cells.size()-1)(random_)];
                    object.position={float((cell%level_->mapW)*64+32),float((cell/level_->mapW)*32+16)};
                } else diagnostics_.push_back("Random code return has no exact 0xff cell; retained pickup position");
            }
            actor.alive=true;actor.nativeHeldOriginal=false;actor.reactivateMs=-1;actor.pickupDelayMs=0;actor.itemTimeoutMs=source.classTimeoutMs;
            object.flags.invisible=false;
            if(!actor.nativeAwake)enrollNativeActor(*target); //439937..439941.
            synchronizeActorCell(*target);
            if(key.part==1)notice(source,"Code #"+std::to_string(id)+" regenerated !");
        }
    }
}
}
