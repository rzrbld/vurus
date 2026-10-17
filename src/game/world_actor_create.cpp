#include "game/world.h"
#include "game/properties.h"
#include "game/death_products.h"
#include <cmath>

namespace vyruz {
namespace {
bool supportedCreate(const ObjectClass& target,const TextNode& node) {
    const bool fixed=std::any_of(target.behaviours.begin(),target.behaviours.end(),
        [](const auto& b){return contentNameIs(b,"fixed");});
    if(attribute(target.properties,"ammo"))return false;
    // Distance is parsed but unused by413800/437b00/413740. In particular,
    // authored75/150 is not an initial spawn offset or a maximum travel range.
    if(launchedDeathProduct(target))return true;
    return fixed && propertyNumber(target.properties,"Speed-max")==0 &&
        !propertyBool(node.children,"Random-dir") && !propertyBool(node.children,"Set-speed") &&
        propertyNumber(node.children,"Distance")==0 && propertyText(node.children,"Dir").empty();
}
}

void GameWorld::configureActorCreates(ActorState& actor,const ObjectPlacement& object,bool legacy) {
    actor.creates.clear();actor.createsInitialized=true;
    //Ammunition also runs factory CREATE. Its future schedule is transferred
    //to the compact Projectile adapter by the emitter after construction.
    for(const auto& node:object.properties) {
        if(!contentNameIs(node.key,"create"))continue;
        const auto* target=findClass(*scene_,propertyText(node.children,"Name"));
        if(!target)continue;
        // Thing launch uses the native momentum solver below; other moving
        // controllers remain outside this bounded CREATE implementation.
        if(!supportedCreate(*target,node)) {
            if(diagnosedActorCreates_.insert(object.classId.value).second)
                diagnostics_.push_back("ordinary CREATE launch modifiers not implemented: "+scene_->classes[object.classId.value].name);
            continue;
        }
        ActorCreate create;create.classId=target->id;
        create.nextMs=std::trunc(propertyNumber(node.children,"Delay"));
        create.repeatMs=std::trunc(propertyNumber(node.children,"Repeat-timeout"));
        create.currentPosition=propertyBool(node.children,"At-current-position");
        create.trailStep=propertyNumber(node.children,"Trail-step");
        if(const auto* offset=contentProperty(node.children,"Offset")) {
            if(!offset->values.empty())create.offset.x=std::strtof(offset->values[0].c_str(),nullptr);
            if(offset->values.size()>1)create.offset.y=std::strtof(offset->values[1].c_str(),nullptr);
            if(!std::isfinite(create.offset.x)||!std::isfinite(create.offset.y))continue;
        }
        // Older saves contain no ordinary emission history. Resume future
        // repeats without inventing missed construction products or old bursts.
        if(legacy && create.nextMs<=actor.ageMs) {
            if(create.repeatMs>0)create.nextMs=actor.ageMs+create.repeatMs;
            else create.done=true;
        }
        actor.creates.push_back(create);
    }
}
void GameWorld::updateActorCreates(size_t parent,ContentPosition previous,bool constructing) {
    if(parent>=actors_.size() || !actors_[parent].alive || actors_[parent].creates.empty())return;
    //Lifetime/death admission belongs to the visit caller. A Two-pieces
    //lifetime conversion still reaches this same-visit CREATE tail.
    // Original data is finite. Bound malformed recursive class definitions
    // independently of the normal 100,000-object allocation limit.
    if(actorCreateDepth_>=64) {diagnostics_.push_back("recursive ordinary CREATE limit");return;}
    ++actorCreateDepth_;
    struct Scope {unsigned& depth;~Scope(){--depth;}} scope{actorCreateDepth_};
    const size_t count=actors_[parent].creates.size();
    for(size_t n=0;n<count;++n) {
        const auto create=actors_[parent].creates[n]; // spawn can reallocate actors
        if(create.done || create.nextMs>actors_[parent].ageMs)continue;
        const auto origin=constructing || create.currentPosition?scene_->objects[parent].position:previous;
        const ContentPosition position{origin.x+create.offset.x,origin.y+create.offset.y};
        if(position.x<0 || position.y<0 || (level_->mapW>0 && position.x>=level_->mapW*64.f) ||
           (level_->mapH>0 && position.y>=level_->mapH*32.f))continue;
        const float altitude=actors_[parent].projectileAltitude.value_or(propertyNumber(scene_->objects[parent].properties,"Altitude"));
        const auto id=spawnTrail(create.classId,position);
        if(id>=actors_.size())continue;
        actors_[id].effect=attribute(scene_->objects[id].properties,"fx")!=nullptr;
        actors_[id].projectileAltitude=altitude;
        actors_[id].direction=create.direction;
        // CREATE history stays in the existing save layout. Launch parameters
        // are immutable authored data, recovered by the same admission order.
        if(launchedDeathProduct(scene_->classes[create.classId.value])) {
        const TextNode* launch=nullptr;size_t accepted=0;
        for(const auto& node:scene_->objects[parent].properties) {
            if(!contentNameIs(node.key,"create"))continue;
            const auto* target=findClass(*scene_,propertyText(node.children,"Name"));
            if(!target || !supportedCreate(*target,node))continue;
            if(accepted++==n){launch=&node;break;}
        }
        if(launch) {
            auto& child=actors_[id];
            //413b6a/437c57 install world259c around413740, after child
            //construction (and any nested CREATE draws) has completed.
            child.direction=propertyBool(launch->children,"Random-dir")?
                nativeWorldRandom_.inclusive(0,23):
                static_cast<int>(propertyNumber(launch->children,"Dir"));
            child.direction=(child.direction%24+24)%24;
            const auto* thrust=contentProperty(scene_->objects[id].properties,"Thrust");
            child.turnMs=thrust?propertyNumber(thrust->children,"Initial"):0;
            if(propertyBool(launch->children,"Set-speed"))
                child.speed=child.turnMs=propertyNumber(launch->children,"Speed");
            const auto heading=nativeHeading(child.direction);
            child.contactVelocity={heading.x*child.turnMs,heading.y*child.turnMs};
            child.contactMotion=true;
        }
        }
        // Native CREATE does not set creator ancestry. Do not turn ordinary
        // products into projectiles or override their collision/pickup rules.
        auto& timer=actors_[parent].creates[n];
        if(create.repeatMs==0)timer.done=true;else timer.nextMs+=create.repeatMs;
        // 413800 construction does not seed the trail history flag; 437b00 does.
        if(!constructing && create.trailStep>0) {
            if(create.previousOrigin && actors_[id].effect)
                actors_[id].trailSegment=FxTrailSegment{*create.previousOrigin,create.trailStep};
            timer.previousOrigin=ContentPosition{std::trunc(position.x),std::trunc(position.y)};
        }
    }
}
bool GameWorld::advanceLaunchedThing(size_t id,float seconds) {
    auto& actor=actors_[id];const auto& object=scene_->objects[id];
    if(!actor.contactConfig.launchedThing)return false;
    // Parser keyword2a maps Launched to actor+128 bit8000, distinct
    // from impact bit100. 438b20 suppresses Straight command200 indefinitely
    // while inertia retains this flag, so zero authored Weight is never used.
    const auto& p=object.properties;const auto& c=actor.contactConfig;
    auto velocity=actor.contactVelocity;
    velocity.x*=c.grip;velocity.z*=c.grip;
    const float deceleration=propertyNumber(p,"Decceleration-step");
    if(actor.turnMs>deceleration)actor.turnMs-=deceleration;
    else if(actor.turnMs<-deceleration)actor.turnMs+=deceleration;
    else actor.turnMs=0;
    velocity.x*=c.frictionX;velocity.z*=c.frictionZ;
    const float scale=seconds/.014f;
    if(std::abs(velocity.x)<=c.thresholdX*scale && std::abs(velocity.z)<=c.thresholdZ*scale)velocity={};
    actor.contactVelocity=velocity;
    if(propertyBool(p,"Vertical-Bounce")) {
        const float altitude=projectileBounceSignedAltitude(std::hypot(velocity.x,velocity.z),
            actor.ageMs,propertyNumber(p,"Bounce-period"));
        const auto origin=object.position;
        if(altitude<0 && origin.x>=0 && origin.y>=0 &&
           origin.x<level_->mapW*64.f && origin.y<level_->mapH*32.f) {
            const size_t cell=size_t(origin.y/32)*level_->mapW+size_t(origin.x/64);
            if(cell<level_->collisionMask.size() && level_->collisionMask[cell]==0x100) {
                // 4415bf: water disposal is before movement and bypasses death
                // hooks, even for zero-radius Things. spawn may reallocate.
                actor.alive=false;actor.solid=false;
                releaseNativeActorIdentity(id);
                synchronizeActorCell(id);
                if(const auto* splash=findClass(*scene_,"Water-Splash")) {
                    const auto created=spawn(splash->id,origin);
                    if(created<actors_.size()){actors_[created].solid=false;actors_[created].enemy=false;actors_[created].effect=true;}
                }
                return true;
            }
        }
        actor.projectileAltitude=std::abs(altitude);
    }
    const auto displacement=nativeToSurface(velocity.x*scale,velocity.z*scale);
    const auto before=object.position;
    // 4406e0 skips collision dispatch for disabled bounds (actor+b4 bit1).
    // These authored death products have radius0: no wall/actor rebound.
    auto& position=scene_->objects[id].position;
    position.x+=displacement.x;position.y+=displacement.y;
    if(position.x<1 || position.y<1 || position.x>=level_->mapW*64.f || position.y>=level_->mapH*32.f) {
        actor.alive=false;actor.solid=false;releaseNativeActorIdentity(id);synchronizeActorCell(id);return true;
    }
    actor.locomoting=nativeDistanceSquared(before,scene_->objects[id].position)>.0001f;
    synchronizeActorCell(id);
    return true;
}

}
