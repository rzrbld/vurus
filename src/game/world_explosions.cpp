#include "game/world.h"
#include "game/properties.h"
#include "game/ammo_radial_native.h"
#include "game/ammo_radial_contract_native.h"
#include "game/spider_contact_native.h"
#include <algorithm>
#include <cmath>

namespace vyruz {
bool GameWorld::effectPositionAdmitted(ContentPosition position) const {
    if(!effectCamera_)return true;
    if(!std::isfinite(position.x)||!std::isfinite(position.y))return false;
    auto camera=*effectCamera_;
    camera.surfaceWidth=level_->surfaceWidth>0?level_->surfaceWidth:level_->mapW*64;
    camera.surfaceHeight=level_->surfaceHeight>0?level_->surfaceHeight:level_->mapH*32;
    const auto player=playerPosition();camera.playerX=player.x;camera.playerY=player.y;
    // Native callers convert floats through __ftol (toward zero). Hit FX
    // supplies its already-clamped point; death FX supplies the source point.
    if(position.x < -2147483648.0 || position.x >= 2147483648.0 ||
       position.y < -2147483648.0 || position.y >= 2147483648.0)return false;
    return camera.admits(static_cast<int32_t>(position.x),static_cast<int32_t>(position.y));
}
const GafSequence* GameWorld::effectSequence(const SpriteReference& sprite) {
    const auto key=std::pair{sprite.path,sprite.sequence.value};
    auto found=effectSequences_.find(key);
    if(found==effectSequences_.end()) {
        std::optional<GafSequence> sequence;
        std::vector<uint8_t> bytes;GafAnimation bank;
        if(effectReader_ && sprite.sequence.value && effectReader_(sprite.path,bytes) &&
           decodeGafAnimation(bytes,bank,GafDecodeMode::Metadata) && sprite.sequence.value<=bank.sequences.size())
            sequence=std::move(bank.sequences[sprite.sequence.value-1]);
        found=effectSequences_.emplace(key,std::move(sequence)).first;
        if(!found->second)diagnostics_.push_back("Explosion animation unavailable: "+sprite.path);
    }
    return found->second?&*found->second:nullptr;
}
void GameWorld::spawnExplosions(const ObjectPlacement& object,std::optional<size_t> creator,float altitude,bool topPhase,
                                std::optional<std::uint32_t> capturedCreatorGeneration) {
    // 437d40: TYPE Explosion is transferred to a newly created TYPE FX actor.
    // Copy source metadata: spawn may reallocate both actor and object vectors.
    const auto properties=object.properties;const auto position=object.position;
    const auto creatorGeneration=capturedCreatorGeneration.value_or(
        creator&&*creator<actors_.size()?actors_[*creator].nativeGeneration:0);
    if(attribute(properties,"fx"))return;
    // 437d40 checks the SOURCE CLASS flag, not a placement override or the
    // Boom-fx class. Authored Ammo sets this flag and bypasses the camera gate.
    const auto& sourceClass=scene_->classes[object.classId.value];
    if(!propertyBool(sourceClass.properties,"Graphic-update-needed") &&
       !effectPositionAdmitted(position))return;
    for(const auto& node:properties) {
        if(folded(node.key)!="type" || node.arguments.empty() || folded(node.arguments.front())!="explosion")continue;
        const auto* cls=findClass(*scene_,propertyText(node.children,"Boom-fx"));
        if(!cls || !attribute(cls->properties,"fx"))continue;
        //437ec6 checks class bit40000 (Z-shadow), not bit80000
        //(In-ground), unlike the separate projectile impact producers.
        const bool ground=cls->flags.zShadow.value_or(false);
        const auto id=spawn(cls->id,position);
        if(id>=actors_.size())continue;
        //444ed4 later passes the constructed native bounds center, which is
        //independent of the post-construction surface offset below.
        if(!actors_[id].nativeMotionHistory()) {
            const auto center=spiderSurfaceToNative(position.x,position.y);
            actors_[id].nativeMotion=SpiderMotionHistory{{center.x,center.y},position};
        }
        //437d40 offsets non-Z-shadow first-piece FX surface Y by4 after
        // construction. Immediate CREATE products keep the original origin.
        if(topPhase && !ground)scene_->objects[id].position.y+=4;
        synchronizeActorCell(id);
        //437eab..437ebd copies the dying source's captured UID, including
        //a stale creator link. Do not recapture a reused owner's new UID.
        scene_->objects[id].owner=creator&&*creator<actors_.size()?
            std::optional<ObjectId>{ObjectId{static_cast<std::uint32_t>(*creator)}}:std::nullopt;
        actors_[id].nativeCreatorGeneration=creatorGeneration;
        auto& actor=actors_[id];actor.solid=false;actor.enemy=false;actor.effect=true;
        actor.projectileAltitude=ground?0:altitude;
        PendingExplosion blast;
        blast.damage=propertyNumber(node.children,"Damage");blast.radius=propertyNumber(node.children,"Damage-zone");
        blast.creator=creator;blast.altitude=*actor.projectileAltitude;
        // Native initializes an active animation before consuming Explosion.
        if(const auto* sequence=effectSequence(scene_->objects[id].sprite)) {
            AnimationCursor cursor;cursor.reset(*sequence);blast.animation=cursor.capture();blast.initialized=true;
        }
        actor.explosion=blast;
    }
}
void GameWorld::applyNativeExplosionRadial(size_t sourceId,float damage,float authoredRadius) {
    if(sourceId>=actors_.size())return;
    const int radius=std::isfinite(authoredRadius) && authoredRadius>=1 && authoredRadius<=46340?
        static_cast<int>(authoredRadius):0;
    //43cb20 rejects zero damage/radius before invoking creator ancestry.
    if(!radius || damage==0)return;
    //444ed4 passes the FX body itself to43cb20. The temporary source carries
    //its actual captured owner link, never the older Explosion creator field.
    Projectile source;
    const auto creator=scene_->objects[sourceId].owner;
    source.owner=creator?size_t(creator->value):SIZE_MAX;
    source.nativeOwnerGeneration=actors_[sourceId].nativeCreatorGeneration;
    source.visual=sourceId;
    //43cb72 validates ancestry even when the cell window has no recipient.
    const bool playerOwned=nativeAmmoPlayerOwned(source);
    if(sourceId>=actorCells_.cell.size())return;
    const auto cell=actorCells_.cell[sourceId];
    if(cell==NativeActorCells::Missing)return;
    const auto surface=scene_->objects[sourceId].position;
    const auto projected=spiderSurfaceToNative(surface.x,surface.y);
    const auto center=actors_[sourceId].nativeMotionHistory()?actors_[sourceId].nativeMotionHistory()->center:
        NativeNavPoint{projected.x,projected.y};
    const int cx=int(cell/size_t(actorCells_.height)),cy=int(cell%size_t(actorCells_.height));
    const int rx=(radius>>6)+1,ry=(radius>>5)+1;
    for(int x=std::max(0,cx-rx);x<=std::min(actorCells_.width-1,cx+rx);++x)
        for(int y=std::max(0,cy-ry);y<=std::min(actorCells_.height-1,cy+ry);++y) {
            const auto at=size_t(x)*size_t(actorCells_.height)+size_t(y);
            size_t id=actorCells_.heads[at];auto bare=bareAmmoCells_.heads[at];
            while(id!=NativeActorCells::Missing || bare!=NativeAmmoCells::Missing) {
                const auto bareLink=bareAmmoCells_.links.find(bare);
                const bool haveBare=bareLink!=bareAmmoCells_.links.end();
                const auto actorOrder=id<actorCells_.arrivalOrder.size()?actorCells_.arrivalOrder[id]:0;
                if(nativeBareAmmoVisitsFirst(actorOrder,haveBare?bareLink->second.arrivalOrder:0,
                                             id!=NativeActorCells::Missing,haveBare)) {
                    const auto index=bareAmmoIndex_.find(bare);
                    if(index!=bareAmmoIndex_.end() && index->second<projectiles_.size()) {
                        const auto& victim=projectiles_[index->second];
                        if(victim.alive && victim.nativeBareBody) {
                            const auto& body=*victim.nativeBareBody;
                            const auto& cls=scene_->classes[victim.classId.value];
                            NativeRadialAdmissionInput in;
                            in.targetSubtype=0x10;in.targetEnergy=body.energy;
                            in.targetFlags=(body.killCounted?0x40u:0u)|
                                ((victim.phaseHidden||cls.flags.invisible.value_or(false))?8u:0u)|
                                (propertyBool(cls.properties,"Indestructible")?0x200000u:0u);
                            in.targetClassFlags20=propertyBool(cls.properties,"Player-direct-fire-only")?0x40u:0u;
                            in.sourcePresent=true;in.sourceSubtype=0x11;in.sourcePlayerOwned=playerOwned;
                            if(nativeRadialAdmitted(in)) {
                                const long double dx=static_cast<long double>(body.motion.center.x)-center.x;
                                const long double dz=static_cast<long double>(body.motion.center.y)-center.y;
                                const float amount=nativeAmmoRadialDamage(damage,authoredRadius,dx*dx+dz*dz);
                                if(amount!=0)applyNativeBareAmmoDamage(bare,amount,source,playerOwned,false);
                            }
                        }
                    }
                    //43cb20 reads the current link after health/callback.
                    const auto after=bareAmmoCells_.links.find(bare);
                    bare=after==bareAmmoCells_.links.end()?NativeAmmoCells::Missing:after->second.next;
                    continue;
                }
                if(id==NativeActorCells::Missing || id>=actors_.size())break;
                const auto& target=actors_[id];const auto& object=scene_->objects[id];
                NativeRadialAdmissionInput in;
                in.targetSubtype=nativeDamageSubtype(id);in.targetFlags=nativeDamageFlags(id);
                in.targetClassFlags20=nativeDamageClassFlags(id);in.targetEnergy=target.vitals.energy;
                in.targetIsSource=id==sourceId;in.sourcePresent=true;in.sourceSubtype=0x11;in.sourcePlayerOwned=playerOwned;
                if(const auto* item=attribute(object.properties,"item"))in.onlyForPlayerItem=propertyBool(item->children,"Only-for-player");
                if((target.alive||target.reactivateMs>=0) && nativeRadialAdmitted(in)) {
                    const auto p=object.position;const auto native=spiderSurfaceToNative(p.x,p.y);
                    const auto position=target.nativeMotionHistory()?target.nativeMotionHistory()->center:NativeNavPoint{native.x,native.y};
                    const long double dx=static_cast<long double>(position.x)-center.x,dz=static_cast<long double>(position.y)-center.y;
                    const float amount=nativeAmmoRadialDamage(damage,authoredRadius,dx*dx+dz*dz);
                    if(amount!=0)applyNativeAmmoDamage(id,amount,source,true,playerOwned,false);
                }
                id=id<actorCells_.next.size()?actorCells_.next[id]:NativeActorCells::Missing;
            }
        }
}
void GameWorld::updateExplosions(float milliseconds) {
    const size_t count=actors_.size();
    for(size_t id=0;id<count;++id)updateExplosion(id,milliseconds);
}
void GameWorld::updateExplosion(size_t id,float milliseconds) {
        if(id>=actors_.size() || !actors_[id].alive || !actors_[id].explosion)return;
        auto& blast=*actors_[id].explosion;
        const auto* sequence=effectSequence(scene_->objects[id].sprite);
        if(!sequence)return; // No invented timeout for an uninitialized animation.
        AnimationCursor cursor;
        if(!blast.initialized) {cursor.reset(*sequence);blast.initialized=true;}
        else if(!cursor.restore(*sequence,blast.animation))return;
        const bool active=cursor.frame(*sequence)!=nullptr;
        // 440cee -> 431ae0 advances FX graphics before 442aa9 -> 444ca0
        // consumes Explosion's halfway cursor. Keep the visit's binding
        // admission: native +5c remains bound when a long step finishes, so
        // an already-due timer may still fire before the FX is retired.
        cursor.advance(*sequence,milliseconds);blast.animation=cursor.capture();
        const bool halfway=cursor.capture().cursor >= (sequence->subframes.size()>>1);
        bool apply=false;
        if(active && blast.damage!=0) {
            if(halfway || (blast.remainingMs>0 && blast.remainingMs<=milliseconds))apply=true;
            else if(blast.remainingMs>0)blast.remainingMs-=milliseconds;
        }
        const auto payload=blast; // damage/destroy may grow actors and invalidate references.
        if(apply) {
            blast.remainingMs=-1;
            applyNativeExplosionRadial(id,payload.damage,payload.radius);
            //444edc clears the live payload after the radial callback. The
            //callback can grow actors or remove this FX; reacquire by identity.
            if(id<actors_.size() && actors_[id].explosion)actors_[id].explosion->damage=0;
        }
        if(!cursor.frame(*sequence)) {
            actors_[id].alive=false;actors_[id].solid=false;actors_[id].explosion.reset();
            releaseNativeActorIdentity(id);
            synchronizeActorCell(id);
        }
}
}
