#include "game/world.h"
#include "game/enemy_weapon_readiness.h"
#include "game/properties.h"
#include "game/spider_contact_native.h"
#include "game/spider_turn_native.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
namespace vyruz {
namespace {
NativeNavPoint nativePoint(ContentPosition p){auto n=spiderSurfaceToNative(p.x,p.y);return {n.x,n.y};}
float distance2(NativeNavPoint a,NativeNavPoint b){float x=a.x-b.x,y=a.y-b.y;return x*x+y*y;}
int heading(NativeNavPoint a,NativeNavPoint b){return spiderContactHeading({b.x-a.x,b.y-a.y});}
bool terminal(const SpiderRouteState& r){return r.cursor>=r.points.size() || r.cursor+1==r.points.size();}
bool nextSegment(SpiderRouteState& r){
    if(r.cursor>=r.points.size())return false;
    const auto start=r.points[r.cursor++];
    if(r.cursor>=r.points.size())return false;
    r.segmentStart=start;r.segmentEnd=r.points[r.cursor];
    return heading(r.segmentStart,r.segmentEnd)!=24;
}
using PointHandles=std::unordered_set<size_t>;
PointHandles routeHandles(const SpiderRouteCore& route) {
    PointHandles result;
    const auto add=[&](NativeNavPoint p){if(p.graphNode!=SIZE_MAX)result.insert(p.graphNode);};
    for(auto p:route.points)add(p);
    if(route.search) {
        for(auto p:route.search->seen)add(p);
        for(const auto& candidate:route.search->candidates)for(auto p:candidate.points)add(p);
    }
    return result;
}
void releaseRoutePoints(NativeNavGraph& graph,const SpiderRouteCore& route,const PointHandles& keep={}) {
    for(auto handle:routeHandles(route))if(!keep.contains(handle))removeNativeNavigationObject(graph,handle);
}
const TextNode* typed(const ObjectPlacement& o,const char* name){
    for(const auto& n:o.properties)if(n.key=="TYPE"&&!n.arguments.empty()&&contentNameEqual(n.arguments[0],name))return &n;
    return nullptr;
}
}
void GameWorld::clearSpiderNavigationRoutes(size_t id) {
    if(id>=actors_.size() || !actors_[id].spiderAI)return;
    auto& state=*actors_[id].spiderAI;
    if(spiderNavigation_) {
        PointHandles handles;
        for(const auto* route:{&state.patrolRoute,&state.seekerRoute}) {
            const auto add=[&](const SpiderRouteCore& core){for(auto h:routeHandles(core))handles.insert(h);};
            add(*route);if(route->saved)add(*route->saved);
        }
        for(auto handle:handles)removeNativeNavigationObject(*spiderNavigation_,handle);
    }
    actors_[id].spiderAI.reset();markSpatialDirty(id);
}
bool GameWorld::advanceSpiderAI(size_t id,float seconds,std::span<const ContentPosition> path){
    auto& actor=actors_[id];const auto& object=scene_->objects[id];
    if(!actor.spiderAI){
        actor.spiderAI.emplace();auto& state=*actor.spiderAI;
        // Older port checkpoints have authored progress but no native route
        // action. Retain that goal and rebuild the missing route state.
        if(actor.brainState.initialized && actor.brainState.point>=0){
            state.patrol={true,actor.brainState.reverse,actor.brainState.point};
        }
        actor.route.clear();actor.routeSearch.reset();actor.routeGoal.reset();
    }
    auto& state=*actor.spiderAI;
    if(!state.motion){const auto n=spiderSurfaceToNative(object.position.x,object.position.y);state.motion=SpiderMotionHistory{{n.x,n.y},object.position};markSpatialDirty(id);}
    const auto& classProperties=scene_->classes[object.classId.value].properties;
    const EnemyTurnConfig turnConfig{0x1f,false,propertyBool(classProperties,"Can-turn-on-place"),
        static_cast<int>(propertyNumber(classProperties,"Turn-timeout",42)),0};
    if(!state.turn){
        state.turn=initializeSpiderTurn(turnConfig,actor.direction);
        // Older checkpoints retain visible pose/countdown, but not the desired
        // pose or logical ring. Preserve that pose and reconstruct a neutral
        // goal at it; this cannot recover previously lost original state.
        state.turn->bodyPose=state.turn->bodyDesiredPose=state.turn->bodyPreviousPose=actor.direction+1;
        state.turn->logicalHeading=(actor.direction/6)*6;
        state.turn->orthogonalHeading=(state.turn->logicalHeading+3)%24;
        state.turn->bodyRemainingMs=std::max(0,static_cast<int>(actor.turnMs));
    }
    auto& turning=*state.turn;beginSpiderTurnUpdate(turning);
    actor.spiderPatrolTurning=false;actor.enemyMoveCommand=0;
    const auto position=object.position;const auto native=state.motion->center;
    const float ms=static_cast<float>(std::floor(elapsedMs_)-std::floor(elapsedMs_-seconds*1000));
    EnemyMotionCommand command;bool firing=false,locked=false;
    const bool canTurn=propertyBool(object.properties,"Can-turn-on-place");
    const auto availableTarget=[&](std::optional<size_t> target){return target && *target<actors_.size() && actors_[*target].alive && !scene_->objects[*target].flags.invisible.value_or(false);};
    // 42da80/42d8f0/42ce40 read actor+40/+44 directly. Surface sampling
    // remains separate for the rescan checks at42c5a0.
    const auto targetCenter=[&](size_t target){
        if(const auto* history=actors_[target].nativeMotionHistory())return history->center;
        return nativePoint(scene_->objects[target].position);
    };
    const auto turn=[&](const SpiderRouteState& route){
        int desired=heading(route.segmentStart,route.segmentEnd);
        if(desired<24)desired=((desired+3)/6*6)%24;
        if(desired<24){
            advanceSpiderRouteTurn(turnConfig,turning,desired,static_cast<int>(ms),actor.enemyMagnitude,actor.speed);
            actor.direction=turning.bodyPose-1;
        }
    };
    const auto run=[&](SpiderRouteState& route,bool seeker){
        const auto* config=typed(object,seeker?"Seeker":"Patrol");
        const float scanDelay=config?propertyNumber(config->children,"Scan-Delay",seeker?actor.brain.scanDelayMs:250):seeker?actor.brain.scanDelayMs:250;
        const bool inBounds=spiderInsideRouteCorridor(native.x,native.y,route.segmentStart.x,route.segmentStart.y,
            route.segmentEnd.x,route.segmentEnd.y,actor.radius*2,actor.enemyMagnitude);
        bool unchanged=availableTarget(route.target);
        if(unchanged){const auto p=scene_->objects[*route.target].position;
            unchanged=terminal(route)?p.x==route.sampledTarget.x&&p.y==route.sampledTarget.y:
                int(p.x)/64==int(route.sampledTarget.x)/64&&int(p.y)/32==int(route.sampledTarget.y)/32;
        }
        const auto query=[&](NativeNavPoint from,NativeNavPoint to,NativeNavPoint extent,bool overlap){
            return spiderNavigationObstacle(id,route.target,from,to,extent,overlap);
        };
        const NativeNavPointFactory createPoint=[&](NativeNavPoint p){
            return spiderNavigation_?insertNativeNavigationPoint(*spiderNavigation_,p):p;
        };
        SpiderActionCallbacks callbacks;
        callbacks.beginSearch=[&]{
            ContentPosition goal;
            std::optional<size_t> selectedTarget;
            if(seeker){
                // All shipped Spider variants target the single Vyruz class.
                const auto target=playerPosition();const int direction=heading(native,targetCenter(player_));
                int gap=direction==24?0:std::abs(direction-turning.logicalHeading);gap=std::min(gap,24-gap);
                // 42da80 scans the projected native range rectangle before
                // the exact circular/cone test, even for its sole Vyruz target.
                const int rx=static_cast<int>(actor.brain.sensorRange*2.f*.8944272f)/64;
                const int ry=static_cast<int>(actor.brain.sensorRange*2.f*.4472136f)/32;
                const int tx=int(target.x)/64,ty=int(target.y)/32,cx=int(position.x)/64,cy=int(position.y)/32;
                const int cone=(static_cast<int>(actor.brain.visionDegrees)%361)/30;
                if(!availableTarget(player_) || actors_[player_].reactivateMs>=1 || actor.brain.sensorRange<0 ||
                   !actor.brain.targetClass || *actor.brain.targetClass!=scene_->objects[player_].classId ||
                   tx<std::max(0,cx-rx) || tx>std::min(level_->mapW-1,cx+rx) ||
                   ty<std::max(0,cy-ry) || ty>std::min(level_->mapH-1,cy+ry) ||
                   distance2(native,targetCenter(player_))>actor.brain.sensorRange*actor.brain.sensorRange || gap>cone)return false;
                selectedTarget=player_;goal=target;
            }else{
                const auto selected=selectSpiderPatrolWaypoint(state.patrol,path,position,actor.brain.reverse,actor.brain.closed,
                    !route.points.empty() && route.cursor>=route.points.size() && (route.status&1));
                if(!selected)return false;
                goal=*selected;
            }
            // 42dce0 snapshots the previous action before replacing its route.
            route.saved=static_cast<const SpiderRouteCore&>(route);
            route.target=selectedTarget;
            route.sampledTarget=goal;
            // 42d8f0 uses the target's native center;42e740 projects
            // the authored surface waypoint only for Patrol.
            const auto destination=selectedTarget?targetCenter(*selectedTarget):nativePoint(goal);
            route.search=beginNativeNavigation(native,destination,{actor.radius,actor.radius},seeker?actor.brain.sensorRange:0,createPoint);
            return true;
        };
        callbacks.searchStep=[&]{
            if(!route.search)return SpiderSearchResult::Failure;
            const auto result=advanceNativeNavigation(*route.search,query,createPoint,true);
            if(result==NativeNavProgress::Working)return SpiderSearchResult::Pending;
            if(result==NativeNavProgress::Failed)return SpiderSearchResult::Failure;
            auto selected=selectNativeNavigation(*route.search,query);
            if(!selected)return SpiderSearchResult::Rejected;
            route.points=std::move(selected->points);route.cursor=0;
            if(!nextSegment(route))return SpiderSearchResult::Rejected;
            if(seeker && (route.points.size()!=2 || heading(route.segmentStart,route.segmentEnd)%6!=0))return SpiderSearchResult::Rejected;
            return SpiderSearchResult::Accepted;
        };
        callbacks.dropRoute=[&]{
            // 42d760 rolls back failed/rejected replans, including countdowns.
            if(spiderNavigation_)releaseRoutePoints(*spiderNavigation_,route,route.saved?routeHandles(*route.saved):PointHandles{});
            route.search.reset();route.points.clear();route.cursor=0;
            if(route.saved) {
                static_cast<SpiderRouteCore&>(route)=std::move(*route.saved);
                route.saved.reset();
            }
        };
        callbacks.clearSavedSearch=[&]{
            PointHandles keep;
            for(auto p:route.points)if(p.graphNode!=SIZE_MAX)keep.insert(p.graphNode);
            if(spiderNavigation_) {
                if(route.saved)releaseRoutePoints(*spiderNavigation_,*route.saved,keep);
                // 42d520/42c400 discard alternatives; selected route owns only
                // the point nodes still linked by its simplified segments.
                releaseRoutePoints(*spiderNavigation_,route,keep);
            }
            route.saved.reset();route.search.reset();
        };
        callbacks.cancelPatrol=[&]{
            auto& patrol=state.patrolRoute;
            if(spiderNavigation_ && patrol.saved)
                releaseRoutePoints(*spiderNavigation_,*patrol.saved,routeHandles(patrol));
            patrol.saved.reset();patrol.action.active=patrol.action.searching=false;
        };
        callbacks.steer=[&]{
            const auto& attack=state.seekerRoute;
            const bool hasTarget=seeker && attack.target && *attack.target<actors_.size() && actors_[*attack.target].alive && terminal(attack);
            const float targetDistance=hasTarget?distance2(native,targetCenter(*attack.target)):0;
            //42ce40 admits a directly supplied Seeker (mode2), but its fallback
            //lookup during Patrol only admits node types2/1. Spider Seeker
            //is type7 (42c510), so Patrol must not reuse its near result.
            const auto nearStatus=hasTarget?spiderRouteStatus({0,targetDistance,0,actor.brain.minimumDistance,
                true,true,canTurn,true}):0;
            // 42cd30 reads action+28: Seeker Min-distance, Patrol4.
            const float arrivalRadius=seeker?actor.brain.minimumDistance:4.f;
            route.status=(nearStatus&4)?nearStatus:spiderRouteStatus({distance2(native,route.segmentEnd),targetDistance,arrivalRadius,actor.brain.minimumDistance,
                terminal(route),false,canTurn,true});
            if(!(route.status&4)){
                const bool more=!(route.status&1) || nextSegment(route);
                turn(route);
                if(more && spiderTurnAllowsForward(turning))command.throttle=EnemyThrottle::Forward;
                // 42caa0 returns without a slope visit on the ordinary Patrol
                // branch; the hide-until-firing class branch releases instead.
                if(actor.visibleWhenFiring && !actor.weapons.empty()) {
                    auto& weapon=actor.weapons[actor.selectedWeapon];
                    advanceWeaponAim(weapon.config,weapon.state,false);
                }
            }else{
                turn(route);
                if(route.status&2){command.throttle=EnemyThrottle::Reverse;actor.contactMotion=false;actor.enemyThrust=std::min(actor.enemyThrust,0.f);}
                if(route.target && *route.target<actors_.size() && actors_[*route.target].alive)route.segmentEnd=targetCenter(*route.target);
                const int aim=heading(route.segmentStart,route.segmentEnd);
                const int desired=aim<24?((aim+3)/6*6)%24:24;
                const bool aiming=route.target && *route.target<actors_.size() && actors_[*route.target].alive &&
                    !actor.weapons.empty() && terminal(route) && turning.logicalHeading==aim &&
                    static_cast<uint32_t>(targetDistance)<=static_cast<uint32_t>(actor.weapons[actor.selectedWeapon].range*actor.weapons[actor.selectedWeapon].range);
                firing=aiming && spiderTurnAllowsFire(turning,aim) && actor.direction==desired;
                // 42d2d0 returns admitted aim even during cooldown/pose gating;
                // 42caa0 then visits42c210/42c280 once, before motion/emission.
                if(!actor.weapons.empty()) {
                    auto& weapon=actor.weapons[actor.selectedWeapon];
                    advanceWeaponAim(weapon.config,weapon.state,aiming);
                }
            }
            command.routeCorrection=enemyRouteCorrection({native.x,native.y},{route.segmentStart.x,route.segmentStart.y},
                {route.segmentEnd.x,route.segmentEnd.y},true);
            if(!seeker)command.route=EnemyRouteMotion::Patrol;
            if(seeker && route.target==player_)locked=true;
        };
        const bool escaped=route.action.active && !inBounds;
        advanceSpiderAction(route.action,state.actorFlags,{ms,scanDelay,seeker,inBounds,unchanged},callbacks);
        // 42c5a0 clears Spider forward after the seeker pass. Patrol may then
        // replace it when the shared native action flags admit the base action.
        if(escaped)return; // both native actions return before default commands
        if(seeker)command.throttle=EnemyThrottle::Neutral;
        else if(command.throttle==EnemyThrottle::Neutral && propertyBool(object.properties,"Move-during-path-search"))
            command.throttle=EnemyThrottle::Forward;
    };
    if(actor.brain.seeker)run(state.seekerRoute,true);
    if(!(state.actorFlags&1) || (state.actorFlags&2)){
        if(actor.brain.base==BrainBase::Patrol)run(state.patrolRoute,false);
    }
    actor.brainState.initialized=state.patrol.initialized;actor.brainState.point=state.patrol.point;
    actor.brainState.reverse=state.patrol.reverse;actor.brainState.acquired=state.seekerRoute.target.has_value();
    actor.brainState.scanRemainingMs=state.seekerRoute.action.scanRemainingMs;
    actor.enemyMoveCommand=command.throttle==EnemyThrottle::Forward?200:command.throttle==EnemyThrottle::Reverse?208:0;
    // 4412fe tests actual position history, not the independently retained
    // native bounds center (return2 contacts can move that center alone).
    const auto actual=spiderSurfaceToNative(position.x,position.y);
    const auto previous=spiderSurfaceToNative(state.motion->previousSurface.x,state.motion->previousSurface.y);
    if(turning.bodyPeriodMs>0 && spiderIdleTurnAdmitted(turnConfig,turning,
        {actor.brain.seeker||actor.brain.tracker,actual.x,actual.y,previous.x,previous.y,actor.enemyMoveCommand}))
        advanceSpiderIdleTurn(turnConfig,turning,static_cast<int>(ms),
            [&](int low,int high){return nativeWorldRandom_.inclusive(low,high);});
    actor.direction=turning.bodyPose-1;actor.turnMs=static_cast<float>(turning.bodyRemainingMs);
    command.heading=turning.logicalHeading;
    actor.walkingCommand=actor.enemyMoveCommand==200&&actor.direction%6==0;
    advanceShieldRegeneration(id);
    EnemyMotionState motion{actor.contactVelocity,actor.enemyThrust,actor.enemyMagnitude,actor.contactMotion};
    const auto delta=advanceEnemyMotion(motion,command,seconds,actor.enemyMotionConfig);
    actor.contactVelocity=motion.velocity;actor.enemyThrust=motion.thrust;actor.enemyMagnitude=motion.magnitude;actor.contactMotion=motion.impactLatch;
    const float factor=14.f/(seconds*1000.f);
    const ContactVelocity nativeStep{motion.velocity.x/factor,motion.velocity.z/factor};
    // 4406e0 reconstructs stored contact velocity from438b20's native step.
    actor.contactVelocity={nativeStep.x*factor,nativeStep.z*factor};
    const auto rest=nativeActorRestDecision(id,nativeStep);
    if(rest.rested){actor.enemyMovedLastStep=false;if(auto* history=actor.nativeMotionHistory())history->previousSurface=scene_->objects[id].position;actor.contactVelocity={};if(rest.sleep)sleepNativeActor(id);}
    const auto moved=rest.rested?TerrainDisplacement{}:move(id,delta.x,delta.y,true,&nativeStep);
    if(moved.ammoAbort)return locked;
    if(!moved.constrained && !((delta.x!=0 || delta.y!=0) && lastSpiderSeparated_))actor.contactVelocity={};
    // Terrain response may clear command200 (4367d0 return0) after steering.
    actor.walkingCommand=actor.enemyMoveCommand==200&&actor.direction%6==0;
    actor.locomoting=nativeDistanceSquared(position,scene_->objects[id].position)>.0001f;
    actor.enemyRevealed=firing&&actor.visibleWhenFiring;
    // Preserve the existing target/reveal admission while gating only fire.
    const bool ready=firing && !actor.weapons.empty() && nativeEnemyWeaponReady(
        actor.weapons[actor.selectedWeapon].config,actor.weapons[actor.selectedWeapon].state,
        static_cast<uint64_t>(elapsedMs_),actor.nativeAmmoOwner.activeAmmo);
    // Rescan/search visits that did not steer must neither reset nor advance
    // the slope, and deferred emission must not visit it a second time.
    fire(id,ready,false,{}, {},WeaponFireVisit::AimAlreadyUpdated);
    return locked;
}
}
