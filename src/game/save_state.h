#pragma once
#include "game/world.h"
#include "game/ordinary_enemy_ai_validation.h"
#include "render/scene_state.h"
namespace vyruz {
struct WorldSnapshot {
    int32_t bodyGraphicsStepMs=0; // Last native101c delta retained by active redraw.
    std::optional<NativeActorCells> actorCells;
    std::optional<NativeNavGraph> spiderNavigation;
    uint64_t catalogHash=0;
    int difficulty=1;
    std::vector<ObjectPlacement> objects;
    std::vector<ActorState> actors;
    std::vector<HeldItem> heldItems;
    // Older saves cannot distinguish consumed items from retained inventory.
    bool heldItemsKnown=true;
    std::vector<Projectile> projectiles;
    size_t player=0,navigationCursor=0;
    PlayerMotion motion;
    double elapsedMs=0;
    uint64_t playerKills=0,shotsFired=0,pickupsTaken=0,actorsDestroyed=0;
    uint64_t ammoHits=0,ammoMisses=0;
    std::optional<TransitionRequest> transition;
    std::set<size_t> rejectedLifts;
    std::string randomState,message;
    std::optional<size_t> savePlot;
    double saveDwellMs=0;
    bool saveAttempted=false;
    // Exact in-memory rollback state. Normal disk saves strip temporary
    // Surprises and restore their baseline resources, like native flags 0xb.
    SurpriseState surprise;
    // Native game-loop setup resets the short combat combo; disk saves omit it.
    // In-memory rollback retains it and the native enemy counter exactly.
    double fastKillMs=0;
    uint64_t fastKillCount=0;
    std::optional<int64_t> bonusEnemies;
    std::optional<size_t> endingSource;
    double endingRemainingMs=0;
    bool timedProtectionKnown=true;
    NativeRandomState nativeWorldRandom;
    NativeRandomState nativeGlobalRandom;
    NativeRandomState nativeReplayRandom;
    std::optional<NativeWorldPoolState> nativePool;
    std::optional<NativeAwakeState> nativeAwake;
    std::optional<NativePlayerFacingState> nativePlayerFacing;
    ContentPosition nativePlayerPreviousSurface;
    // Legacy snapshots lack native pointer generations and bare-Ammo bodies.
    // Reconstruct them once, using the currently bound original resources.
    bool nativeAmmoLifecycleKnown=false;
    NativeAmmoCells bareAmmoCells;
    uint32_t nextNativeGeneration=1;
    uint64_t nextNativeCellOrder=1,nextNativeBodyIdentity=1;
};
inline bool validNativeAmmoLifecycleSnapshot(const WorldSnapshot& s) {
    if(!s.nativeAmmoLifecycleKnown)return true;
    if(!s.actorCells || !s.bareAmmoCells.valid() ||
       s.bareAmmoCells.width!=s.actorCells->width || s.bareAmmoCells.height!=s.actorCells->height ||
       !s.nextNativeCellOrder || !s.nextNativeBodyIdentity)return false;
    std::set<uint64_t> orders,identities;
    for(auto order:s.actorCells->arrivalOrder)if(order &&
       (order>=s.nextNativeCellOrder || !orders.insert(order).second))return false;
    for(const auto& p:s.projectiles) {
        if(p.owner!=SIZE_MAX && p.owner>=s.actors.size())return false;
        if(p.visual) {
            if(*p.visual>=s.actors.size() || p.nativeBareBody || p.nativeBodyIdentity ||
               p.nativeGeneration!=s.actors[*p.visual].nativeGeneration)return false;
            continue;
        }
        if(!p.nativeBareBody || !p.nativeBodyIdentity || p.nativeBodyIdentity>=(uint64_t(1)<<61) || p.nativeBodyIdentity>=s.nextNativeBodyIdentity ||
           !identities.insert(p.nativeBodyIdentity).second)return false;
        const auto& b=*p.nativeBareBody;
        if(!std::isfinite(b.energy)||!std::isfinite(b.shield)||
           !std::isfinite(b.motion.center.x)||!std::isfinite(b.motion.center.y)||
           !std::isfinite(b.motion.previousSurface.x)||!std::isfinite(b.motion.previousSurface.y)||
           (b.damagePending && b.energy>0))return false;
        const auto expected=p.alive?s.actorCells->coordinate(p.position.x,p.position.y):NativeActorCells::Missing;
        const auto it=s.bareAmmoCells.links.find(p.nativeBodyIdentity);
        if(expected==NativeActorCells::Missing) {
            if(it!=s.bareAmmoCells.links.end())return false;
        } else if(it==s.bareAmmoCells.links.end() || it->second.cell!=expected)return false;
    }
    for(const auto& [id,link]:s.bareAmmoCells.links)
        if(!identities.contains(id) || link.arrivalOrder>=s.nextNativeCellOrder ||
           !orders.insert(link.arrivalOrder).second)return false;
    return true;
}
inline bool validNativeAwakeSnapshot(const WorldSnapshot& s) {
    if(s.nativePlayerFacing) {
        const auto& f=*s.nativePlayerFacing;
        if(f.pose<1||f.pose>48||f.desiredPose<1||f.desiredPose>48||f.previousPose<1||f.previousPose>48||
           f.logicalHeading<0||f.logicalHeading>=24||f.orthogonalHeading!=(f.logicalHeading+3)%24||
           f.periodMs<=0||f.periodMs>1000000||f.remainingMs< -100000000||f.remainingMs>100000000||
           !std::isfinite(s.nativePlayerPreviousSurface.x)||!std::isfinite(s.nativePlayerPreviousSurface.y))return false;
    }
    if(!s.nativeAwake)return true; //Explicit legacy migration boundary.
    const auto& a=*s.nativeAwake;
    if(a.actors.size()!=s.actors.size())return false;
    std::set<std::uint64_t> bodies;
    for(const auto& p:s.projectiles)if(p.nativeBareBody && p.nativeBodyIdentity)bodies.insert(p.nativeBodyIdentity);
    for(auto body:a.manager.entries) {
        if(body==NativeAwakeManager::Missing)continue;
        if(body&NativeAwakeAmmoBit){if(!bodies.contains(body&~NativeAwakeAmmoBit))return false;}
        else if(body>=s.actors.size())return false;
    }
    for(size_t id=0;id<a.actors.size();++id)if(a.actors[id]) {
        const auto index=*a.actors[id];
        if(index>=a.manager.entries.size() || a.manager.entries[index]!=id)return false;
    }
    for(const auto& [id,index]:a.ammo)
        if(!bodies.contains(id) || index>=a.manager.entries.size() ||
           a.manager.entries[index]!=(NativeAwakeAmmoBit|id))return false;
    return true;
}
inline bool validSpiderNavigationSnapshot(const WorldSnapshot& s) {
    if(!validNativeWorldPoolSnapshot(s))return false;
    if(!validNativeAwakeSnapshot(s))return false;
    if(!validNativeAmmoLifecycleSnapshot(s))return false;
    if(s.actorCells){
        if(!s.actorCells->valid(s.actors.size()) || s.objects.size()!=s.actors.size())return false;
        for(size_t id=0;id<s.actors.size();++id){
            const auto& a=s.actors[id];const auto p=s.objects[id].position;
            const auto expected=a.alive||a.reactivateMs>=0?s.actorCells->coordinate(p.x,p.y):NativeActorCells::Missing;
            if(s.actorCells->cell[id]!=expected)return false;
        }
    }
    for(const auto& a:s.actors)if(a.nativeDamagePending && ((!a.alive && a.reactivateMs<0)||a.vitals.energy>0))return false;
    for(const auto& a:s.actors)if(a.nativeAmmoContinuation && (!a.nativeMotionHistory() || (!a.alive && a.reactivateMs<0)))return false;
    if(s.spiderNavigation && !validNativeNavigationGraph(*s.spiderNavigation))return false;
    const auto point=[&](NativeNavPoint p){
        if(p.graphNode==SIZE_MAX)return true;
        if(!s.spiderNavigation || p.graphNode>=s.spiderNavigation->objects.size())return false;
        const auto& o=s.spiderNavigation->objects[p.graphNode];
        return o.alive&&o.point&&o.bounds.minX==p.x&&o.bounds.minY==p.y;
    };
    const auto route=[&](const SpiderRouteCore& r){
        for(auto p:r.points)if(!point(p))return false;
        if(r.search){for(auto p:r.search->seen)if(!point(p))return false;
            for(const auto& c:r.search->candidates)for(auto p:c.points)if(!point(p))return false;}
        return true;
    };
    if(s.spiderNavigation){std::set<uint64_t> references;
        for(const auto& o:s.spiderNavigation->objects)if(o.alive&&!o.point){
            if(!(o.reference&((uint64_t(1)<<63)|(uint64_t(1)<<62)))&&o.reference>=s.actors.size())return false;
            if(!references.insert(o.reference).second)return false;
        }
    }
    for(const auto& p:s.projectiles)if(p.spiderGraphNode!=SIZE_MAX){
        if(!s.spiderNavigation||p.spiderGraphNode>=s.spiderNavigation->objects.size())return false;
        const auto& o=s.spiderNavigation->objects[p.spiderGraphNode];
        if(!o.alive||o.point||!(o.reference&(uint64_t(1)<<62))||
           (o.reference&((uint64_t(1)<<61)-1))!=(s.nativeAmmoLifecycleKnown?p.nativeBodyIdentity:p.spiderGraphNode))return false;
    }
    for(const auto& a:s.actors)if(a.nativeMotion){const auto& m=*a.nativeMotion;
        if(!std::isfinite(m.center.x)||!std::isfinite(m.center.y)||!std::isfinite(m.previousSurface.x)||!std::isfinite(m.previousSurface.y))return false;
    }
    for(const auto& a:s.actors)if(a.spiderAI){const auto& ai=*a.spiderAI;
        if(!validSpiderAI(ai,s.actors.size())||!route(ai.patrolRoute)||!route(ai.seekerRoute)||
           (ai.patrolRoute.saved&&!route(*ai.patrolRoute.saved))||(ai.seekerRoute.saved&&!route(*ai.seekerRoute.saved)))return false;
    }
    return true;
}
// Native missile actions own search-created graph points until replacement.
// Keep validation shared by disk readers/writers and live world restoration.
inline bool validMissileNativeNavigationSnapshot(const WorldSnapshot& s) {
    const auto finitePoint=[](NativeNavPoint p){return std::isfinite(p.x)&&std::isfinite(p.y);};
    const auto finiteSurface=[](ContentPosition p){return std::isfinite(p.x)&&std::isfinite(p.y);};
    const auto member=[&](NativeNavPoint p){
        if(!finitePoint(p)||p.graphNode==SIZE_MAX||!s.spiderNavigation||
           p.graphNode>=s.spiderNavigation->objects.size())return false;
        const auto& o=s.spiderNavigation->objects[p.graphNode];
        return o.alive&&o.point&&o.bounds.minX==p.x&&o.bounds.minY==p.y;
    };
    std::set<size_t> claimed;
    const auto spiderRoute=[&](const SpiderRouteCore& r){
        const auto add=[&](NativeNavPoint p){if(p.graphNode!=SIZE_MAX)claimed.insert(p.graphNode);};
        for(auto p:r.points)add(p);
        if(r.search){for(auto p:r.search->seen)add(p);
            for(const auto& c:r.search->candidates)for(auto p:c.points)add(p);}
    };
    for(const auto& a:s.actors)if(a.spiderAI)for(const auto* r:{&a.spiderAI->patrolRoute,&a.spiderAI->seekerRoute}){
        spiderRoute(*r);if(r->saved)spiderRoute(*r->saved);
    }
    for(const auto& a:s.actors)if(a.ordinaryAI &&
        !validOrdinaryEnemyAI(*a.ordinaryAI,s.actors.size(),s.spiderNavigation,claimed))return false;
    const auto action=[&](const MissileNativeRoute& r,const MissileGuidanceState& state,
                           std::optional<size_t> target,const std::vector<ContentPosition>& cache,
                           size_t cursor,ContentPosition start){
        if(static_cast<unsigned>(state.phase)>static_cast<unsigned>(MissileGuidancePhase::Following)||
           state.pose48<1||state.pose48>48||!finiteSurface(start)||
           !std::isfinite(state.sampledTarget.x)||!std::isfinite(state.sampledTarget.y)||
           (target&&*target>=s.actors.size())||cursor>cache.size())return false;
        for(int timer:{state.scanRemainingMs,state.pathRemainingMs,state.turnRemainingMs})
            if(timer< -100000000||timer>100000000)return false;
        for(auto p:cache)if(!finiteSurface(p))return false;
        if(r.search){
            if(state.phase!=MissileGuidancePhase::Searching||!r.points.empty()||!r.ownedPoints.empty()||
               !cache.empty()||cursor!=0)return false;
            const auto& search=*r.search;
            if(!finitePoint(search.halfExtent)||search.halfExtent.x<0||search.halfExtent.y<0||
               !std::isfinite(search.maximumDistance)||search.maximumDistance<0||search.seen.size()<2)return false;
        }else {
            if(state.phase==MissileGuidancePhase::Searching)return false;
            if(r.points.empty()){
                if(!r.ownedPoints.empty()||!cache.empty()||cursor!=0||
                   state.phase==MissileGuidancePhase::Following)return false;
            }else if(r.points.size()<2||cache.size()+1!=r.points.size())return false;
        }
        const auto& owned=r.search?r.search->seen:r.ownedPoints;
        std::set<size_t> local;
        for(auto p:owned){if(!member(p)||!local.insert(p.graphNode).second||
            !claimed.insert(p.graphNode).second)return false;}
        const auto ownedMember=[&](NativeNavPoint p){return member(p)&&local.contains(p.graphNode);};
        for(auto p:r.points)if(!ownedMember(p))return false;
        if(r.search)for(const auto& c:r.search->candidates){
            if(c.points.empty()||c.cursor>=c.points.size())return false;
            for(auto p:c.points)if(!ownedMember(p))return false;
        }
        return true;
    };
    for(const auto& p:s.projectiles)if(p.guidanceNative){
        const auto& native=*p.guidanceNative;
        if(p.guidanceSearch||!p.guidanceClass||!action(native,p.guidance,p.guidanceTarget,p.guidanceRoute,p.guidancePoint,p.guidanceStart))return false;
        if(native.saved){const auto& saved=*native.saved;
            if(!native.search||!action(saved.route,saved.state,saved.target,saved.surfaceRoute,saved.point,saved.start))return false;
        }
    }
    return true;
}
inline void normalizeSavedSurprise(WorldSnapshot& s) {
    if(s.player<s.actors.size() && s.player<s.objects.size()) {
        auto& actor=s.actors[s.player];
        if(s.surprise.active(SurpriseKind::Shield) && actor.shieldUpStep)
            actor.shieldUpStep=std::max(0.f,*actor.shieldUpStep-10.f);
        if(s.surprise.active(SurpriseKind::Ammo))restoreAmmoBaseline(actor.ammo,s.surprise.savedAmmo);
        if(s.surprise.active(SurpriseKind::Drone)) {
            s.objects[s.player].position=s.surprise.droneOrigin;
            if(auto* history=actor.nativeMotionHistory()) {
                const auto n=spiderSurfaceToNative(s.surprise.droneOrigin.x,s.surprise.droneOrigin.y);
                *history={{n.x,n.y},s.surprise.droneOrigin};
            }
            s.objects[s.player].flags.invisible=s.surprise.originalInvisible;
            actor.solid=s.surprise.originalSolid;
            s.motion.velocityX=s.motion.velocityZ=s.motion.magnitude=s.motion.thrust=0;
            if(s.actorCells){
                const auto cell=s.actorCells->coordinate(s.objects[s.player].position.x,s.objects[s.player].position.y);
                if(s.nativeAmmoLifecycleKnown && s.actorCells->cell[s.player]!=cell)
                    s.actorCells->relocate(s.player,cell,s.nextNativeCellOrder++);
                else s.actorCells->relocate(s.player,cell);
            }
        }
    }
    std::erase_if(s.heldItems,[](const HeldItem& item){return item.surprise;});
    s.surprise={};
}
struct SavedLevel { WorldSnapshot world;SceneRendererState render; };
struct SaveGame { std::string currentLevel;std::map<std::string,SavedLevel> levels; std::optional<SavedLevel> restartLevel; };
}
