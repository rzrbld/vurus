#include "game/world.h"
#include "game/native_body_contacts.h"
#include "game/native_body_shape.h"
#include "game/properties.h"
#include <cmath>

namespace vyruz {
namespace {
uint32_t storedAge(double value) {
    if(!std::isfinite(value)||value<0)return 0;
    return static_cast<uint32_t>(std::fmod(std::trunc(value),4294967296.0));
}
}

NativeAmmoContactObject GameWorld::nativeBodyContactObject(size_t id) const {
    NativeAmmoContactObject result;
    if(id>=actors_.size()){result.identity=0;return result;}
    const auto& object=scene_->objects[id];const auto& actor=actors_[id];
    result.identity=id+1;result.subtype=nativeDamageSubtype(id);
    result.generation=actor.nativeGeneration;result.creatorGeneration=actor.nativeCreatorGeneration;
    result.ageMs=storedAge(actor.ageMs);
    if(object.owner)result.creator=object.owner->value+1;
    if(const auto* ammo=attribute(object.properties,"ammo"))result.ignoreOwner=propertyBool(ammo->children,"Ignore-owner");
    result.shape=nativeBodyContactShape(result.subtype,actor.radius);
    return result;
}

std::optional<NativeAmmoShapeTarget> GameWorld::resolveNativeBodyContact(uint64_t reference) const {
    NativeAmmoShapeTarget target;
    if(reference&NativeContactDecorBit){
        const size_t id=reference&~NativeContactDecorBit;
        if(id>=level_->nativeNavigationDecor.size())return {};
        const auto& decor=level_->nativeNavigationDecor[id];
        target.filter.identity=reference+1;target.filter.kind=decor.water?2:1;
        target.filter.shape=decor.rectangle?4:3;target.disabled=decor.disabled;
        target.minimum={decor.minX,decor.minY};target.maximum={decor.maxX,decor.maxY};return target;
    }
    if(reference&NativeContactProjectileBit){
        const auto identity=reference&NativeContactProjectileNodeMask;
        const auto found=bareAmmoIndex_.find(identity);
        if(found==bareAmmoIndex_.end()||found->second>=projectiles_.size())return {};
        const auto& shot=projectiles_[found->second];
        if(shot.alive&&!shot.visual&&shot.nativeBodyIdentity==identity){
            if(shot.radius<=0)return {};
            target.disabled=(reference&NativeContactDisabledProjectileBit)!=0 || !ammoContactEnabled(shot);
            target.filter.identity=NativeContactProjectileBit|shot.nativeBodyIdentity;
            target.filter.generation=shot.nativeGeneration;target.filter.creatorGeneration=shot.nativeOwnerGeneration;
            target.filter.subtype=0x10;target.filter.creator=shot.owner+1;
            target.filter.ageMs=storedAge(shot.ageMs);target.filter.ignoreOwner=shot.ignoreOwner;
            target.filter.shape=shot.radius<2?2:5;target.radius=shot.radius;
            // Bounds belong to the last producer update. Do not advance phase
            // or rebuild a target from a proposed source position in this query.
            const auto center=spiderSurfaceToNative(shot.position.x,shot.position.y);
            target.center={center.x,center.y};
            if(shot.nativeBareBody)target.center={shot.nativeBareBody->motion.center.x,shot.nativeBareBody->motion.center.y};
            return target;
        }
        return {};
    }
    if(reference>=actors_.size())return {};
    const size_t id=reference;const auto& actor=actors_[id];const auto& object=scene_->objects[id];
    if(!actor.alive||actor.radius<=0)return {};
    target.filter=nativeBodyContactObject(id);target.radius=actor.radius;
    target.disabled=target.filter.shape==1||actor.ammoContactDisabled||
        (((target.filter.subtype==0x13||target.filter.subtype==0x14||target.filter.subtype==0x17)&&
          (actor.gate||actor.lift||!actor.generators.empty()))&&!actor.solid)||
        ((object.flags.invisible.value_or(false)||actor.ammoHidden)&&!propertyBool(object.properties,"Keep-bb-when-invisible"));
    auto center=spiderSurfaceToNative(object.position.x,object.position.y);
    if(const auto* history=actor.nativeMotionHistory())center={history->center.x,history->center.y};
    target.center={center.x,center.y};
    if(target.filter.shape==4){const float half=actor.radius*.7071067690849304f;
        target.minimum={center.x-half,center.y-half};target.maximum={center.x+half,center.y+half};}
    return target;
}

NativeBodyContactQuery GameWorld::queryNativeBodyContacts(size_t sourceActor,ContactVelocity previous,
        ContactVelocity proposed,float radius) {
    NativeBodyContactQuery result;
    if(sourceActor>=actors_.size())return result;
    // Surface-only synthetic fixture edges have no native rectangle/segment
    // producer. Preserve the explicit fallback rather than pretending complete.
    if(level_->nativeNavigationDecor.empty()&&!level_->collisionEdges.empty())return result;
    if(!spiderNavigation_)initializeSpiderNavigation(true);
    updateSpiderNavigation(sourceActor);
    if(!spiderNavigation_||sourceActor>=spiderNavigationActors_.size()||
       spiderNavigationActors_[sourceActor]>=spiderNavigation_->objects.size())return result;
    const auto source=nativeBodyContactObject(sourceActor);
    const auto query=queryNativeAmmoShapes(*spiderNavigation_,spiderNavigationActors_[sourceActor],source,
        previous,proposed,radius,[&](uint64_t reference){return resolveNativeBodyContact(reference);},true);
    result.unsupportedShape=query.unsupportedShape;if(result.unsupportedShape)return result;
    result.used=true;result.selected=query.contact;result.closestNonContactNode=query.closestNonContactNode;
    result.hasAmmoCandidate=query.hasAmmoCandidate;
    if(!result.selected)return result;
    result.selectedShape=resolveNativeBodyContact(result.selected->reference);
    if(!result.selectedShape)return result;
    const auto& collider=*result.selectedShape;
    const auto reference=result.selected->reference;
    if(reference&NativeContactProjectileBit){
        if(!(reference&NativeContactDecorBit)){
            const auto found=bareAmmoIndex_.find(reference&NativeContactProjectileNodeMask);
            if(found!=bareAmmoIndex_.end()&&found->second<projectiles_.size()&&projectiles_[found->second].alive)result.projectile=found->second;
        }
    }else if(collider.filter.kind==3&&reference<actors_.size()){
        for(size_t n=0;n<projectiles_.size();++n)if(projectiles_[n].visual==reference){result.projectile=n;break;}
        const auto& object=scene_->objects[reference];
        if(sourceActor!=player_)if(const auto* item=attribute(object.properties,"item"))
            result.callerIgnored=propertyBool(item->children,"Only-for-player");
        if(reference==player_&&surprise_.active(SurpriseKind::Drone))result.callerIgnored=true;
        if(object.classId==scene_->objects[sourceActor].classId&&
           propertyBool(scene_->classes[object.classId.value].properties,"Ignore-same-class-objects"))result.callerIgnored=true;
    }
    if(sourceActor==player_&&surprise_.active(SurpriseKind::Drone)&&collider.filter.kind==3)result.callerIgnored=true;
    if(collider.filter.kind==2&&propertyBool(scene_->objects[sourceActor].properties,"Over-water"))result.callerIgnored=true;
    return result;
}

bool GameWorld::applyNativeActorAmmoContact(size_t incoming,
        const NativeBodyContactQuery& query,float tickMilliseconds,ContactVelocity currentCenter,
        ContactVelocity proposedCenter,ContactVelocity step,float factor) {
    NativeActorAmmoDispatch result;
    if(incoming>=actors_.size()||!query.used||!query.selected||!query.selectedShape)return false;
    if(query.callerIgnored)return false;
    const auto& collider=*query.selectedShape;
    const TextNode* ammo=nullptr;std::optional<size_t> visual;const Projectile* liveShot=nullptr;
    if(query.projectile&&*query.projectile<projectiles_.size()){
        liveShot=&projectiles_[*query.projectile];visual=liveShot->visual;
        ammo=attribute(visual?scene_->objects[*visual].properties:scene_->classes[liveShot->classId.value].properties,"ammo");
    }else if(collider.filter.kind==3&&query.selected->reference<actors_.size()){
        visual=static_cast<size_t>(query.selected->reference);
        ammo=attribute(scene_->objects[*visual].properties,"ammo");
    }
    //4406e0 common caller: selected unmanaged kind3 wakes after the caller's
    //ignore gates, before Ammo flags/Hit/late owner dispatch. Queries stay pure.
    if(collider.filter.kind==3) {
        if(visual)wakeNativeActor(*visual);
        else if(query.projectile && *query.projectile<projectiles_.size()) {
            auto& selectedShot=projectiles_[*query.projectile];
            if(selectedShot.nativeBareBody && !selectedShot.nativeBareBody->awake &&
               !nativeClassManaged(scene_->classes[selectedShot.classId.value].properties))
                enrollNativeAmmo(selectedShot);
        }
    }
    NativeActorAmmoAttribute first{0xb,0};
    if(ammo)first.flags=(propertyBool(ammo->children,"on-hit")?1u:0u)|
        (propertyBool(ammo->children,"on-object-hit")?2u:0u)|
        (propertyBool(ammo->children,"Ignore-owner")?4u:0u);
    const auto flags=nativeDamageFlags(incoming);
    result=nativeActorAmmoDispatch(nativeBodyContactObject(incoming),collider.filter,flags,
        ammo?std::span<const NativeActorAmmoAttribute>(&first,1):std::span<const NativeActorAmmoAttribute>{});
    actors_[incoming].nativeHit=(result.sourceFlags&0x200u)!=0;
    if(result.action!=NativeActorAmmoAction::ImpactAndAbortActor)return false;
    //Install the caller's state BEFORE impact can add its own impulse, damage
    //or FX. Neither this branch nor the abort refreshes cached magnitude.
    auto& actor=actors_[incoming];
    const uint32_t behavior=(incoming==player_?3u:actor.brain.tracker?2u:actor.brain.seeker?1u:0u)|
        (actor.brain.base==BrainBase::Patrol?0xcu:0u);
    const auto velocity=nativeActorAmmoAbortVelocity(nativeDamageSubtype(incoming),behavior,
        currentCenter,proposedCenter,step,factor);
    if(incoming==player_){motion_.velocityX=velocity.x;motion_.velocityZ=velocity.z;}
    else actor.contactVelocity=velocity;
    const auto previous=spiderNativeToSurface(currentCenter.x,currentCenter.z);
    if(auto* history=actor.nativeMotionHistory())history->previousSurface={previous.x,previous.y};
    actor.nativeAmmoContinuation=true;
    // Copy before damage/FX/EXPL can reallocate either world vector. Authored
    // stationary Ammo has a native body but no emitted Projectile in this port.
    Projectile shot;
    if(liveShot)shot=*liveShot;
    else if(visual){
        const auto& object=scene_->objects[*visual];shot.visual=visual;shot.classId=object.classId;
        shot.owner=object.owner?object.owner->value:SIZE_MAX;shot.position=object.position;
        shot.nativeGeneration=actors_[*visual].nativeGeneration;shot.nativeOwnerGeneration=actors_[*visual].nativeCreatorGeneration;
        shot.ageMs=actors_[*visual].ageMs;shot.radius=actors_[*visual].radius;
        shot.damage=propertyNumber(ammo->children,"damage");shot.damageRadius=propertyNumber(ammo->children,"damage-zone",1);
        shot.hitEffect=propertyText(ammo->children,"hit-fx");
        const auto heading=nativeHeading(actors_[*visual].direction);shot.directionX=heading.x;shot.directionZ=heading.y;
    }else return false; // Resolver guarantees a body for admitted live Ammo.
    //43d86b..43d879: source CREATE runs at its existing age/current/previous
    //surface before contact relocation. An actor consuming Ammo does not
    //refresh that Ammo's motion history as a projectile visit would.
    ContentPosition createPrevious=shot.position;
    if(shot.nativeBareBody)createPrevious=shot.nativeBareBody->motion.previousSurface;
    if(visual)if(const auto* history=actors_[*visual].nativeMotionHistory())createPrevious=history->previousSurface;
    beginAmmoImpact(shot,incoming,true);
    if(liveShot)updateProjectileCreates(shot,createPrevious);
    else if(visual)updateActorCreates(*visual,createPrevious);
    const auto current=spiderSurfaceToNative(shot.position.x,shot.position.y);
    NativeNavPoint radialCenter{current.x,current.y};
    if(shot.nativeBareBody)radialCenter=shot.nativeBareBody->motion.center;
    if(visual)if(const auto* history=actors_[*visual].nativeMotionHistory())radialCenter=history->center;
    const auto point=spiderNativeToSurface(query.selected->point.x,query.selected->point.z);
    shot.position={point.x,point.y};
    //43d7d0 retains the indefinite surface only until removal. Keep that raw
    //point in the transient impact copy for native cell/FX decisions; never
    //persist a NaN in a live object or checkpoint.
    const ContentPosition finitePosition{std::isfinite(point.x)?point.x:0.f,std::isfinite(point.y)?point.y:0.f};
    if(query.projectile&&*query.projectile<projectiles_.size()){
        auto& consumed=projectiles_[*query.projectile];consumed.position=finitePosition;synchronizeBareAmmoCell(consumed);
    }
    if(visual){scene_->objects[*visual].position=finitePosition;synchronizeActorCell(*visual);}
    applyAmmoRadialImpact(shot,radialCenter,tickMilliseconds);
    finishAmmoImpact(shot,incoming,true);
    //43d7d0 removes only after radial and direct phases. Re-find the body by
    //stable identity because the final callback/FX may grow or clear the vector.
    if(shot.nativeBodyIdentity){
        const auto found=bareAmmoIndex_.find(shot.nativeBodyIdentity);
        if(found!=bareAmmoIndex_.end()&&found->second<projectiles_.size()){
            auto& consumed=projectiles_[found->second];releaseNativeProjectile(consumed);consumed.alive=false;updateSpiderProjectileNavigation(consumed);
        }
    }else if(visual){
        for(auto& consumed:projectiles_)if(consumed.visual==visual&&consumed.nativeGeneration==shot.nativeGeneration){
            releaseNativeProjectile(consumed);consumed.alive=false;updateSpiderProjectileNavigation(consumed);break;
        }
    }
    if(visual)destroy(*visual,shot.owner<actors_.size()?std::optional<size_t>(shot.owner):std::nullopt);
    return true;
}
}
