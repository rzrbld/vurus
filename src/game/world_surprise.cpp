#include "game/world.h"
#include "game/properties.h"
#include "game/attached_fx_altitude.h"
#include "game/shield_regeneration_native.h"
#include "game/event_random_native.h"
#include <algorithm>
#include <cmath>
namespace vyruz {
bool GameWorld::debugSetSurprise(SurpriseKind kind) {
    if(!scene_ || static_cast<unsigned>(kind)>static_cast<unsigned>(SurpriseKind::Nop) ||
       (kind!=SurpriseKind::None && !player().alive))return false;
    cancelSurprise();
    return kind==SurpriseKind::None || takeSurprise(kind);
}
bool GameWorld::takeSurprise(SurpriseKind kind) {
    if(kind==SurpriseKind::None || surprise_.kind!=SurpriseKind::None)return false;
    //41ba00 uses the current global stream normally; replay/demo18000
    //installs the independent World+2590 stream and restores global state.
    if(kind==SurpriseKind::Random)kind=nativeRandomSurprise(
        effectCamera_ && (effectCamera_->modeFlags&0x18000)?nativeReplayRandom_:nativeGlobalRandom_);
    const float tail=surprise_.tripleOffset,flash=surprise_.feedbackStrength;const auto shake=surprise_.shakeOffset;
    surprise_={};surprise_.feedbackStrength=flash;surprise_.feedbackMs=flash*14;surprise_.shakeOffset=shake;surprise_.kind=kind;surprise_.delayMs=500;surprise_.remainingMs=surpriseDuration(kind);surprise_.tripleOffset=tail;
    notifications_.add({NoticeKind::Bonus,surpriseName(kind),188,108,0,
        static_cast<double>(std::max(5000,500+(surpriseDuration(kind)<2?3000:surpriseDuration(kind)))),0,false,false,6,5,0xff2010});
    return true;
}
void GameWorld::cancelSurprise() {
    for(auto held=heldItems_.begin();held!=heldItems_.end();) {
        if(held->holder!=player_ || !held->surprise){++held;continue;}
        //41d270 releases the retained original item before discarding its link.
        releaseNativeHeldItem(*held);
        held=heldItems_.erase(held);
    }
    if(scene_ && player_<actors_.size() && surprise_.applied) {
        auto& p=actors_[player_];
        if(surprise_.kind==SurpriseKind::Shield)p.shieldUpStep=nativeReleaseShieldSurprise(p.shieldUpStep.value_or(10));
        if(surprise_.kind==SurpriseKind::Ammo)restoreAmmoBaseline(p.ammo,surprise_.savedAmmo);
        if(surprise_.kind==SurpriseKind::Drone) {
            scene_->objects[player_].position=surprise_.droneOrigin;
            // The ordinary Player now retains native contact coordinates.
            // Restore both spaces when the Drone returns to its parked body.
            if(auto* history=p.nativeMotionHistory()) {
                const auto n=spiderSurfaceToNative(surprise_.droneOrigin.x,surprise_.droneOrigin.y);
                *history={{n.x,n.y},surprise_.droneOrigin};
            }
            synchronizeActorCell(player_);
            scene_->objects[player_].flags.invisible=surprise_.originalInvisible;
            p.solid=surprise_.originalSolid && p.alive;
            motion_.velocityX=motion_.velocityZ=motion_.magnitude=0;
        }
    }
    surprise_={};
    if(scene_ && player_<actors_.size())actors_[player_].shieldPulse=0;
}
void GameWorld::spawnBananaTrails(bool constructing) {
    const auto* fx=findClass(*scene_,"No-Inertia-Control-FX-1");
    if(!fx)return;
    const auto* type=attribute(fx->properties,"fx");if(!type)return;
    const auto origin=playerPosition();
    const float parentAltitude=actors_[player_].projectileAltitude.value_or(
        propertyNumber(scene_->objects[player_].properties,"Altitude"));
    for(const auto& node:fx->properties)if(contentNameIs(node.key,"create")) {
        const auto* trail=findClass(*scene_,propertyText(node.children,"Name"));if(!trail)continue;
        const auto trailId=trail->id;
        const int count=std::max(0,int(propertyNumber(type->children,"count",1)));
        for(int j=0;j<count;++j) {
            ContentPosition position=origin;
            if(!constructing && surprise_.bananaOrbitInitialized && size_t(j)<surprise_.bananaOrbitPositions.size())
                position=surprise_.bananaOrbitPositions[j];
            const auto id=spawnTrail(trailId,position);if(id>=actors_.size())continue;
            // 413800 constructs the trail before41c180 binds parent altitude.
            // 437b00 only-when-visible copies parent altitude before later CREATE.
            actors_[id].projectileAltitude=std::trunc(constructing?propertyNumber(fx->properties,"Altitude"):parentAltitude);
        }
    }
}
void GameWorld::updateSurpriseOrbitPositions() {
    if(!surprise_.active(SurpriseKind::Banana))return;
    const auto parent=playerPosition();
    const float elapsed=surpriseDuration(SurpriseKind::Banana)-surprise_.remainingMs;
    // Same accepted orbit adapter as the renderer; preserve actual historical
    // positions instead of reconstructing old trails around a moving parent.
    for(size_t j=0;j<surprise_.bananaOrbitPositions.size();++j) {
        const float angle=j*6.28318530718f/5+elapsed*.0024f;
        const auto offset=nativeToSurface(std::cos(angle)*24.f,std::sin(angle)*24.f);
        surprise_.bananaOrbitPositions[j]={parent.x+offset.x,parent.y+offset.y};
    }
    surprise_.bananaOrbitInitialized=true;
}
void GameWorld::initializeSurpriseOrbitAltitude() {
    const auto kind=surprise_.kind;
    if(kind!=SurpriseKind::Banana && kind!=SurpriseKind::Touch && kind!=SurpriseKind::Invulnerable)return;
    const float parent=actors_[player_].projectileAltitude.value_or(
        propertyNumber(scene_->objects[player_].properties,"Altitude"));
    const int groups=kind==SurpriseKind::Invulnerable?5:kind==SurpriseKind::Touch?3:1;
    for(int group=0;group<groups;++group) {
        const auto name=kind==SurpriseKind::Banana?std::string("No-Inertia-Control-FX-1"):
            std::string(kind==SurpriseKind::Touch?"Touch-And-Destroy-FX-":"Invulnerability-FX-")+std::to_string(group+1);
        if(const auto* cls=findClass(*scene_,name))
            surprise_.orbitAttachmentAltitudes[group]=initialAttachedFxAltitude(attachedFxAltitudeConfig(*cls),parent);
    }
    surprise_.orbitAltitudeInitialized=true;
}
void GameWorld::applySurprise() {
    surprise_.applied=true;
    const auto kind=surprise_.kind;
    // 41c647: World clock low two bits choose sample slots 0x23/0x28.
    // Queue once on application; the existing speech scheduler owns admission.
    if(kind==SurpriseKind::Xtc)
        voiceEvent((static_cast<uint64_t>(elapsedMs_)&3)<2?"V-ARIBA.WAV":"V-YEEPEE.WAV",3000,2000,0);
    if(kind==SurpriseKind::Shield)actors_[player_].shieldUpStep=actors_[player_].shieldUpStep.value_or(0)+10.f;
    initializeSurpriseOrbitAltitude();
    if(kind==SurpriseKind::Banana) {
        surprise_.bananaOrbitPositions.fill(playerPosition());surprise_.bananaOrbitInitialized=true;
        spawnBananaTrails(true);
    }
    if(kind==SurpriseKind::Jam)surprise_.jammed=!player().weapons.empty();
    if(kind==SurpriseKind::Ammo)for(auto& [name,stock]:actors_[player_].ammo) {
        surprise_.savedAmmo.slots.insert(surprise_.savedAmmo.begin(),{name,stock});stock.count=stock.maximum;
    }
    if(kind==SurpriseKind::Ammo)refreshSelectedAmmo();
    if(kind==SurpriseKind::Drone) {
        surprise_.droneOrigin=playerPosition();
        surprise_.originalInvisible=scene_->objects[player_].flags.invisible.value_or(false);
        surprise_.originalSolid=player().solid;
        scene_->objects[player_].flags.invisible=true;actors_[player_].solid=false;
        markSpatialDirty(player_);
    }
    if(kind==SurpriseKind::Trap) {
        //41c390 constructs both effects before reducing vitals and drawing.
        for(const char* name:{"Explosion","Shock-Wave"})if(const auto* cls=findClass(*scene_,name)) {
            const auto id=spawn(cls->id,playerPosition());
            if(id<actors_.size()){actors_[id].effect=true;actors_[id].solid=false;actors_[id].enemy=false;}
        }
        actors_[player_].vitals.energy*=.75f;actors_[player_].vitals.shield*=.75f;
    }
    if(kind==SurpriseKind::Destroy) {
        //43d980: ascending Y, then X, then the native mixed cell chain.
        //Marking/enrollment order becomes later death/factory/RNG order.
        const auto p=playerPosition();const auto center=actorCells_.coordinate(p.x,p.y);
        if(center!=NativeActorCells::Missing) {
            const int cx=int(center/size_t(actorCells_.height));
            const int cy=int(center%size_t(actorCells_.height));
            for(int y=std::max(0,cy-6);y<=std::min(actorCells_.height-1,cy+6);++y)
                for(int x=std::max(0,cx-6);x<=std::min(actorCells_.width-1,cx+6);++x) {
                    const size_t cell=size_t(x)*size_t(actorCells_.height)+size_t(y);
                    size_t actor=actorCells_.heads[cell];
                    auto bare=bareAmmoCells_.heads[cell];
                    while(actor!=NativeActorCells::Missing || bare!=NativeAmmoCells::Missing) {
                        const auto link=bareAmmoCells_.links.find(bare);
                        const bool haveBare=bare!=NativeAmmoCells::Missing && link!=bareAmmoCells_.links.end();
                        if(nativeBareAmmoVisitsFirst(actor<actorCells_.arrivalOrder.size()?actorCells_.arrivalOrder[actor]:0,
                                haveBare?link->second.arrivalOrder:0,actor!=NativeActorCells::Missing,haveBare)) {
                            const auto next=link->second.next;
                            const auto found=bareAmmoIndex_.find(bare);
                            if(found!=bareAmmoIndex_.end() && found->second<projectiles_.size()) {
                                auto& shot=projectiles_[found->second];
                                if(shot.alive && shot.nativeBareBody) {
                                    const auto& properties=scene_->classes[shot.classId.value].properties;
                                    if(!propertyBool(properties,"Indestructible") &&
                                       !propertyBool(properties,"Player-direct-fire-only")) {
                                        shot.nativeBareBody->energy=0;
                                        shot.nativeBareBody->damagePending=true;
                                        if(!shot.nativeBareBody->awake)enrollNativeAmmo(shot);
                                    }
                                }
                            }
                            bare=next;continue;
                        }
                        if(actor==NativeActorCells::Missing || actor>=actors_.size())break;
                        const auto next=actor<actorCells_.next.size()?actorCells_.next[actor]:NativeActorCells::Missing;
                        if(actor!=player_ && !actors_[actor].indestructible &&
                           !actors_[actor].directFireOnly) {
                            actors_[actor].vitals.energy=0;actors_[actor].nativeDamagePending=true;
                            if(!actors_[actor].nativeAwake)enrollNativeActor(actor);
                        }
                        actor=next;
                    }
                }
        }
    }
    if(kind==SurpriseKind::Trap || kind==SurpriseKind::Destroy) {
        const auto shake=nativeRandomShake(nativeGlobalRandom_,kind==SurpriseKind::Trap?16:32);
        surprise_.shakeOffset={static_cast<float>(shake[0]),static_cast<float>(shake[1])};
        surprise_.feedbackStrength=std::min(150.f,surprise_.feedbackStrength+(kind==SurpriseKind::Trap?100.f:200.f));
        surprise_.feedbackMs=surprise_.feedbackStrength*14;
    }
}
void GameWorld::updateSurprise(float ms) {
    surprise_.feedbackStrength=std::max(0.f,surprise_.feedbackStrength-ms/14);
    surprise_.feedbackMs=surprise_.feedbackStrength*14;
    const auto decay=[&](float x){return x>0?-std::max(0.f,x-ms/14):std::max(0.f,-x-ms/14);};
    surprise_.shakeOffset={decay(surprise_.shakeOffset.x),decay(surprise_.shakeOffset.y)};
    if(surprise_.kind==SurpriseKind::None)return;
    // Native attached CREATE runs before parent/check-event termination. A due
    // trail survives independently even when the attachment ends this update.
    if(surprise_.active(SurpriseKind::Banana) && surprise_.delayMs<=0) {
        const float elapsed=surpriseDuration(SurpriseKind::Banana)-surprise_.remainingMs;
        if(elapsed>=surprise_.bananaTrailNextMs) {
            spawnBananaTrails(false);
            surprise_.bananaTrailNextMs+=100; // one due emission per update
        }
    }
    if(!player().alive){cancelSurprise();return;}
    if(surprise_.delayMs>0){surprise_.delayMs=std::max(0.f,surprise_.delayMs-ms);return;}
    if(surprise_.remainingMs<=0) {
        const float tail=surprise_.tripleOffset,flash=surprise_.feedbackStrength;const int sweep=surprise_.circularRemaining;const auto shake=surprise_.shakeOffset;
        cancelSurprise();surprise_.tripleOffset=tail;surprise_.circularRemaining=sweep;surprise_.feedbackStrength=flash;surprise_.feedbackMs=flash*14;surprise_.shakeOffset=shake;return;
    }
    surprise_.remainingMs=std::max(0.f,surprise_.remainingMs-ms);
    if(!surprise_.applied)applySurprise();
}
Displacement GameWorld::surpriseMovement(const MovementCommand& raw,float seconds) {
    auto command=raw;
    if(surprise_.active(SurpriseKind::Drone) && surprise_.remainingMs<=1000) {
        if(!surprise_.droneReturning){surprise_.droneReturning=true;surprise_.droneReturnFrom=playerPosition();}
        const float t=1-surprise_.remainingMs/1000;
        const auto a=surprise_.droneReturnFrom,b=surprise_.droneOrigin,p=playerPosition();
        motion_.velocityX=motion_.velocityZ=motion_.magnitude=0;
        return {a.x+(b.x-a.x)*t-p.x,a.y+(b.y-a.y)*t-p.y};
    }
    if(surprise_.active(SurpriseKind::Reverse))command.strafe=-command.strafe;
    if(surprise_.active(SurpriseKind::Motor))command={};
    if(surprise_.active(SurpriseKind::Xtc))command.forward=1;
    auto config=playerMotionConfig_;
    config.reverseTurns=surprise_.active(SurpriseKind::Reverse);
    // 41c390 mutates speed admission, not velocity or acceleration. 438540
    // separately reduces live strafe from2 to.5 under Thrust Loss.
    if(surprise_.active(SurpriseKind::Slow)) {
        config.speedThreshold*=.25f;command.strafe*=.25f;
    }
    if(surprise_.active(SurpriseKind::Xtc))config.speedThreshold*=2;
    if(surprise_.active(SurpriseKind::Banana)) {
        config.frictionX=config.frictionZ=.999f;config.grip=1;
    }
    return advancePlayerMotion(motion_,command,seconds,config);
}
}
