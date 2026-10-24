#include "game/world.h"
#include "game/projectile_guidance.h"
#include "game/properties.h"
#include "game/spider_contact_native.h"
#include <algorithm>
#include <cmath>

namespace vyruz {
namespace {
MissileTargetSample sample(ContentPosition p) {
    return {p.x,p.y,static_cast<int>(p.x/64),static_cast<int>(p.y/32)};
}
NativeNavPoint nativePoint(ContentPosition p) {const auto n=surfaceToNative(p.x,p.y);return {n.x,n.y};}
ContentPosition surfacePoint(NativeNavPoint p) {const auto s=nativeToSurface(p.x,p.y);return {s.x,s.y};}
void releasePoints(NativeNavGraph& graph,const MissileNativeRoute& route) {
    const auto& owned=route.search?route.search->seen:route.ownedPoints;
    // 42c2c0 frees the entire seen array in reverse allocation/list order.
    for(auto it=owned.rbegin();it!=owned.rend();++it)removeNativeNavigationObject(graph,it->graphNode);
}
// 41e1e0/42da80 follow each cell's head/next chain. Visit occupied heads via
// actor slots rather than scanning every map cell; selectors retain their own
// distinct cross-cell priorities, so only within-cell span order matters here.
std::vector<GuidanceLaunchCandidate> candidates(const SceneContent& scene,
    const std::vector<ActorState>& actors,const NativeActorCells& cells,
    std::optional<ClassId> matching={}) {
    std::vector<GuidanceLaunchCandidate> result;
    for(size_t head=0;head<cells.cell.size();++head) {
        const size_t cell=cells.cell[head];
        if(cell==NativeActorCells::Missing || cells.heads[cell]!=head)continue;
        for(size_t id=head;id!=NativeActorCells::Missing;id=cells.next[id]) {
            const auto& a=actors[id];const auto& o=scene.objects[id];
            if(!a.alive || (matching && o.classId!=*matching))continue;
            // Inert FX do not participate in the original classed-actor grid.
            if(a.effect || a.pooledTrail)continue;
            const auto p=o.position;const auto subtype=objectSubtype(o);
            const bool ordinary=subtype!="fx" && subtype!="item" && subtype!="lift" && subtype!="gate";
            result.push_back({id,o.classId,p,static_cast<int>(cell/size_t(cells.height)),
                static_cast<int>(cell%size_t(cells.height)),
                true,a.hudEnemy,ordinary,true,!o.flags.invisible.value_or(false),
                static_cast<int>(a.reactivateMs)});
            if(const auto* history=a.nativeMotionHistory())
                result.back().nativeCenter=ContentPosition{history->center.x,history->center.y};
        }
    }
    return result;
}
}
void GameWorld::initializeProjectileGuidance(Projectile& shot,size_t shooter) {
    const auto& cls=scene_->classes[shot.classId.value];
    if(std::none_of(cls.behaviours.begin(),cls.behaviours.end(),[](const auto& b){return contentNameIs(b,"tracker");}))return;
    const auto p=scene_->objects[shooter].position;
    // 41e1e0 reads the shooter's heading ring directly. Re-quantizing an
    // approximate launch vector can change the acquisition cone and pose.
    const int heading=actors_[shooter].weaponHeading();
    GuidanceLaunchQuery query;query.position=p;query.cellX=static_cast<int>(p.x/64);query.cellY=static_cast<int>(p.y/32);
    query.heading=heading;query.mapWidth=level_->mapW;query.mapHeight=level_->mapH;
    query.playerShooter=shooter==player_;query.targetClass=actors_[shooter].brain.targetClass;
    const auto selected=selectGuidanceLaunchTarget(query,candidates(*scene_,actors_,actorCells_,query.playerShooter?std::nullopt:query.targetClass));
    if(!selected)return; // Native clears Tracker on a launch miss permanently.
    shot.guidanceClass=scene_->objects[*selected].classId;shot.guidanceTarget=selected;
    shot.guidance.pose48=heading*2+1;
    shot.guidanceNative.emplace();
    if(!spiderNavigation_)initializeSpiderNavigation(true);
}
void GameWorld::clearProjectileNavigation(Projectile& shot) {
    if(shot.guidanceNative && spiderNavigation_) {
        releasePoints(*spiderNavigation_,*shot.guidanceNative);
        if(shot.guidanceNative->saved)releasePoints(*spiderNavigation_,shot.guidanceNative->saved->route);
    }
    shot.guidanceNative.reset();shot.guidanceSearch.reset();shot.guidanceRoute.clear();shot.guidancePoint=0;
}
void GameWorld::updateProjectileGuidance(Projectile& shot,float milliseconds) {
    if(!shot.guidanceClass)return;
    const auto& properties=scene_->classes[shot.classId.value].properties;
    const auto* tracker=attribute(properties,"tracker");if(!tracker)return;
    auto& state=shot.guidance;
    // Old A* frontiers have no native graph identity. Replan them on the next
    // update, retaining launch class, target, pose, clocks, motion and fuse.
    // An old accepted route can finish until its normal invalidation/replan.
    if(!shot.guidanceNative && state.phase==MissileGuidancePhase::Searching) {
        shot.guidanceSearch.reset();shot.guidanceRoute.clear();shot.guidancePoint=0;
        state.phase=MissileGuidancePhase::Idle;
    }
    MissileGuidanceInput input;input.dtMs=static_cast<int>(milliseconds);
    input.scanPeriodMs=static_cast<int>(propertyNumber(tracker->children,"Scan-Delay",20));
    if(shot.guidanceTarget && *shot.guidanceTarget<actors_.size()) {
        const size_t id=*shot.guidanceTarget;
        input.targetValid=actors_[id].alive;
        input.targetInvisible=scene_->objects[id].flags.invisible.value_or(false);
        input.target=sample(scene_->objects[id].position);
    }
    input.routeAtEnd=shot.guidancePoint+1>=shot.guidanceRoute.size();
    if(state.phase==MissileGuidancePhase::Following && shot.guidancePoint<shot.guidanceRoute.size()) {
        const auto end=shot.guidanceRoute[shot.guidancePoint];
        const auto p=surfaceToNative(shot.position.x,shot.position.y);
        auto start=surfaceToNative(shot.guidanceStart.x,shot.guidanceStart.y);
        auto goal=surfaceToNative(end.x,end.y);
        if(shot.guidanceNative && shot.guidancePoint+1<shot.guidanceNative->points.size()) {
            const auto a=shot.guidanceNative->points[shot.guidancePoint],b=shot.guidanceNative->points[shot.guidancePoint+1];
            start={a.x,a.y};goal={b.x,b.y};
        }
        input.inCorridor=missileGuidanceInsideCorridor(p.x,p.y,
            start.x,start.y,goal.x,goal.y,2*shot.radius,shot.motion.magnitude);
    }
    MissileGuidanceHooks hooks;
    bool rollback=false;
    hooks.beginSearch=[&]()->std::optional<MissileTargetSample> {
        GuidanceTrackingQuery q;q.position=shot.position;q.cellX=static_cast<int>(q.position.x/64);q.cellY=static_cast<int>(q.position.y/32);
        q.heading=(state.pose48-1)/2;q.mapWidth=level_->mapW;q.mapHeight=level_->mapH;q.targetClass=shot.guidanceClass;
        q.sensorRange=static_cast<int>(propertyNumber(tracker->children,"Sensor-Range"));
        q.visionDegrees=static_cast<int>(propertyNumber(tracker->children,"Vision-Angle",360));
        auto target=selectGuidanceTrackingTarget(q,candidates(*scene_,actors_,actorCells_,q.targetClass));
        if(!target)return {};
        if(!spiderNavigation_)initializeSpiderNavigation(true);
        if(!shot.guidanceNative)shot.guidanceNative.emplace();
        auto& native=*shot.guidanceNative;
        MissileSavedRoute saved{static_cast<MissileNativeRoute&>(native),state,shot.guidanceTarget,
            shot.guidanceRoute,shot.guidancePoint,shot.guidanceStart};
        static_cast<MissileNativeRoute&>(native)={};native.saved=std::move(saved);
        shot.guidanceTarget=target;shot.guidanceStart=shot.position;shot.guidanceRoute.clear();shot.guidancePoint=0;shot.guidanceSearch.reset();
        const auto destination=scene_->objects[*target].position;
        auto goal=nativePoint(destination);
        if(const auto* history=actors_[*target].nativeMotionHistory())goal=history->center;
        const float radius=shot.radius<2?0:shot.radius;
        const NativeNavPointFactory create=[&](NativeNavPoint p){return insertNativeNavigationPoint(*spiderNavigation_,p);};
        native.search=beginNativeNavigation(nativePoint(shot.position),goal,{radius,radius},
            float(static_cast<int>(propertyNumber(tracker->children,"Sensor-Range"))),create);
        return sample(destination);
    };
    hooks.stepSearch=[&] {
        if(!shot.guidanceNative || !shot.guidanceNative->search || !spiderNavigation_)return MissilePathResult::Failed;
        auto& native=*shot.guidanceNative;
        const NativeNavQuery query=[&](NativeNavPoint a,NativeNavPoint b,NativeNavPoint extent,bool overlap){
            return projectileNavigationObstacle(shot,shot.guidanceTarget,a,b,extent,overlap);
        };
        const NativeNavPointFactory create=[&](NativeNavPoint p){return insertNativeNavigationPoint(*spiderNavigation_,p);};
        const auto progress=advanceNativeNavigation(*native.search,query,create,true);
        if(progress==NativeNavProgress::Working)return MissilePathResult::Pending;
        const auto selected=progress==NativeNavProgress::Failed?std::nullopt:selectNativeNavigation(*native.search,query);
        if(!selected || selected->points.size()<2 ||
           spiderContactHeading({selected->points[1].x-selected->points[0].x,selected->points[1].y-selected->points[0].y})==24) {
            rollback=true;return MissilePathResult::Failed;
        }
        native.points=selected->points;
        native.ownedPoints=std::move(native.search->seen);native.search.reset();
        if(native.saved)releasePoints(*spiderNavigation_,native.saved->route);
        native.saved.reset();
        shot.guidanceStart=surfacePoint(native.points.front());shot.guidanceRoute.clear();
        for(size_t i=1;i<native.points.size();++i)shot.guidanceRoute.push_back(surfacePoint(native.points[i]));
        shot.guidancePoint=0;
        return MissilePathResult::Ready;
    };
    const auto result=advanceMissileGuidance(state,input,hooks);
    if(rollback && shot.guidanceNative) {
        auto& native=*shot.guidanceNative;
        if(spiderNavigation_)releasePoints(*spiderNavigation_,native);
        if(native.saved) {
            auto saved=std::move(*native.saved);native.saved.reset();
            static_cast<MissileNativeRoute&>(native)=std::move(saved.route);
            state=saved.state;shot.guidanceTarget=saved.target;shot.guidanceRoute=std::move(saved.surfaceRoute);
            shot.guidancePoint=saved.point;shot.guidanceStart=saved.start;
        } else {static_cast<MissileNativeRoute&>(native)={};}
        return;
    }
    // Native invalidation clears action flags, retaining route/seen ownership
    // for the next search's saved-action snapshot and rollback.
    if(result.clearRoute && !shot.guidanceNative){shot.guidanceSearch.reset();shot.guidanceRoute.clear();shot.guidancePoint=0;}
    if(result.steer && shot.guidancePoint<shot.guidanceRoute.size()) {
        // 42cd30 intermediate-node radius is8 native units; final Missile
        // Min-distance is0. Desired heading is the fixed route edge vector.
        float distance=nativeDistanceSquared(shot.position,shot.guidanceRoute[shot.guidancePoint]);
        if(shot.guidanceNative && shot.guidancePoint+1<shot.guidanceNative->points.size()) {
            const auto p=nativePoint(shot.position),q=shot.guidanceNative->points[shot.guidancePoint+1];
            distance=(p.x-q.x)*(p.x-q.x)+(p.y-q.y)*(p.y-q.y);
        }
        if(shot.guidancePoint+1<shot.guidanceRoute.size() && distance<=64) {
            shot.guidanceStart=shot.guidanceRoute[shot.guidancePoint++];
        }
        const auto end=shot.guidanceRoute[shot.guidancePoint];
        int desired=nativeDirectionFromVector(end.x-shot.guidanceStart.x,end.y-shot.guidanceStart.y);
        if(shot.guidanceNative && shot.guidancePoint+1<shot.guidanceNative->points.size()) {
            const auto a=shot.guidanceNative->points[shot.guidancePoint],b=shot.guidanceNative->points[shot.guidancePoint+1];
            desired=spiderContactHeading({b.x-a.x,b.y-a.y});
        }
        if(desired>=24) {
            state.phase=MissileGuidancePhase::Idle;
            if(!shot.guidanceNative){shot.guidanceRoute.clear();shot.guidancePoint=0;}
            return; // 42c9d0 rejects a zero-length edge, with no arbitrary turn.
        }
        const bool turnInPlace=propertyBool(properties,"Can-turn-on-place");
        int timeout=static_cast<int>(propertyNumber(properties,"Turn-timeout",6));
        if(!turnInPlace && timeout!=0 && shot.motionConfig.speedMax>0) {
            timeout=static_cast<int>((1-shot.motion.magnitude/shot.motionConfig.speedMax)*timeout);
            state.turnRemainingMs=std::min(state.turnRemainingMs,timeout);
        }
        const int heading=missileGuidanceTurn(state,desired,input.dtMs,
            timeout,turnInPlace || shot.motion.magnitude>=1);
        const auto v=nativeHeading(heading);const float length=std::hypot(v.x,v.y);
        shot.directionX=v.x/length;shot.directionZ=v.y/length;
    }
    if(shot.visual) {
        actors_[*shot.visual].direction=(state.pose48-1)/2;
    }
}
}
