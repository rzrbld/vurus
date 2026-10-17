#include "game/world.h"
#include "game/properties.h"
#include "game/health_damage_native.h"
#include "game/ammo_absorption_native.h"
#include "game/ammo_radial_native.h"
#include "game/ammo_radial_contract_native.h"
#include "game/spider_contact_native.h"
#include "game/native_impact_coordinate.h"
#include "game/hit_latch_native.h"
#include "game/event_random_native.h"
#include <algorithm>
#include <cmath>

namespace vyruz {
namespace {
uint32_t ageWord(double milliseconds) {
    if(!std::isfinite(milliseconds)||milliseconds<0)return 0;
    return static_cast<uint32_t>(std::fmod(std::trunc(milliseconds),4294967296.0));
}
}
void GameWorld::initializeActorCells() {
    actorCells_.reset(std::max(0,level_->mapW),std::max(0,level_->mapH));
    bareAmmoCells_.reset(actorCells_.width,actorCells_.height);
    for(size_t id=0;id<actors_.size();++id)synchronizeActorCell(id);
}
void GameWorld::synchronizeActorCell(size_t id) {
    if(!scene_||!level_||id>=actors_.size()||actorCells_.width!=level_->mapW||actorCells_.height!=level_->mapH)return;
    const auto p=scene_->objects[id].position;
    const auto destination=!actors_[id].nativeHeldOriginal && (actors_[id].alive||actors_[id].reactivateMs>=0)?
        actorCells_.coordinate(p.x,p.y):NativeActorCells::Missing;
    if(id<actorCells_.cell.size() && actorCells_.cell[id]==destination)return;
    actorCells_.relocate(id,destination,destination==NativeActorCells::Missing?
        std::optional<std::uint64_t>{}:std::optional<std::uint64_t>{nextNativeCellOrder_++});
}
void GameWorld::initializeNativeIdentity(size_t id) {
    if(id>=actors_.size())return;
    const auto allocation=allocateNativePoolBody();
    actors_[id].nativePoolSlot=allocation.slot;actors_[id].nativeGeneration=allocation.generation;
    actors_[id].nativeCreatorGeneration=0;
    actors_[id].nativeAmmoOwner={};
    if(actors_[id].nativeManaged)enrollNativeActor(id);
}
void GameWorld::assignNativeCreator(size_t id,std::optional<size_t> creator) {
    if(id>=actors_.size())return;
    if(creator && *creator<actors_.size()) {
        scene_->objects[id].owner=ObjectId{static_cast<std::uint32_t>(*creator)};
        actors_[id].nativeCreatorGeneration=actors_[*creator].nativeGeneration;
    } else {scene_->objects[id].owner.reset();actors_[id].nativeCreatorGeneration=0;}
}
std::optional<size_t> GameWorld::nativeAmmoOwner(const Projectile& source) const {
    if(source.owner<actors_.size() && actors_[source.owner].nativeGeneration==source.nativeOwnerGeneration)
        return source.owner;
    return {};
}
bool GameWorld::nativeAmmoPlayerOwned(Projectile& source) {
    const auto owner=nativeAmmoOwner(source);
    if(!owner) {
        //43c9cc clears only the stale pointer, retaining captured generation.
        source.owner=SIZE_MAX;
        if(source.visual && *source.visual<actors_.size())scene_->objects[*source.visual].owner.reset();
        return false;
    }
    size_t current=*owner;
    if(current==player_)return true;
    for(size_t count=0;count<actors_.size();++count) {
        auto& object=scene_->objects[current];const auto creator=object.owner;
        if(!creator)return false;
        const auto id=size_t(creator->value);
        if(id>=actors_.size() || actors_[id].nativeGeneration!=actors_[current].nativeCreatorGeneration) {
            object.owner.reset();return false;
        }
        if(id==player_)return true;
        current=id;
    }
    return false; //Finite port inventory bounds corrupted cycles, not native AI.
}
void GameWorld::releaseNativeActorIdentity(size_t id) {
    if(id>=actors_.size())return;
    releaseNativeAwakeActor(id);
    //The visual Actor and its Projectile adapter are one native body. Physical
    //release must retire both even though the erased manager slot cannot visit
    //the Projectile adapter later in this pass.
    if(const auto at=visualAmmoIndex_.find(id);at!=visualAmmoIndex_.end() && at->second<projectiles_.size())
        {projectiles_[at->second].alive=false;projectiles_[at->second].nativeGeneration=0;}
    const auto creator=scene_->objects[id].owner;
    if(nativeDamageSubtype(id)==0x10 && creator && creator->value<actors_.size() &&
       actors_[creator->value].nativeGeneration==actors_[id].nativeCreatorGeneration)
        nativeAmmoOwnerReleased(actors_[creator->value].nativeAmmoOwner,true,true,false,false);
    //4199d0 guards UID0 before calling raw419ac0. Detach that higher-level
    //body without freeing its still-allocated zero-generation pool slot.
    if(actors_[id].nativeGeneration==0)actors_[id].nativePoolSlot=SIZE_MAX;
    else releaseNativePoolBody(actors_[id].nativePoolSlot);
    actors_[id].nativeGeneration=0; //419c5a, upper-piece conversion is not release.
    actors_[id].nativeAmmoContinuation=false;
}
void GameWorld::initializeBareAmmoBody(Projectile& shot) {
    shot.nativeOwnerGeneration=shot.owner<actors_.size()?actors_[shot.owner].nativeGeneration:0;
    if(shot.visual && *shot.visual<actors_.size()) {
        shot.nativeGeneration=actors_[*shot.visual].nativeGeneration;
        assignNativeCreator(*shot.visual,shot.owner<actors_.size()?std::optional<size_t>{shot.owner}:std::nullopt);
        return;
    }
    const auto& cls=scene_->classes[shot.classId.value];
    const auto allocation=allocateNativePoolBody();
    shot.nativePoolSlot=allocation.slot;shot.nativeGeneration=allocation.generation;
    shot.nativeBodyIdentity=nextNativeBodyIdentity_++;
    NativeBareAmmoBody body;
    const auto* energy=contentProperty(cls.properties,"Energy");
    const auto* shield=contentProperty(cls.properties,"Shield");
    body.energy=energy?propertyNumber(energy->children,"Initial",1):1;
    body.shield=shield?propertyNumber(shield->children,"Initial"):0;
    const auto center=spiderSurfaceToNative(shot.position.x,shot.position.y);
    body.motion={{center.x,center.y},shot.position};shot.nativeBareBody=body;
    if(nativeClassManaged(cls.properties))enrollNativeAmmo(shot);
}
void GameWorld::synchronizeBareAmmoCell(const Projectile& shot) {
    if(!shot.nativeBareBody || !shot.nativeBodyIdentity)return;
    if(bareAmmoCells_.width!=actorCells_.width || bareAmmoCells_.height!=actorCells_.height)
        bareAmmoCells_.reset(actorCells_.width,actorCells_.height);
    const auto destination=shot.alive?actorCells_.coordinate(shot.position.x,shot.position.y):SIZE_MAX;
    const auto at=bareAmmoCells_.links.find(shot.nativeBodyIdentity);
    if(at!=bareAmmoCells_.links.end() && at->second.cell==destination)return;
    bareAmmoCells_.relocate(shot.nativeBodyIdentity,destination,
        destination==SIZE_MAX?0:nextNativeCellOrder_++);
}
void GameWorld::rebuildBareAmmoIndex() {
    bareAmmoIndex_.clear();
    visualAmmoIndex_.clear();
    for(size_t id=0;id<projectiles_.size();++id)
        if(projectiles_[id].visual)visualAmmoIndex_[*projectiles_[id].visual]=id;
        else
        if(projectiles_[id].alive && projectiles_[id].nativeBareBody && projectiles_[id].nativeBodyIdentity)
            bareAmmoIndex_.emplace(projectiles_[id].nativeBodyIdentity,id);
}
void GameWorld::releaseNativeProjectile(Projectile& shot) {
    releaseNativeAwakeAmmo(shot);
    // Physical removal can be requested by actor->Ammo immediately and again
    //by the projectile sweep. The live identity index is the enrollment guard;
    //direct hit bookkeeping above is deliberately not guarded by this removal.
    if(!shot.visual && bareAmmoIndex_.contains(shot.nativeBodyIdentity)) {
        if(const auto owner=nativeAmmoOwner(shot))
            nativeAmmoOwnerReleased(actors_[*owner].nativeAmmoOwner,true,true,false,false);
    }
    //A restored already-dead compact body is deliberately absent from the
    //live spatial lookup, but still owns its saved physical pool slot.
    //Slot ownership (not live-index admission) guards physical release.
    if(!shot.visual) {
        if(shot.nativeGeneration==0)shot.nativePoolSlot=SIZE_MAX;
        else releaseNativePoolBody(shot.nativePoolSlot);
    }
    if(shot.nativeBodyIdentity){bareAmmoCells_.erase(shot.nativeBodyIdentity);bareAmmoIndex_.erase(shot.nativeBodyIdentity);}
    shot.nativeGeneration=0;
}
uint32_t GameWorld::nativeDamageSubtype(size_t id) const {
    // Recursive placement CREATE can inspect a later authored slot before its
    // makeActor has run. Preserve the original resolver during that factory pass.
    if(constructingScene_)return nativeObjectSubtype(scene_->objects[id],id==player_);
    return actors_[id].nativeSubtype;
}
uint32_t GameWorld::nativeDamageClassFlags(size_t id) const {
    const auto& a=actors_[id];const auto& p=scene_->classes[scene_->objects[id].classId.value].properties;
    return (propertyBool(p,"Two-pieces")?2u:0u)|(propertyBool(p,"Final-object")?0x20u:0u)|
           (a.directFireOnly?0x40u:0u);
}
uint32_t GameWorld::nativeDamageFlags(size_t id) const {
    const auto& a=actors_[id];uint32_t flags=0;
    if(scene_->objects[id].flags.invisible.value_or(false)||a.ammoHidden)flags|=8;
    if(a.turretOnlyBase)flags|=0x20;
    if(a.nativeKillCounted)flags|=0x40;
    if(a.nativeHit)flags|=0x200;
    if(a.nativeAwake)flags|=0x08000000;
    if(a.indestructible)flags|=0x200000;
    if(a.timedProtection.active || (id==player_ && (debugGodMode_||surprise_.active(SurpriseKind::Invulnerable))))flags|=0x800000;
    if(id==player_ && surprise_.active(SurpriseKind::Touch))flags|=0x400000;
    return flags;
}
void GameWorld::consumePlayerHitForRender(bool animationOnly,int pass) {
    if(player_>=actors_.size() || !actors_[player_].alive)return;
    auto& player=actors_[player_];
    const auto consumed=nativeHitLatchRender(nativeDamageFlags(player_),0,
        ageWord(elapsedMs_),true,animationOnly,pass);
    player.nativeHit=(consumed.flags&0x200u)!=0;
    // The accepted renderer already owns a transient500ms player-only interval.
    // Start it at actual drawing, never during headless/animation-only updates.
    if(consumed.consumed)player.hitFlashMs=500;
}
void GameWorld::applyNativeContactDamage(std::size_t id,float amount) {
    if(id>=actors_.size() || (!actors_[id].alive && actors_[id].reactivateMs<0) ||
       !std::isfinite(amount))return;
    //442798/4428e1/442916 call43ca30 directly, after their distinct geometry,
    //Shield and Direct-fire-only admission. This is not an Ammo hit/kill caller.
    auto& target=actors_[id];
    NativeHealthDamageInput input;
    input.energy=target.vitals.energy;input.shield=target.vitals.shield;
    input.shieldMinimum=target.vitals.shieldMin;input.actorFlags=nativeDamageFlags(id);
    input.classFlags1c=target.nativeManaged?0x10u:0u;input.classFlags20=nativeDamageClassFlags(id);
    input.behaviorFlags=id==player_?3u:target.brain.tracker?2u:target.brain.seeker?1u:0u;
    const auto result=nativeHealthDamage(input,amount);
    if(result.awakeRequested)wakeNativeActor(id); //43ca7f: even protected targets.
    target.vitals.energy=result.energy;target.vitals.shield=result.shield;
    if(result.energyUpdated)target.nativeDamagePending=result.energy<=0;
    if(result.lethalClassCallback && target.finalObject)beginFinalEnding(id);
    //The current visit's response/common tail continues. Energy death is tested
    //only when this target next enters440c6f; no Hit, ownership or Ammo counters.
}
void GameWorld::applyNativeAmmoDamage(size_t id,float amount,Projectile& source,bool radial,bool radialPlayerOwned,bool sourceIsAmmo) {
    if(id>=actors_.size()||(!actors_[id].alive && actors_[id].reactivateMs<0)||!std::isfinite(amount))return;
    auto& target=actors_[id];
    const bool playerOwned=radial?radialPlayerOwned:(target.directFireOnly?nativeAmmoPlayerOwned(source):false);
    NativeHealthDamageInput input;
    input.energy=target.vitals.energy;input.shield=target.vitals.shield;input.shieldMinimum=target.vitals.shieldMin;
    input.actorFlags=nativeDamageFlags(id);input.classFlags20=nativeDamageClassFlags(id);
    input.classFlags1c=target.nativeManaged?0x10u:0u;
    input.behaviorFlags=id==player_?3:target.brain.tracker?2:target.brain.seeker?1:0;
    NativeHealthDamageResult result;result.energy=input.energy;result.shield=input.shield;
    //43d658 skips only health on Direct-fire-only rejection; the later owner
    //statistics still examine the target's existing Energy/counting state.
    if(!target.directFireOnly || playerOwned)result=nativeHealthDamage(input,amount);
    if(result.awakeRequested)wakeNativeActor(id); //43ca7f precedes protection rejection.
    target.vitals.energy=result.energy;target.vitals.shield=result.shield;
    if(result.energyUpdated)target.nativeDamagePending=result.energy<=0;
    const bool finalCallback=result.lethalClassCallback && target.finalObject;
    if(finalCallback)beginFinalEnding(id); // Native callback precedes credit.
    NativeAmmoKillCreditInput credit;
    credit.targetEnergy=actors_[id].vitals.energy;credit.targetFlags=nativeDamageFlags(id);credit.targetClassFlags20=nativeDamageClassFlags(id);
    credit.targetSubtype=nativeDamageSubtype(id);credit.sourcePresent=true;credit.sourceIsAmmo=sourceIsAmmo;
    credit.sourcePlayerOwned=playerOwned;credit.immediateOwnerValid=nativeAmmoOwner(source).has_value();
    credit.immediateOwnerIsPlayer=source.owner==player_;credit.targetCountsAsEnemy=actors_[id].hudEnemy;
    const auto effect=nativeAmmoKillCredit(credit,radial);
    if(effect.fastKill){if(fastKillMs_<1){fastKillMs_=500;fastKillCount_=1;}else ++fastKillCount_;}
    if(effect.ownerKill){
        ++actors_[source.owner].nativeAmmoOwner.kills;
        if(source.owner==player_)++playerKills_;
    }
    if(effect.markCounted)actors_[id].nativeKillCounted=true;
    // The native awake-manager enrollment and general nonplayer owner counters
    // remain separate adapters. Actor death/reward/EXPL occur at its next visit.
    if(effect.ownerCommentary){
        static const char* voices[]={"V-YEAH.WAV","V-HEY.WAV","V-YO.WAV","V-GIVE-ME.WAV","V-THE-BEST.WAV","V-WANT-MORE.WAV","V-LAUGH-1.WAV"};
        const unsigned choice=static_cast<uint64_t>(elapsedMs_)&15;
        if(choice<std::size(voices))voiceEvent(voices[choice],1000,1500);
    }
    //43ce6a is reached independently of kill credit and source subtype; plain
    //43ca30 health and43d658 direct health do not synthesize Hit themselves.
    if(radial)actors_[id].nativeHit=(nativeHitLatchSet(nativeDamageFlags(id))&0x200u)!=0;
    //43ce80..43d017: admitted player radial damage also shakes at >=200,
    //independently of healthApplied; subthreshold fractional decay stays intact.
    if(radial && id==player_ && amount>=200) {
        const auto shake=nativeRadialDamageShake(nativeWorldRandom_,amount,true,
            {int(surprise_.shakeOffset.x),int(surprise_.shakeOffset.y)});
        surprise_.shakeOffset={float(shake[0]),float(shake[1])};
    }
    if(radial && id==player_)radialInjuryCommentary(amount);
}
void GameWorld::applyNativeBareAmmoDamage(std::uint64_t identity,float amount,Projectile& source,bool playerOwned,bool sourceIsAmmo) {
    const auto found=bareAmmoIndex_.find(identity);
    if(found==bareAmmoIndex_.end() || found->second>=projectiles_.size() || !std::isfinite(amount))return;
    auto& target=projectiles_[found->second];
    if(!target.alive || !target.nativeBareBody)return;
    const auto& cls=scene_->classes[target.classId.value];
    auto& body=*target.nativeBareBody;
    NativeHealthDamageInput input;input.energy=body.energy;input.shield=body.shield;
    if(const auto* shield=contentProperty(cls.properties,"Shield"))input.shieldMinimum=propertyNumber(shield->children,"Min");
    input.actorFlags=(body.killCounted?0x40u:0u)|(body.nativeHit?0x200u:0u)|(body.awake?0x08000000u:0u)|
        ((target.phaseHidden||cls.flags.invisible.value_or(false))?8u:0u)|
        (propertyBool(cls.properties,"Indestructible")?0x200000u:0u);
    input.classFlags1c=nativeClassManaged(cls.properties)?0x10u:0u;
    input.classFlags20=(propertyBool(cls.properties,"Two-pieces")?2u:0u)|
        (propertyBool(cls.properties,"Final-object")?0x20u:0u)|
        (propertyBool(cls.properties,"Player-direct-fire-only")?0x40u:0u);
    for(const auto& behavior:cls.behaviours){
        if(contentNameIs(behavior,"player"))input.behaviorFlags|=3;
        if(contentNameIs(behavior,"seeker"))input.behaviorFlags|=1;
        if(contentNameIs(behavior,"tracker"))input.behaviorFlags|=2;
    }
    if(!(input.classFlags20&0x40) || playerOwned){
        const auto result=nativeHealthDamage(input,amount);
        if(result.awakeRequested)enrollNativeAmmo(target);
        body.energy=result.energy;body.shield=result.shield;
        if(result.energyUpdated)body.damagePending=result.energy<=0;
    }
    NativeAmmoKillCreditInput credit;
    credit.targetEnergy=body.energy;credit.targetFlags=input.actorFlags;credit.targetClassFlags20=input.classFlags20;
    credit.targetSubtype=0x10;credit.sourcePresent=true;credit.sourceIsAmmo=sourceIsAmmo;
    credit.sourcePlayerOwned=playerOwned;credit.immediateOwnerValid=nativeAmmoOwner(source).has_value();
    credit.immediateOwnerIsPlayer=source.owner==player_;
    const auto effect=nativeAmmoKillCredit(credit,true);
    if(effect.ownerKill){++actors_[source.owner].nativeAmmoOwner.kills;if(source.owner==player_)++playerKills_;}
    if(effect.markCounted)body.killCounted=true;
    if(effect.ownerCommentary){
        static const char* voices[]={"V-YEAH.WAV","V-HEY.WAV","V-YO.WAV","V-GIVE-ME.WAV","V-THE-BEST.WAV","V-WANT-MORE.WAV","V-LAUGH-1.WAV"};
        const unsigned choice=static_cast<uint64_t>(elapsedMs_)&15;
        if(choice<std::size(voices))voiceEvent(voices[choice],1000,1500);
    }
    body.nativeHit=(nativeHitLatchSet(input.actorFlags)&0x200u)!=0;
}
void GameWorld::applyAmmoImpact(Projectile& shot,std::optional<size_t> target,bool contact,
                               NativeNavPoint center,float tickMilliseconds) {
    //441b9a: selected moving-Ammo contact latches before43d7d0, even if the
    //following Hit-fx admission suppresses direct health. Fuse-only calls do not.
    if(contact && target && *target<actors_.size()) {
        wakeNativeActor(*target); //common kind3 admission precedes lateimpact flags.
        actors_[*target].nativeHit=(nativeHitLatchSet(nativeDamageFlags(*target))&0x200u)!=0;
    }
    //World26e0/e4 contact counters precede all damage/Hit-fx. The separate
    //owner264/268 direct-phase counters are not these exposed global counters.
    if(contact)recordAmmoContact(nativeAmmoOwner(shot),target);
    //43d7d0: the source's retained/current native bounds are independent of
    //the relocated contact surface. Radial first, including the direct target.
    const int radius=std::isfinite(shot.damageRadius) && shot.damageRadius>=1 && shot.damageRadius<=46340?
        static_cast<int>(shot.damageRadius):0;
    if(radius>0 && shot.damage!=0){
        //43cb72 validates ancestry once, before cell traversal (even when its
        //window contains no recipients). Direct calls do so only for class40.
        const bool playerOwned=nativeAmmoPlayerOwned(shot);
        //43d7d0 converts relocated contact surface through __ftol before the
        //radial caller chooses cells. A singular local contact has word0;
        //its independently retained/current native center stays unchanged.
        const auto cell=actorCells_.coordinate(static_cast<float>(nativeImpactCoordinateWord(shot.position.x)),
                                              static_cast<float>(nativeImpactCoordinateWord(shot.position.y)));
        if(cell!=NativeActorCells::Missing){
            const int cx=int(cell/size_t(actorCells_.height)),cy=int(cell%size_t(actorCells_.height));
            const int rx=(radius>>6)+1,ry=(radius>>5)+1;
            for(int x=std::max(0,cx-rx);x<=std::min(actorCells_.width-1,cx+rx);++x)
                for(int y=std::max(0,cy-ry);y<=std::min(actorCells_.height-1,cy+ry);++y){
                    size_t id=actorCells_.heads[size_t(x)*size_t(actorCells_.height)+size_t(y)];
                    auto bare=bareAmmoCells_.heads[size_t(x)*size_t(actorCells_.height)+size_t(y)];
                    while(id!=NativeActorCells::Missing || bare!=NativeAmmoCells::Missing){
                        const auto bareLink=bareAmmoCells_.links.find(bare);
                        const bool haveBare=bareLink!=bareAmmoCells_.links.end();
                        const auto actorOrder=id<actorCells_.arrivalOrder.size()?actorCells_.arrivalOrder[id]:0;
                        if(nativeBareAmmoVisitsFirst(actorOrder,haveBare?bareLink->second.arrivalOrder:0,
                                                     id!=NativeActorCells::Missing,haveBare)){
                            const auto index=bareAmmoIndex_.find(bare);
                            if(index!=bareAmmoIndex_.end() && index->second<projectiles_.size()) {
                                const auto& victim=projectiles_[index->second];
                                if(victim.alive && victim.nativeBareBody){
                                    const auto& body=*victim.nativeBareBody;
                                    const auto& cls=scene_->classes[victim.classId.value];
                                    NativeRadialAdmissionInput in;
                                    in.targetSubtype=0x10;in.targetEnergy=body.energy;
                                    in.targetFlags=(body.killCounted?0x40u:0u)|
                                        ((victim.phaseHidden||cls.flags.invisible.value_or(false))?8u:0u)|
                                        (propertyBool(cls.properties,"Indestructible")?0x200000u:0u);
                                    in.targetClassFlags20=propertyBool(cls.properties,"Player-direct-fire-only")?0x40u:0u;
                                    in.targetIsSource=shot.nativeBodyIdentity==bare;
                                    in.sourcePresent=true;in.sourceSubtype=0x10;in.sourcePlayerOwned=playerOwned;
                                    in.targetAge=ageWord(victim.ageMs);in.sourceAge=ageWord(shot.ageMs);
                                    in.tickMilliseconds=ageWord(tickMilliseconds);
                                    if(nativeRadialAdmitted(in)){
                                        const long double dx=static_cast<long double>(body.motion.center.x)-center.x;
                                        const long double dz=static_cast<long double>(body.motion.center.y)-center.y;
                                        const float amount=nativeAmmoRadialDamage(shot.damage,shot.damageRadius,dx*dx+dz*dz);
                                        if(amount!=0)applyNativeBareAmmoDamage(bare,amount,shot,playerOwned);
                                    }
                                }
                            }
                            // Native next is read after damage/callback, independently of the actor list.
                            const auto after=bareAmmoCells_.links.find(bare);
                            bare=after==bareAmmoCells_.links.end()?NativeAmmoCells::Missing:after->second.next;
                            continue;
                        }
                        if(id==NativeActorCells::Missing)break;
                        // Death is deferred; construction may grow vectors but
                        //stable actor identities and the current next link remain valid.
                        if(id>=actors_.size())break;
                        const auto& a=actors_[id];const auto& object=scene_->objects[id];
                        NativeRadialAdmissionInput in;
                        in.targetSubtype=nativeDamageSubtype(id);in.targetFlags=nativeDamageFlags(id);
                        in.targetClassFlags20=nativeDamageClassFlags(id);in.targetEnergy=a.vitals.energy;
                        in.targetIsSource=shot.visual==id;in.sourcePresent=true;in.sourceSubtype=0x10;
                        in.sourcePlayerOwned=playerOwned;in.targetAge=ageWord(a.ageMs);
                        in.sourceAge=ageWord(shot.ageMs);in.tickMilliseconds=ageWord(tickMilliseconds);
                        if(const auto* item=attribute(object.properties,"item"))in.onlyForPlayerItem=propertyBool(item->children,"Only-for-player");
                        if((a.alive||a.reactivateMs>=0) && nativeRadialAdmitted(in)){
                            const auto surface=object.position;
                            const auto p=a.nativeMotionHistory()?a.nativeMotionHistory()->center:
                                NativeNavPoint{spiderSurfaceToNative(surface.x,surface.y).x,spiderSurfaceToNative(surface.x,surface.y).y};
                            const long double dx=static_cast<long double>(p.x)-center.x,dz=static_cast<long double>(p.y)-center.y;
                            const float amount=nativeAmmoRadialDamage(shot.damage,shot.damageRadius,dx*dx+dz*dz);
                            if(amount!=0)applyNativeAmmoDamage(id,amount,shot,true,playerOwned);
                        }
                        id=id<actorCells_.next.size()?actorCells_.next[id]:NativeActorCells::Missing;
                    }
                }
        }
    }
    //43d350: duplicate or invalid TYPE FX returns from the entire direct phase.
    if(!spawnProjectileHitEffect(shot,contact))return;
    if(target && *target<actors_.size()){
        const auto projected=nativeToSurface(shot.directionX,shot.directionZ);
        applyAmmoImpulse(*target,nativeDirectionFromVector(projected.x,projected.y));
        const float direct=nativeAbsorbedAmmoDamage(scene_->objects[*target].properties,
            scene_->classes[shot.classId.value].name,shot.damage);
        applyNativeAmmoDamage(*target,direct,shot,false);
    }
    if(const auto owner=nativeAmmoOwner(shot))
        nativeAmmoOwnerDirectContact(actors_[*owner].nativeAmmoOwner,true,target&&*target<actors_.size());
}
}
