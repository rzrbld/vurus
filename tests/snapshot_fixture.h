#pragma once
#include "game/save_state.h"
#include "game/properties.h"
#include "game/spider_contact_native.h"
#include <stdexcept>

namespace vyruz_test {
//Legacy mechanics fixtures represent actively simulated authored classes.
//The resolved original definitions explicitly set Managed; raw absent isfalse.
inline bool loadManagedFixture(vyruz::GameWorld& world,vyruz::SceneContent& scene,
        const vyruz::Level& level,std::string& error,int difficulty=1) {
    for(auto& cls:scene.classes)if(!vyruz::contentProperty(cls.properties,"Managed")){
        vyruz::TextNode flag;flag.kind=vyruz::TextKind::Assignment;flag.key="Managed";flag.values={"1"};
        cls.properties.push_back(std::move(flag));
    }
    return world.load(scene,level,error,difficulty);
}
// Explicit synthetic fixture edits must keep the real v32 cell/identity
//invariants. These helpers do not weaken production checkpoint validation.
inline void synchronizeActor(vyruz::WorldSnapshot& s,std::size_t id) {
    if(!s.actorCells || id>=s.actors.size())throw std::logic_error("fixture requires retained actor cells");
    const auto& a=s.actors[id];const auto p=s.objects[id].position;
    const auto cell=a.alive||a.reactivateMs>=0?s.actorCells->coordinate(p.x,p.y):SIZE_MAX;
    if(s.actorCells->cell[id]!=cell)s.actorCells->relocate(id,cell,cell==SIZE_MAX?0:s.nextNativeCellOrder++);
}
//Explicit physical removal fixture: preserve sparse-manager and allocator
//invariants rather than zeroing a UID while leaving its pool body owned.
inline void releaseActor(vyruz::WorldSnapshot& s,std::size_t id) {
    auto& actor=s.actors.at(id);
    if(s.nativePool && actor.nativePoolSlot!=SIZE_MAX){vyruz::NativeActorPool pool;
        if(!pool.restore(s.nativePool->pool))throw std::logic_error("invalid fixture pool");
        if(actor.nativeGeneration)pool.release(actor.nativePoolSlot);
        s.nativePool->pool=pool.snapshot();
    }
    actor.nativePoolSlot=SIZE_MAX;actor.nativeGeneration=0;actor.nativeAwake=false;
    if(s.nativeAwake && s.nativeAwake->actors.at(id)){
        s.nativeAwake->manager.erase(*s.nativeAwake->actors[id]);s.nativeAwake->actors[id].reset();
    }
    actor.nativeAwakeIndex=0;
    for(auto& shot:s.projectiles)if(shot.visual==id)shot.nativeGeneration=0;
    synchronizeActor(s,id);
}
inline void appendProjectile(vyruz::WorldSnapshot& s,const vyruz::SceneContent& scene,vyruz::Projectile shot) {
    shot.nativeOwnerGeneration=shot.owner<s.actors.size()?s.actors[shot.owner].nativeGeneration:0;
    if(shot.visual)shot.nativeGeneration=s.actors[*shot.visual].nativeGeneration;
    else {
        const auto& cls=scene.classes[shot.classId.value];
        if(s.nativePool){
            vyruz::NativeActorPool pool;
            if(!pool.restore(s.nativePool->pool))throw std::logic_error("fixture requires valid native pool");
            const auto allocation=pool.allocate();shot.nativePoolSlot=allocation.slot;shot.nativeGeneration=allocation.generation;
            s.nativePool->pool=pool.snapshot();s.nextNativeGeneration=pool.nextGeneration();
        }else shot.nativeGeneration=s.nextNativeGeneration++;
        shot.nativeBodyIdentity=s.nextNativeBodyIdentity++;
        vyruz::NativeBareAmmoBody body;
        if(const auto* e=vyruz::contentProperty(cls.properties,"Energy"))body.energy=vyruz::propertyNumber(e->children,"Initial",1);else body.energy=1;
        if(const auto* sh=vyruz::contentProperty(cls.properties,"Shield"))body.shield=vyruz::propertyNumber(sh->children,"Initial");
        const auto p=vyruz::spiderSurfaceToNative(shot.position.x,shot.position.y);
        body.motion={{p.x,p.y},shot.position};
        if(s.nativeAwake && vyruz::nativeClassManaged(cls.properties)){
            body.awake=true;body.awakeIndex=s.nativeAwake->manager.append(vyruz::NativeAwakeAmmoBit|shot.nativeBodyIdentity);
            s.nativeAwake->ammo[shot.nativeBodyIdentity]=body.awakeIndex;
        }
        shot.nativeBareBody=body;
        if(!s.actorCells)throw std::logic_error("synthetic Ammo requires retained actor cells");
        const auto cell=shot.alive?s.actorCells->coordinate(shot.position.x,shot.position.y):SIZE_MAX;
        if(cell!=SIZE_MAX)s.bareAmmoCells.relocate(shot.nativeBodyIdentity,cell,s.nextNativeCellOrder++);
    }
    s.projectiles.push_back(std::move(shot));
}
// Intentional pre32 migration fixtures must use old graph references as well
//as absent new fields; merely flipping the known marker produces a fake file.
inline void pre32AmmoLifecycle(vyruz::WorldSnapshot& s) {
    s.nativePool.reset();s.nativeAwake.reset();s.nativePlayerFacing.reset();
    s.nativeAmmoLifecycleKnown=false;s.bareAmmoCells={};
    s.nextNativeGeneration=1;s.nextNativeCellOrder=s.nextNativeBodyIdentity=1;
    for(auto& a:s.actors){a.nativeGeneration=a.nativeCreatorGeneration=0;a.nativeAmmoOwner={};}
    for(auto& h:s.heldItems){h.actor.nativeGeneration=h.actor.nativeCreatorGeneration=0;h.actor.nativeAmmoOwner={};}
    for(auto& p:s.projectiles){
        if(s.spiderNavigation && p.spiderGraphNode!=SIZE_MAX){auto& ref=s.spiderNavigation->objects[p.spiderGraphNode].reference;
            ref=(ref&~((std::uint64_t(1)<<61)-1))|p.spiderGraphNode;}
        p.nativeGeneration=p.nativeOwnerGeneration=0;p.nativeBodyIdentity=0;p.nativeBareBody.reset();
    }
}
} //namespace vyruz_test
