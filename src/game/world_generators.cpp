#include "game/world.h"
#include "game/native_body_shape.h"
#include "game/properties.h"
#include <algorithm>
#include <cmath>
namespace vyruz {
void GameWorld::configureGenerators(ActorState& actor,const ObjectPlacement& object) {
    actor.generators.clear();actor.generatorsInitialized=true;
    // 413550 reverses the class linked list back into authored TYPE order.
    for(const auto& node:object.properties) {
        if(folded(node.key)!="type" || node.arguments.empty() || folded(node.arguments[0])!="generator")continue;
        GeneratorState g;
        if(const auto* product=findClass(*scene_,propertyText(node.children,"Product")))g.product=product->id;
        g.maxBirths=static_cast<int>(propertyNumber(node.children,"Max-births"));
        g.birthCount=static_cast<int>(propertyNumber(node.children,"Birth-count"));
        g.delayMs=propertyNumber(node.children,"Birth-delay");g.durationMs=propertyNumber(node.children,"Birth-duration");
        g.active=propertyBool(node.children,"Active");g.stressable=propertyBool(node.children,"Stressable");
        g.stablePopulation=propertyBool(node.children,"Stable-population");
        // 405210 zeroes +24; construction copies it unchanged. Active starts
        // with a birth attempt, not an invented initial Birth-duration wait.
        actor.generators.push_back(g);
    }
    // Placement parser 4157a0 case69 also seeds the countdown, unlike the
    // class parser. Placement TYPE overlays claim matching records by ordinal.
    size_t ordinal=0;
    for(const auto& node:object.authored.children) {
        if(folded(node.key)!="type" || node.arguments.empty() || folded(node.arguments[0])!="generator")continue;
        if(ordinal<actor.generators.size() && contentProperty(node.children,"Birth-delay"))
            actor.generators[ordinal].remainingMs=propertyNumber(node.children,"Birth-delay");
        ++ordinal;
    }
    if(!actor.generators.empty())actor.solid=actor.alive && actor.radius>0 &&
        std::any_of(actor.generators.begin(),actor.generators.end(),[](const auto& g){return g.active;});
}
bool GameWorld::generatorOccupied(size_t source) {
    //405df0 refreshes current BBOX storage from float surface coordinates, then
    //walks cached cells X-major/Y-minor in a fixed3x3 window. Actual head order
    //merges compact Ammo and Actor bodies on their shared insertion ordinal.
    const auto actorBounds=[&](size_t id) {
        auto& actor=actors_[id];const auto p=scene_->objects[id].position;
        NativeGeneratorBounds bounds;
        const auto shape=nativeBodyContactShape(nativeDamageSubtype(id),actor.radius);
        bounds.words[0]=shape;
        if(shape==4) {
            if(!actor.nativeGeneratorSquare)
                actor.nativeGeneratorSquare=makeNativeGeneratorSquare(p.x,p.y,static_cast<int32_t>(actor.radius));
            auto& square=*actor.nativeGeneratorSquare;refreshNativeGeneratorSquare(square,p.x,p.y);
            bounds.value(9,square.centerX);bounds.value(10,square.centerZ);
            bounds.value(11,square.minX);bounds.value(12,square.minZ);
            bounds.value(13,square.maxX);bounds.value(14,square.maxZ);
        } else {
            bounds.value(1,actor.radius);refreshNativeGeneratorBounds(bounds,p.x,p.y);
        }
        if(auto* history=actor.nativeMotionHistory())history->center={bounds.value(9),bounds.value(10)};
        else actor.nativeMotion=SpiderMotionHistory{{bounds.value(9),bounds.value(10)},p};
        return bounds;
    };
    const auto bounds=actorBounds(source);
    const auto cell=source<actorCells_.cell.size()?actorCells_.cell[source]:NativeActorCells::Missing;
    if(cell==NativeActorCells::Missing)return false;
    const int cx=int(cell/size_t(actorCells_.height)),cy=int(cell%size_t(actorCells_.height));
    for(int x=std::max(0,cx-1);x<=std::min(actorCells_.width-1,cx+1);++x)
        for(int y=std::max(0,cy-1);y<=std::min(actorCells_.height-1,cy+1);++y) {
            const size_t at=size_t(x)*size_t(actorCells_.height)+size_t(y);
            size_t id=actorCells_.heads[at];auto bare=bareAmmoCells_.heads[at];
            while(id!=NativeActorCells::Missing||bare!=NativeAmmoCells::Missing) {
                const auto link=bareAmmoCells_.links.find(bare);
                const bool haveBare=link!=bareAmmoCells_.links.end();
                const auto actorOrder=id<actorCells_.arrivalOrder.size()?actorCells_.arrivalOrder[id]:0;
                if(nativeBareAmmoVisitsFirst(actorOrder,haveBare?link->second.arrivalOrder:0,
                                            id!=NativeActorCells::Missing,haveBare)) {
                    const auto next=link->second.next;const auto found=bareAmmoIndex_.find(bare);
                    if(found!=bareAmmoIndex_.end()&&found->second<projectiles_.size()) {
                        auto& shot=projectiles_[found->second];const auto& cls=scene_->classes[shot.classId.value];
                        const bool keep=shot.phaseConfigReady?shot.keepBoundsWhenHidden:
                            propertyBool(cls.properties,"Keep-bb-when-invisible");
                        const bool disabled=shot.radius<=0||!ammoContactEnabled(shot)||
                            ((shot.phaseHidden||cls.flags.invisible.value_or(false))&&!keep);
                        if(nativeGeneratorCandidate(3,disabled?1:0,0x10,false)) {
                            NativeGeneratorBounds other;other.words[0]=nativeBodyContactShape(0x10,shot.radius);
                            other.value(1,shot.radius);refreshNativeGeneratorBounds(other,shot.position.x,shot.position.y);
                            if(shot.nativeBareBody)shot.nativeBareBody->motion.center={other.value(9),other.value(10)};
                            if(nativeGeneratorBoundsOverlap(bounds,other))return true;
                        }
                    }
                    bare=next;continue;
                }
                if(id==NativeActorCells::Missing||id>=actors_.size())break;
                const auto& actor=actors_[id];const auto& object=scene_->objects[id];
                const auto subtype=nativeDamageSubtype(id);
                const bool disabled=actor.radius<=0||actor.ammoContactDisabled||
                    ((actor.gate||actor.lift||!actor.generators.empty())&&!actor.solid)||
                    ((object.flags.invisible.value_or(false)||actor.ammoHidden||
                      (!actor.alive&&actor.reactivateMs>=0))&&
                        !propertyBool(object.properties,"Keep-bb-when-invisible"));
                // TYPE0x13 is Item; its bit1 is Only-for-player. Gate's TYPE
                // tag is4 and must not be confused with actor subtype0x14.
                const auto* item=attribute(object.properties,"item");
                const bool playerOnlyItemType=item&&propertyBool(item->children,"Only-for-player");
                if(id!=source&&nativeGeneratorCandidate(3,disabled?1:0,subtype,playerOnlyItemType)&&
                   nativeGeneratorBoundsOverlap(bounds,actorBounds(id)))return true;
                id=actorCells_.next[id];
            }
        }
    return false;
}
void GameWorld::updateGenerators(float ms) {
    const size_t count=actors_.size();
    for(size_t id=0;id<count;++id)updateGenerator(id,ms);
}
void GameWorld::updateGenerator(size_t id,float ms) {
        //4372a0 is the current body's tail, after its death/contact admission.
        if(id>=actors_.size() || !actors_[id].alive || actors_[id].nativeDamagePending)return;
        const size_t count=actors_[id].generators.size();
        for(size_t slot=0;slot<count;++slot) {
            // spawn can reallocate both actor and object vectors: never retain
            // references over that call.
            auto& g=actors_[id].generators[slot];
            if(!g.product || (g.maxBirths>=0 && g.birthCount>=g.maxBirths) || (g.delayMs==0 && g.durationMs==0))continue;
            // Native compares dt before stress doubles the subtraction. An
            // underflow caused by stress is consumed on the following tick.
            if(ms<g.remainingMs) {
                const auto* energy=contentProperty(scene_->classes[scene_->objects[id].classId.value].properties,"Energy");
                const float initial=energy?propertyNumber(energy->children,"Initial",1):1;
                g.remainingMs-=ms*(g.stressable && actors_[id].vitals.energy<initial?2:1);continue;
            }
            if(!g.active && actors_[id].solid)continue;
            if(generatorOccupied(id))continue;
            if(!g.active) {g.remainingMs=g.durationMs;g.active=true;actors_[id].solid=actors_[id].radius>0;continue;}
            const auto product=*g.product;
            const size_t born=spawn(product,scene_->objects[id].position);
            if(born>=actors_.size())continue;
            actors_[born].generatorParent=id;
            if(actors_[born].hudEnemy)++bonusEnemies_; // 4372a0 successful hostile birth
            auto& current=actors_[id].generators[slot];++current.birthCount;
            current.remainingMs=current.delayMs;current.active=false;actors_[id].solid=false;
        }
}
}
