#include "game/world.h"
#include "game/grenade_contact_native.h"
#include "game/ammo_collision_native.h"
#include "game/hit_latch_native.h"
#include "game/properties.h"
#include <algorithm>
#include <limits>
namespace vyruz {
GrenadeNativeContactResult GameWorld::updateNativeGrenadeContacts(Projectile& shot,
        ProjectileDisplacement delta,float seconds) {
    GrenadeNativeContactResult result;
    const auto& cls=scene_->classes[shot.classId.value];
    if(!contentNameIs(cls.name,"grenade-ammo") || !shot.visual || *shot.visual>=actors_.size() || !shot.verticalBounce)return result;
    // Explicit synthetic surface edges have no original native graph primitives.
    // Keep their accepted response fixture rather than inventing diagonal shapes.
    if(level_->nativeNavigationDecor.empty() && !level_->collisionEdges.empty())return result;
    result.used=true;const size_t visual=*shot.visual;const auto from=shot.position;
    markSpatialDirty(visual);
    const auto initialHistory=actors_[visual].nativeMotion;
    const int initialCommand=actors_[visual].enemyMoveCommand;
    result.proposedX=from.x;result.proposedY=from.y;
    if(!actors_[visual].nativeMotion){
        const auto p=spiderSurfaceToNative(from.x,from.y);
        actors_[visual].nativeMotion=SpiderMotionHistory{{p.x,p.y},from};
        actors_[visual].enemyMoveCommand=200;
    }
    const auto saved=*actors_[visual].nativeMotion;
    const ContactVelocity old{saved.center.x,saved.center.y};
    if(delta.x==0 && delta.z==0){
        actors_[visual].nativeMotion->previousSurface=from;
        shot.motion.velocityX=shot.motion.velocityZ=0;
        updateSpiderNavigation(visual);return result;
    }
    //44175b/4417d8: proposed native center and surface position are independent.
    const ContactVelocity trial{static_cast<float>(static_cast<long double>(old.x)+delta.x),
                                   static_cast<float>(static_cast<long double>(old.z)+delta.z)};
    const auto projected=spiderNativeToSurface(trial.x,trial.z);
    const ContentPosition proposedSurface{projected.x,projected.y};
    result.proposedX=proposedSurface.x;result.proposedY=proposedSurface.y;
    //41b4b0 rebuilds proposed bounds from the just-written surface coordinate,
    //overwriting trial native center before4105d0 and velocity installation.
    const auto rebuilt=spiderSurfaceToNative(proposedSurface.x,proposedSurface.y);
    const ContactVelocity proposed{rebuilt.x,rebuilt.y};
    if(proposedSurface.x<1 || proposedSurface.y<1 || proposedSurface.x>=level_->mapW*64.f || proposedSurface.y>=level_->mapH*32.f){
        shot.alive=false;actors_[visual].alive=false;actors_[visual].solid=false;updateSpiderNavigation(visual);return result;
    }
    if(!spiderNavigation_)initializeSpiderNavigation(true);
    updateSpiderNavigation(visual);
    if(!spiderNavigation_ || visual>=spiderNavigationActors_.size() ||
       spiderNavigationActors_[visual]>=spiderNavigation_->objects.size()){
        // Missing/disabled source bounds: original skips contact traversal.
        shot.position=proposedSurface;actors_[visual].nativeMotion->center={proposed.x,proposed.z};
        const auto previous=spiderNativeToSurface(old.x,old.z);
        actors_[visual].nativeMotion->previousSurface={previous.x,previous.y};
        scene_->objects[visual].position=shot.position;updateSpiderNavigation(visual);return result;
    }
    NativeAmmoContactObject source=nativeBodyContactObject(visual);
    source.creator=shot.owner+1;source.subtype=0x10;
    source.ageMs=static_cast<uint32_t>(std::min<double>(shot.ageMs,std::numeric_limits<uint32_t>::max()));
    const auto resolve=[&](uint64_t reference){return resolveNativeBodyContact(reference);};
    const auto query=queryNativeAmmoShapes(*spiderNavigation_,spiderNavigationActors_[visual],source,old,proposed,shot.radius,resolve,true);
    if(query.unsupportedShape){
        actors_[visual].nativeMotion=initialHistory;actors_[visual].enemyMoveCommand=initialCommand;
        updateSpiderNavigation(visual);return {};
    }
    // All original terrain and ordinary target shapes above are supported. The
    // graph's route points/Ammo siblings are separately filtered, never swept.
    const auto selected=query.contact;
    bool ignore=false;std::optional<NativeAmmoShapeTarget> collider;
    if(selected){collider=resolve(selected->reference);
        if(collider && collider->filter.kind==3){const size_t id=selected->reference;
            const auto& object=scene_->objects[id];
            if(const auto* item=attribute(object.properties,"item"))ignore=propertyBool(item->children,"Only-for-player");
            if(id==player_ && surprise_.active(SurpriseKind::Drone))ignore=true;
            if(object.classId==shot.classId && propertyBool(cls.properties,"Ignore-same-class-objects"))ignore=true;
        }else if(collider && collider->filter.kind==2)ignore=propertyBool(cls.properties,"Over-water");
    }
    //4419e3 installs per-tick displacement before contact; clear movement
    //unscales it again at441d11. The magnitude cache remains438b20's value.
    const ContactVelocity provisional{proposed.x-old.x,proposed.z-old.z};
    const float factor=14.f/(seconds*1000.f);
    const auto previousSurface=spiderNativeToSurface(old.x,old.z);
    bool committed=false;
    if(!selected || !collider || ignore){
        shot.position=proposedSurface;shot.motion.velocityX=provisional.x*factor;shot.motion.velocityZ=provisional.z*factor;
        actors_[visual].nativeMotion->center={proposed.x,proposed.z};
        actors_[visual].nativeMotion->previousSurface={previousSurface.x,previousSurface.y};
        committed=true;
    }else if(collider->filter.kind==3){
        const auto hitActor=static_cast<size_t>(selected->reference);
        if(hitActor<actors_.size()) {
            wakeNativeActor(hitActor);
            actors_[hitActor].nativeHit=(nativeHitLatchSet(nativeDamageFlags(hitActor))&0x200u)!=0;
        }
        //43d7d0 receives the selected native impact coordinate without another
        //circle calculation, including square Gate/Lift and nonenemy Items.
        const auto hit=spiderNativeToSurface(selected->point.x,selected->point.z);result.impactSurface=ContentPosition{hit.x,hit.y};
        shot.position={std::isfinite(hit.x)?hit.x:0.f,std::isfinite(hit.y)?hit.y:0.f};
        result.terminal=true;result.target=selected->reference;
    }else{
        //4449f0 restores projected old position and the saved earlier surface
        //history. Return2 later commits native bounds while surface stays here.
        shot.position={previousSurface.x,previousSurface.y};
        auto effective=provisional;const int command=actors_[visual].enemyMoveCommand;
        const bool moved=shot.position.x!=saved.previousSurface.x || shot.position.y!=saved.previousSurface.y;
        if(!moved && command!=0){const auto heading=nativeHeading(actors_[visual].direction);
            effective={heading.x*shot.motion.thrust,heading.y*shot.motion.thrust};}
        const ContactVelocity inward{selected->point.x-old.x,selected->point.z-old.z};
        const float impact=shot.motion.magnitude*shot.motionConfig.weight*.1f;
        if(std::abs(delta.x)+std::abs(delta.z)>2){
            spawnProjectileBounceEffect(shot,shot.position,{selected->point.x,selected->point.z});
            actors_[visual].vitals.shield=std::max(0.f,actors_[visual].vitals.shield-impact);
        }
        const bool separating=!spiderContactAccepted(effective,inward);
        if(separating){
            actors_[visual].nativeMotion->center={proposed.x,proposed.z};
            committed=true;
            shot.motion.velocityX=provisional.x;shot.motion.velocityZ=provisional.z;
        }else{
            ContactVelocity normal;
            const ContactVelocity lo{collider->minimum.x-old.x,collider->minimum.z-old.z};
            const ContactVelocity hi{collider->maximum.x-old.x,collider->maximum.z-old.z};
            if(collider->filter.shape==4)normal=spiderRectangleNormal(lo,hi,inward,shot.radius);
            else normal=lo.x==hi.x?ContactVelocity{lo.x>=0?shot.radius:-shot.radius,0}:
                                       ContactVelocity{0,lo.z>=0?shot.radius:-shot.radius};
            auto response=moved || command!=0?resolveNativeTerrainContact(effective,normal,shot.motion.thrust,shot.bounceElasticity,command!=0):SpiderTerrainResponse{};
            shot.motion.velocityX=response.applied?response.velocity.x:provisional.x;
            shot.motion.velocityZ=response.applied?response.velocity.z:provisional.z;
            if(response.clearCommand)actors_[visual].enemyMoveCommand=0;
        }
        if(!separating && (shot.motion.velocityX!=0 || shot.motion.velocityZ!=0)){
            const int direction=spiderContactHeading({shot.motion.velocityX,shot.motion.velocityZ});
            if(direction<24){const auto heading=nativeHeading(direction);const float norm=std::hypot(heading.x,heading.y);
                shot.directionX=heading.x/norm;shot.directionZ=heading.y/norm;actors_[visual].direction=direction;}
        }
    }
    scene_->objects[visual].position=shot.position;
    updateSpiderNavigation(visual,committed?query.closestNonContactNode:std::nullopt);return result;
}

GrenadeNativeContactResult GameWorld::updateNativeProjectileContacts(Projectile& shot,
        ContentPosition proposedSurface,ProjectileDisplacement delta,float seconds) {
    auto result=updateNativeGrenadeContacts(shot,delta,seconds);
    if(result.used)return result; // Preserve the already validated Grenade response.
    // Explicit surface-only test edges have no native segment/rectangle producer.
    if(level_->nativeNavigationDecor.empty() && !level_->collisionEdges.empty())return {};
    const auto from=shot.position;
    //441884..441931: release moving nonplayer bodies at the map boundary
    //before querying contacts or producing CREATE/impact effects. Grenades
    //perform this check above against their independently projected center.
    if((delta.x!=0 || delta.z!=0) &&
       (proposedSurface.x<1 || proposedSurface.y<1 ||
        proposedSurface.x>=level_->mapW*64.f || proposedSurface.y>=level_->mapH*32.f)) {
        result.used=true;result.publicationPending=true;
        result.proposedX=proposedSurface.x;result.proposedY=proposedSurface.y;
        shot.alive=false;
        if(shot.visual){
            actors_[*shot.visual].alive=false;actors_[*shot.visual].solid=false;
            releaseNativeActorIdentity(*shot.visual);synchronizeActorCell(*shot.visual);
        }
        return result;
    }
    const auto& cls=scene_->classes[shot.classId.value];
    if(!spiderNavigation_)initializeSpiderNavigation(true);
    size_t node=SIZE_MAX;
    NativeAmmoContactObject source;
    source.subtype=0x10;source.generation=shot.nativeGeneration;
    source.creator=shot.owner+1;source.creatorGeneration=shot.nativeOwnerGeneration;
    source.ageMs=static_cast<uint32_t>(std::min<double>(shot.ageMs,std::numeric_limits<uint32_t>::max()));
    source.ignoreOwner=shot.ignoreOwner;source.shape=shot.radius<2?2:5;
    SpiderMotionHistory history;
    const auto fallback=spiderSurfaceToNative(from.x,from.y);
    history.center={fallback.x,fallback.y};history.previousSurface=from;
    if(shot.visual && *shot.visual<actors_.size()) {
        const auto id=*shot.visual;updateSpiderNavigation(id);
        source.identity=id+1;
        if(const auto* h=actors_[id].nativeMotionHistory())history=*h;
        if(id<spiderNavigationActors_.size())node=spiderNavigationActors_[id];
    }else {
        updateSpiderProjectileNavigation(shot);node=shot.spiderGraphNode;
        source.identity=NativeContactProjectileBit|shot.nativeBodyIdentity;
        if(shot.nativeBareBody)history=shot.nativeBareBody->motion;
    }
    result.used=true;result.publicationPending=true;
    result.proposedX=proposedSurface.x;result.proposedY=proposedSurface.y;
    const auto clear=[&]{
        shot.position=proposedSurface;
        // A previous rebound can leave an independently retained native center.
        // Advance that history too; otherwise subsequent visual Ammo queries
        // would keep using the old center despite a clear flight step.
        if(shot.visual && actors_[*shot.visual].nativeMotion){
            const auto previous=spiderNativeToSurface(history.center.x,history.center.y);
            const auto current=spiderSurfaceToNative(proposedSurface.x,proposedSurface.y);
            actors_[*shot.visual].nativeMotion=SpiderMotionHistory{{current.x,current.y},{previous.x,previous.y}};
        }
        return result;
    };
    if(shot.radius<=0 || !ammoContactEnabled(shot) ||
       ((shot.phaseHidden||cls.flags.invisible.value_or(false)) && !propertyBool(cls.properties,"Keep-bb-when-invisible")))return clear();
    if(!spiderNavigation_ || node>=spiderNavigation_->objects.size())return clear();
    const ContactVelocity old{history.center.x,history.center.y};
    const auto center=spiderSurfaceToNative(proposedSurface.x,proposedSurface.y);
    const ContactVelocity proposed{center.x,center.y};
    const auto query=queryNativeAmmoShapes(*spiderNavigation_,node,source,old,proposed,
        source.shape==2?0:shot.radius,[&](uint64_t ref){return resolveNativeBodyContact(ref);},true,true);
    if(query.unsupportedShape)return {};
    result.contactHint=query.closestNonContactNode;
    if(!query.contact)return clear();
    const auto& contact=*query.contact;const auto collider=resolveNativeBodyContact(contact.reference);
    if(!collider)return clear();
    if(collider->filter.kind==3) {
        if(contact.reference>=actors_.size())return clear(); // Ammo/Ammo is filtered natively.
        const auto& object=scene_->objects[contact.reference];
        if(const auto* item=attribute(object.properties,"item"))if(propertyBool(item->children,"Only-for-player"))return clear();
        if(contact.reference==player_ && surprise_.active(SurpriseKind::Drone))return clear();
        if(object.classId==shot.classId && propertyBool(cls.properties,"Ignore-same-class-objects"))return clear();
    }else if(collider->filter.kind==2 && (cls.flags.overWater?*cls.flags.overWater:propertyBool(cls.properties,"Over-water")))return clear();
    if(collider->filter.kind==3 || !shot.terrainBounce) {
        const auto hit=spiderNativeToSurface(contact.point.x,contact.point.z);
        result.impactSurface=ContentPosition{hit.x,hit.y};result.terminal=true;
        if(collider->filter.kind==3)result.target=contact.reference;
        shot.position={std::isfinite(hit.x)?hit.x:0.f,std::isfinite(hit.y)?hit.y:0.f};
        return result;
    }
    //4449f0/4367d0: retain the projectile after a single native-selected wall
    //response. Do not gather a different wall or run a second swept actor query.
    const auto previousSurface=spiderNativeToSurface(old.x,old.z);
    shot.position={previousSurface.x,previousSurface.y};
    const ContactVelocity provisional{proposed.x-old.x,proposed.z-old.z};
    //4406e0 CB2/Straight sets command200 on every admitted visit. Other
    //controllers retain their existing command; compact Fixed Ammo has zero.
    const bool straight=std::any_of(cls.behaviours.begin(),cls.behaviours.end(),
        [](const auto& behaviour){return contentNameIs(behaviour,"straight");});
    const int command=straight?200:shot.visual?actors_[*shot.visual].enemyMoveCommand:0;
    auto effective=provisional;
    const bool moved=shot.position.x!=history.previousSurface.x || shot.position.y!=history.previousSurface.y;
    if(!moved && command!=0){
        const auto heading=nativeHeading(shot.visual?actors_[*shot.visual].direction:spiderContactHeading({shot.directionX,shot.directionZ}));
        effective={heading.x*shot.motion.thrust,heading.y*shot.motion.thrust};
    }
    const ContactVelocity inward{contact.point.x-old.x,contact.point.z-old.z};
    if(std::abs(delta.x)+std::abs(delta.z)>2) {
        spawnProjectileBounceEffect(shot,shot.position,{contact.point.x,contact.point.z});
        const float impact=shot.motion.magnitude*shot.motionConfig.weight*.1f;
        if(shot.visual)actors_[*shot.visual].vitals.shield=std::max(0.f,actors_[*shot.visual].vitals.shield-impact);
        else if(shot.nativeBareBody)shot.nativeBareBody->shield=std::max(0.f,shot.nativeBareBody->shield-impact);
    }
    const bool separating=!spiderContactAccepted(effective,inward);
    if(separating) {
        history.center={proposed.x,proposed.z};shot.motion.velocityX=provisional.x;shot.motion.velocityZ=provisional.z;
    }else {
        const ContactVelocity lo{collider->minimum.x-old.x,collider->minimum.z-old.z};
        const ContactVelocity hi{collider->maximum.x-old.x,collider->maximum.z-old.z};
        const auto normal=collider->filter.shape==4?spiderRectangleNormal(lo,hi,inward,shot.radius):
            lo.x==hi.x?ContactVelocity{lo.x>=0?shot.radius:-shot.radius,0}:ContactVelocity{0,lo.z>=0?shot.radius:-shot.radius};
        const auto response=moved||command!=0?resolveNativeTerrainContact(effective,normal,shot.motion.thrust,shot.bounceElasticity,command!=0):SpiderTerrainResponse{};
        shot.motion.velocityX=response.applied?response.velocity.x:provisional.x;
        shot.motion.velocityZ=response.applied?response.velocity.z:provisional.z;
        if(response.clearCommand&&shot.visual)actors_[*shot.visual].enemyMoveCommand=0;
        if(shot.motion.velocityX!=0 || shot.motion.velocityZ!=0){
            const auto direction=spiderContactHeading({shot.motion.velocityX,shot.motion.velocityZ});
            if(direction<24){const auto heading=nativeHeading(direction);const float n=std::hypot(heading.x,heading.y);
                shot.directionX=heading.x/n;shot.directionZ=heading.y/n;if(shot.visual)actors_[*shot.visual].direction=direction;}
        }
    }
    result.motionHandled=true;
    if(shot.nativeBareBody)shot.nativeBareBody->motion=history;
    if(shot.visual)actors_[*shot.visual].nativeMotion=history;
    return result;
}
}
