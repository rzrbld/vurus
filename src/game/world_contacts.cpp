#include "game/world.h"
#include "game/ammo_contact.h"
#include "game/properties.h"
#include <algorithm>
#include <cmath>
namespace vyruz {
bool GameWorld::ammoContactEnabled(const Projectile& shot) const {
    if(!shot.alive)return false;
    if(!shot.phaseInitialized)return shot.ageMs>=shot.activeAfterMs;
    if(shot.inactiveMs==0)return true;
    return shot.phaseHidden && (shot.phaseConfigReady?shot.keepBoundsWhenHidden:
        propertyBool(scene_->classes[shot.classId.value].properties,"Keep-bb-when-invisible"));
}
bool GameWorld::updateAmmoActivation(Projectile& shot,float ms) {
    if(!shot.phaseConfigReady){
        const auto& properties=scene_->classes[shot.classId.value].properties;
        const auto* on=contentProperty(properties,"Random-on-timeout");
        const auto* off=contentProperty(properties,"Random-off-timeout");
        const auto number=[](const TextNode* n,const char* field){return n?propertyNumber(n->children,field):0.f;};
        shot.onInitial=number(on,"Initial");shot.onMinimum=number(on,"Min");shot.onMaximum=number(on,"Max");
        shot.offInitial=number(off,"Initial");shot.offMinimum=number(off,"Min");shot.offMaximum=number(off,"Max");
        shot.keepBoundsWhenHidden=propertyBool(properties,"Keep-bb-when-invisible");shot.phaseConfigReady=true;
    }
    if(!shot.phaseInitialized){
        // Old checkpoints lack activation state. Reconstruct the initial arming
        // phase; v6 checkpoints retain subsequent phases exactly.
        shot.inactiveMs=shot.activeAfterMs>0?std::max(0.f,shot.activeAfterMs-(shot.ageMs-ms)):shot.offInitial;
        shot.onMs=shot.activeAfterMs>0?0:shot.onInitial;
        shot.damageRemainingMs=shot.fuseMs>0?std::max(0.f,shot.fuseMs-std::max(0.f,shot.ageMs-ms-shot.activeAfterMs)):0;
        shot.phaseInitialized=true;
    }
    const auto randomTimer=[&](float minimum,float maximum){
        const int low=static_cast<int>(minimum),high=static_cast<int>(maximum);
        //413a31/413a3d give every constructed body these native seeds.
        //440b58..440bb4 and440bff..440c58 install them for one4540c0
        //draw, then restore the outer stream without advancing the body seeds.
        NativeRandomState bodyRandom;
        return static_cast<float>(bodyRandom.inclusive(low,std::max(low,high)));
    };
    const auto visible=[&]{
        if(!shot.visual || *shot.visual>=actors_.size())return;
        auto& actor=actors_[*shot.visual];
        actor.ammoHidden=shot.phaseHidden;
        actor.ammoContactDisabled=!ammoContactEnabled(shot);
        actor.ammoFadeClockMs+=ms;
        const float steps=std::floor(actor.ammoFadeClockMs/14.f);
        actor.ammoFadeClockMs-=steps*14.f;
        actor.ammoVisibility=std::clamp(actor.ammoVisibility+(shot.phaseHidden?-steps:steps),0.f,49.f);
        actor.ammoAlpha=shot.phaseHidden || (shot.onMs>0 && shot.onMs<800);
    };
    if(shot.inactiveMs!=0){
        if(shot.inactiveMs<0){visible();return false;}
        shot.inactiveMs-=ms;
        if(shot.inactiveMs>0){visible();return false;}
        shot.inactiveMs=0;shot.onMs=0;shot.phaseHidden=false;
        if(shot.onMaximum!=0)shot.onMs=randomTimer(shot.onMinimum,shot.onMaximum);
    }
    // Native explicitly uses <0, not <=0: an exactly-zero on timer stops.
    if(shot.onMs>0 && (shot.onMs-=ms)<0){
        shot.onMs=0;shot.inactiveMs=-1;shot.phaseHidden=true;
        if(shot.offMaximum!=0)shot.inactiveMs=randomTimer(shot.offMinimum,shot.offMaximum);
        visible();return false;
    }
    visible();
    if(shot.damageRemainingMs>0)shot.damageRemainingMs=std::max(0.f,shot.damageRemainingMs-ms);
    return true;
}
void GameWorld::recordAmmoContact(std::optional<size_t> owner,std::optional<size_t> target) {
    //43d7d0 counts direct player-owned Ammo contact, even when no damage gets
    //through. Fuse expiry, disposal and explosion victims are separate paths.
    if(owner!=player_)return;
    bool hit=false;
    if(target && *target<actors_.size()) {
        const auto& actor=actors_[*target];
        hit=actor.hudEnemy || (!actor.indestructible && objectSubtype(scene_->objects[*target])=="item");
    }
    if(hit)++ammoHits_;else ++ammoMisses_;
}
void GameWorld::contactAmmo(size_t incoming,ContentPosition from,ContentPosition to) {
    if(from.x==to.x && from.y==to.y)return;
    if(incoming>=actors_.size() || !actors_[incoming].alive || actors_[incoming].radius<2)return;
    if(incoming!=player_ && !actors_[incoming].enemy)return;
    const auto a=contactNativePoint(from.x,from.y),b=contactNativePoint(to.x,to.y);
    std::vector<std::pair<double,size_t>> contacts;
    for(const auto i:contactMines_){
        if(i==incoming || !actors_[i].alive || actors_[i].radius<2 || !actors_[i].contactMine)continue;
        const Projectile* shot=nullptr;
        for(const auto& p:projectiles_)if(p.visual==i){shot=&p;break;}
        if(shot && (!ammoContactEnabled(*shot) || (incoming==shot->owner && (shot->ageMs<500 || shot->ignoreOwner))))continue;
        const auto q=scene_->objects[i].position;
        const auto hit=nativeAmmoCircleContact(a,b,actors_[incoming].radius,contactNativePoint(q.x,q.y),actors_[i].radius);
        if(hit)contacts.emplace_back(std::hypot(hit->x-b.x,hit->z-b.z),i);
    }
    std::stable_sort(contacts.begin(),contacts.end());
    for(const auto& [distance,id]:contacts){
        (void)distance;
        if(std::none_of(ammoContactVictims_.begin(),ammoContactVictims_.end(),[&](const auto& contact){return contact.first==id;}))ammoContactVictims_.emplace_back(id,incoming);
    }
}
void GameWorld::resolveAmmoContacts(){
    auto victims=std::move(ammoContactVictims_);ammoContactVictims_.clear();
    for(const auto [id,incoming]:victims){
        if(id>=actors_.size() || !actors_[id].alive)continue;
        std::optional<size_t> owner;
        for(auto& shot:projectiles_)if(shot.visual==id){shot.alive=false;owner=shot.owner;break;}
        auto statsOwner=owner;
        if(!statsOwner && scene_->objects[id].owner)statsOwner=scene_->objects[id].owner->value;
        recordAmmoContact(statsOwner,incoming);
        applyAmmoImpulse(incoming,actors_[id].direction);
        // Authored/fired mines have zero Ammo damage: destruction transfers
        // TYPE Explosion onto Boom-fx, whose own native timer applies damage.
        destroy(id,owner);
    }
}
}
