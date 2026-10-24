#include "game/demo_native.h"
#include "game/world.h"
#include "game/save_state.h"
#include "game/properties.h"
#include <cmath>
#include <algorithm>

namespace vyruz {
namespace {template<class F> struct NativeHeldVisitScope {F finish;~NativeHeldVisitScope(){finish();}};}

void GameWorld::enrollNativeActor(std::size_t id) {
    if(id>=actors_.size())return;
    auto& a=actors_[id];
    if(!nativeAwakeEnrollmentAllowed(a.nativeAwake,a.nativeAwakeIndex))return;
    a.nativeAwakeIndex=awakeManager_.append(id);a.nativeAwake=true;
}
void GameWorld::enrollNativeAmmo(Projectile& shot) {
    if(!shot.nativeBareBody || !shot.nativeBodyIdentity)return;
    auto& a=*shot.nativeBareBody;
    if(!nativeAwakeEnrollmentAllowed(a.awake,a.awakeIndex))return;
    a.awakeIndex=awakeManager_.append(NativeAwakeAmmoBit|shot.nativeBodyIdentity);a.awake=true;
}
void GameWorld::wakeNativeActor(std::size_t id) {
    //43ca30/contact callers guard already-awake bodies before419ca0.
    if(id<actors_.size() && !actors_[id].nativeManaged && !actors_[id].nativeAwake)
        enrollNativeActor(id);
}
void GameWorld::releaseNativeAwakeActor(std::size_t id) {
    if(id>=actors_.size())return;
    auto& a=actors_[id];if(a.nativeAwake)awakeManager_.erase(a.nativeAwakeIndex);
    a.nativeAwake=false;a.nativeAwakeIndex=0;
}
void GameWorld::releaseNativeAwakeAmmo(Projectile& shot) {
    if(!shot.nativeBareBody)return;
    auto& a=*shot.nativeBareBody;if(a.awake)awakeManager_.erase(a.awakeIndex);
    a.awake=false;a.awakeIndex=0;
}
void GameWorld::sleepNativeActor(std::size_t id) {
    if(id>=actors_.size() || !actors_[id].nativeAwake)return;
    //441717 clears the CURRENT traversal slot, not the cached index.
    if(awakeVisitIndex_)awakeManager_.erase(*awakeVisitIndex_);
    actors_[id].nativeAwake=false;actors_[id].nativeAwakeIndex=0;
}
NativeAwakeRestDecision GameWorld::nativeActorRestDecision(std::size_t id,ContactVelocity step) const {
    const auto& a=actors_[id];unsigned pose=0,desired=0;
    if(id==player_){pose=nativePlayerFacing_.pose;desired=nativePlayerFacing_.desiredPose;}
    else if(a.spiderAI && a.spiderAI->turn){pose=a.spiderAI->turn->bodyPose;desired=a.spiderAI->turn->bodyDesiredPose;}
    else if(a.enemyTurn){pose=a.enemyTurn->bodyPose;desired=a.enemyTurn->bodyDesiredPose;}
    return nativeAwakeRestDecision(step.x,step.z,3,nativeDamageSubtype(id),true,
        a.nativeManaged?0x10u:0u,a.nativeAwake?0x08000000u:0u,pose,desired);
}
NativeAwakeState GameWorld::captureNativeAwake() const {
    NativeAwakeState state;state.manager=awakeManager_;state.actors.resize(actors_.size());
    for(std::size_t id=0;id<actors_.size();++id)
        if(actors_[id].nativeAwake)state.actors[id]=actors_[id].nativeAwakeIndex;
    for(const auto& shot:projectiles_)if(shot.nativeBareBody && shot.nativeBareBody->awake)
        state.ammo.emplace(shot.nativeBodyIdentity,shot.nativeBareBody->awakeIndex);
    return state;
}
void GameWorld::restoreNativeAwake(const std::optional<NativeAwakeState>& state) {
    awakeManager_.entries.clear();awakeVisitIndex_.reset();deferActorFire_=false;deferredActorFire_.reset();
    for(std::size_t id=0;id<actors_.size();++id) {
        auto& a=actors_[id];a.nativeAwake=false;a.nativeAwakeIndex=0;
        a.nativeManaged=nativeClassManaged(scene_->classes[scene_->objects[id].classId.value].properties);
    }
    for(auto& shot:projectiles_)if(shot.nativeBareBody){shot.nativeBareBody->awake=false;shot.nativeBareBody->awakeIndex=0;}
    if(state) {
        awakeManager_=state->manager;
        for(std::size_t id=0;id<actors_.size();++id)if(state->actors[id]) {
            actors_[id].nativeAwake=true;actors_[id].nativeAwakeIndex=*state->actors[id];
        }
        for(auto& shot:projectiles_)if(shot.nativeBareBody) {
            const auto at=state->ammo.find(shot.nativeBodyIdentity);
            if(at!=state->ammo.end()){shot.nativeBareBody->awake=true;shot.nativeBareBody->awakeIndex=at->second;}
        }
        return;
    }
    //Legacy files have no sparse manager history. Reconstruct admitted bodies
    //in serialized actor order followed by compact Ammo order, including moving/off bodies.
    //Legacy files never stored the mixed order. Numeric UID sorting cannot recover
    //it across wrap; this deterministic bounded migration does not claim to.
    std::vector<std::pair<std::uint32_t,std::uint64_t>> order;
    for(std::size_t id=0;id<actors_.size();++id) {
        const auto& a=actors_[id];
        if(a.nativeGeneration && (a.alive || a.reactivateMs>=0) &&
           (a.nativeManaged || a.reactivateMs>=0 || a.contactMotion || a.contactVelocity.x!=0 || a.contactVelocity.z!=0))
            order.emplace_back(a.nativeGeneration,id);
    }
    for(const auto& shot:projectiles_)if(shot.alive && shot.nativeBareBody &&
        nativeClassManaged(scene_->classes[shot.classId.value].properties))
        order.emplace_back(shot.nativeGeneration,NativeAwakeAmmoBit|shot.nativeBodyIdentity);
    for(const auto& [generation,id]:order) {
        (void)generation;
        if(id&NativeAwakeAmmoBit){const auto at=bareAmmoIndex_.find(id&~NativeAwakeAmmoBit);if(at!=bareAmmoIndex_.end())enrollNativeAmmo(projectiles_[at->second]);}
        else enrollNativeActor(id);
    }
}
void GameWorld::visitNativeActor(size_t id,float seconds,const WorldInput& input,bool& locked) {
    if(id>=actors_.size() || (!actors_[id].nativeHeldOriginal && !actors_[id].alive && actors_[id].reactivateMs<0))return;
    const bool heldVisit=actors_[id].nativeHeldOriginal;
    const auto heldSlot=actors_[id].nativePoolSlot;const auto heldGeneration=actors_[id].nativeGeneration;
    if(heldVisit){markSpatialDirty(id);actors_[id].alive=true;}
    // The native held original remains the same kind3 manager body. Port
    // presentation uses alive=false, while its normal age/timer/rest visit
    //still runs. Keep the inventory copy synchronized on every early return.
    NativeHeldVisitScope heldScope{[&,id,heldVisit,heldSlot,heldGeneration] {
        if(!heldVisit || id>=actors_.size())return;
        bool retained=false;
        for(auto& item:heldItems_)if(item.object.id.value==id &&
            item.actor.nativePoolSlot==heldSlot &&
            item.actor.nativeGeneration==heldGeneration) {
            // Derived flag identifies only a proved current-map alias.
            retained=true;item.actor=actors_[id];
        }
        if(retained){actors_[id].alive=false;actors_[id].nativeHeldOriginal=true;}
        else actors_[id].nativeHeldOriginal=false;
        synchronizeActorCell(id);
    }};
    auto& initial=actors_[id];initial.nativeAmmoAbort=false;initial.locomoting=false;
    const auto ammo=visualAmmoIndex_.find(id);
    if(ammo!=visualAmmoIndex_.end()) {
        const size_t index=ammo->second;
        updateProjectile(index,seconds,true);
        return;
    }
    const auto convertedTail=[&] {
        updateActorCreates(id,scene_->objects[id].position);
        if(!actors_[id].alive)return;
        if(retireBodyGraphics(id))return;
        if(actors_[id].effect){updateExplosion(id,seconds*1000);return;}
        if(actors_[id].nativeFireRequested)fire(id,true,false);
        if(actors_[id].alive)updateGenerator(id,seconds*1000);
    };
    initial.ageMs+=seconds*1000;
    if(initial.lifeMs>=0 && initial.ageMs>=initial.lifeMs) {
        initial.reactivateMs=-1;initial.alive=true;destroy(id,std::nullopt);
        if(actors_[id].alive)convertedTail();
        return;
    }
    if(id==player_) {updateHeldCodes(seconds*1000);updateSurprise(seconds*1000);}
    if(!actors_[id].alive) {
        actors_[id].reactivateMs-=seconds*1000;
        if(actors_[id].reactivateMs>0)return;
        actors_[id].alive=true;actors_[id].reactivateMs=-1;markSpatialDirty(id);
    }
    if(actors_[id].nativeDamagePending) {
        destroy(id,std::nullopt);
        if(!actors_[id].alive)return;
        convertedTail();return; //Two-pieces conversion reaches only the common tail.
    }
    if(id!=player_ && !actorUpdateAdmitted(id))return;
    advanceBodyGraphicsAwake(id,bodyGraphicsStepMs_);
    actors_[id].pickupDelayMs=std::max(0.f,actors_[id].pickupDelayMs-seconds*1000);
    actors_[id].walkingCommand=false;actors_[id].enemyRevealed=false;
    const auto previous=scene_->objects[id].position;
    if(id==player_ && surprise_.circularRemaining>0) {
        //440dd7 repeated-fire visits skip ordinary motion/facing/CREATE.
        fire(id,true,true);updateGenerator(id,seconds*1000);return;
    }
    actors_[id].nativeFireRequested=false;
    deferActorFire_=true;deferredActorFire_.reset();
    if(id==player_) {
        const bool sweeping=false;
        //438540 returns action1/fire only, even while movement continues.
        //Motor block02000000 returns false; jammed weapon still counts input.
        audioInput_=input.fire && !surprise_.active(SurpriseKind::Motor);
        auto& p=actors_[player_];
        if (input.weaponDelta && !p.weapons.empty()) {
            const auto n=static_cast<int>(p.weapons.size());
            for (int attempt=0;attempt<n;++attempt) {
                p.selectedWeapon=(static_cast<int>(p.selectedWeapon)+(input.weaponDelta>0?1:-1)+n)%n;
                selectCompatibleActorAmmo(player_);
                const auto stock=weaponStock(p,p.weapons[p.selectedWeapon]);
                if (stock!=p.ammo.end() && stock->second.count) break;
            }
            p.weapons[p.selectedWeapon].state.lastFireMs.reset();
            p.nativeAmmoOwner.activeAmmo=0;
            audioEvent(AudioEventKind::WeaponStop,player_);audioEvent(AudioEventKind::WeaponSwitch,player_);
            notifyWeapon();
        }
        advanceShieldRegeneration(player_);
        //438800 runs inside the admitted player visit, after Drone/Circular
        //special branches. A replay pose is not a teleport/reset: preserve
        //thrust, cached magnitude, previous surface and collision continuation.
        if(input.replayPose && !(surprise_.active(SurpriseKind::Drone)&&surprise_.remainingMs<=1000)) {
            const auto& record=*input.replayPose;
            auto& position=scene_->objects[player_].position;
            motion_.direction=record.direction;p.direction=record.direction;
            nativePlayerFacing_.logicalHeading=record.direction;
            nativePlayerFacing_.orthogonalHeading=(record.direction+3)%24;
            nativePlayerFacing_.desiredPose=record.direction*2+1;
            if(position.x!=record.x || position.y!=record.y) {
                position={record.x,record.y};
                const auto previousSurface=p.nativeAmmoContinuation&&p.nativeMotionHistory()?
                    p.nativeMotionHistory()->previousSurface:nativePlayerPreviousSurface_;
                const auto velocity=nativeDemoRecordedVelocity(position.x,position.y,
                    previousSurface.x,previousSurface.y);
                motion_.velocityX=velocity.x;motion_.velocityZ=velocity.y;
                if(auto* h=p.nativeMotionHistory()){const auto n=spiderSurfaceToNative(position.x,position.y);h->center={n.x,n.y};}
                synchronizeActorCell(player_);
                if(collisionQueriesActive_)updateActorBounds(player_);
                else updateSpiderNavigation(player_);
            }
        }
        const auto previousPlayerSurface=playerPosition();
        const int target=input.movement.aimActive?
            nativeDirectionFromVector(input.movement.aimX,input.movement.aimY):24;
        const auto* retained=p.nativeAmmoContinuation?p.nativeMotionHistory():nullptr;
        const auto previousMouseSurface=retained?retained->previousSurface:nativePlayerPreviousSurface_;
        const bool moving=previousPlayerSurface.x!=previousMouseSurface.x ||
            previousPlayerSurface.y!=previousMouseSurface.y;
        int turnCommand=nativePlayerAimTurnCommand(nativePlayerFacing_.logicalHeading,target,
            moving,surprise_.active(SurpriseKind::Reverse),surprise_.active(SurpriseKind::Motor));
        if(input.replayPose) {
            const auto flags=input.replayPose->flags;
            turnCommand=flags&0xc0?0:flags&0x10?0xcb:flags&0x20?0xcd:0;
            if(surprise_.active(SurpriseKind::Reverse)&&turnCommand)turnCommand=turnCommand==0xcb?0xcd:0xcb;
            if(surprise_.active(SurpriseKind::Motor))turnCommand=0;
        }
        const int elapsed=static_cast<int>(std::floor(elapsedMs_)-std::floor(elapsedMs_-seconds*1000));
        advanceNativePlayerFacing(nativePlayerFacing_,turnCommand,elapsed);
        if(input.replayPose)motion_.direction=nativePlayerFacing_.logicalHeading;
        const auto delta=surpriseMovement(input.movement,seconds);
        p.direction=motion_.direction;
        if(surprise_.active(SurpriseKind::Drone)&&surprise_.droneReturning) {
            scene_->objects[player_].position.x+=delta.x;scene_->objects[player_].position.y+=delta.y;
        } else {
            const float scale=seconds/.014f;
            const ContactVelocity nativeStep=delta.x==0 && delta.y==0?ContactVelocity{}:
                ContactVelocity{motion_.velocityX*scale,motion_.velocityZ*scale};
            if(nativeActorRestDecision(player_,nativeStep).rested) {
                if(auto* h=p.nativeMotionHistory())h->previousSurface=previousPlayerSurface;
                motion_.velocityX=motion_.velocityZ=0;
                if(!p.nativeManaged)sleepNativeActor(player_);
            } else move(player_,delta.x,delta.y,true,&nativeStep);
        }
        synchronizeActorCell(player_);
        nativePlayerPreviousSurface_=previousPlayerSurface;
        if(!actors_[player_].nativeAmmoAbort) {
            if(!sweeping && surprise_.active(SurpriseKind::Circular) && input.fire)surprise_.circularRemaining=24;
            fire(player_,sweeping || input.fire,sweeping,
                input.movement.forward>0?std::optional<ContentPosition>{previousPlayerSurface}:std::nullopt);
        }

    } else if(actors_[id].gate || actors_[id].lift) {
        if(actors_[id].gate)updateGate(id,seconds*1000);
        if(actors_[id].lift)updateLift(id,seconds*1000);
    } else if(advanceLaunchedThing(id,seconds)) {
        //The launched Thing helper owns its motion/disposal; live bodies still
        //reach the same current-visit CREATE/graphic/generator tail.
    } else if(!actors_[id].enemy || !debugEnemyAI_) {
        actors_[id].enemyMoveCommand=0;advanceShieldRegeneration(id);
        advanceContactMotion(id,seconds);
        actors_[id].locomoting=nativeDistanceSquared(previous,scene_->objects[id].position)>.0001f;
        const auto velocity=actors_[id].contactVelocity;
        if(nativeActorRestDecision(id,velocity).sleep)sleepNativeActor(id);
    } else locked|=advanceActorAI(id,seconds);
    deferActorFire_=false;
    const auto pending=deferredActorFire_;deferredActorFire_.reset();
    if(id>=actors_.size() || !actors_[id].alive || actors_[id].nativeAmmoAbort)return;
    //442aa2/444ca0: CREATE is before subtype FX/Ammo/ordinary fire dispatch.
    updateActorCreates(id,previous);
    if(id>=actors_.size() || !actors_[id].alive)return;
    if(retireBodyGraphics(id))return;
    if(actors_[id].effect){updateExplosion(id,seconds*1000);return;}
    if(pending)fire(pending->actor,pending->held,pending->sweep,pending->previous,pending->aiming,pending->visit);
    if(id<actors_.size() && actors_[id].alive)updateGenerator(id,seconds*1000);
}

}
