#include "game/projectile_guidance.h"
#include "game/controls.h"
#include "game/spider_contact_native.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>
namespace vyruz {
std::optional<size_t> selectGuidanceLaunchTarget(const GuidanceLaunchQuery& q,
                                               std::span<const GuidanceLaunchCandidate> candidates) {
    if(!q.shooterEligible || q.rings<=0 || q.mapWidth<=0 || q.mapHeight<=0 ||
       (!q.playerShooter && !q.targetClass) || !std::isfinite(q.position.x) ||
       !std::isfinite(q.position.y))return {};
    std::optional<size_t> selected;
    std::tuple<int64_t,int64_t,int64_t> best{};
    for(const auto& c:candidates) {
        if(!c.eligible || c.cellX<0 || c.cellY<0 || c.cellX>=q.mapWidth || c.cellY>=q.mapHeight ||
           !std::isfinite(c.position.x) || !std::isfinite(c.position.y))continue;
        if(q.playerShooter ? !c.countsAsEnemy : (!c.ordinaryActor || c.classId!=*q.targetClass))continue;
        const int64_t dx=int64_t(c.cellX)-q.cellX,dy=int64_t(c.cellY)-q.cellY;
        const int64_t ring=std::max(std::abs(dx),std::abs(dy));
        if(ring>=q.rings)continue;
        const auto key=std::tuple{ring,dy,dx};
        if(selected && key>=best)continue;
        const auto nativeDelta=spiderSurfaceToNative(c.position.x-q.position.x,c.position.y-q.position.y);
        const int direction=spiderContactHeading({nativeDelta.x,nativeDelta.y});
        // 404330 returns zero for equal or invalid(24) headings; preserve the
        // coincident-candidate case rather than adding a new distance filter.
        int delta=0;
        if(direction>=0 && direction<24 && q.heading>=0 && q.heading<24) {
            delta=std::abs(direction-q.heading);delta=std::min(delta,24-delta);
        }
        if(delta>2)continue;
        selected=c.id;best=key;
    }
    return selected;
}
std::optional<size_t> selectGuidanceTrackingTarget(const GuidanceTrackingQuery& q,
                                                 std::span<const GuidanceLaunchCandidate> candidates) {
    if(!q.targetClass || q.sensorRange<0 || q.mapWidth<=0 || q.mapHeight<=0 ||
       !std::isfinite(q.position.x) || !std::isfinite(q.position.y))return {};
    // Raw42db37..74 projects the full native range diameter before tile shifts.
    const int64_t rx=static_cast<int64_t>(float(q.sensorRange)*2.f*.8944272f)/64;
    const int64_t ry=static_cast<int64_t>(float(q.sensorRange)*2.f*.4472136f)/32;
    const int64_t x0=std::max<int64_t>(0,int64_t(q.cellX)-rx),x1=std::min<int64_t>(q.mapWidth-1,int64_t(q.cellX)+rx);
    const int64_t y0=std::max<int64_t>(0,int64_t(q.cellY)-ry),y1=std::min<int64_t>(q.mapHeight-1,int64_t(q.cellY)+ry);
    const int cone=(q.visionDegrees%361)/30; // parser40a010 case8a
    const float range2=float(int64_t(q.sensorRange)*q.sensorRange);
    const auto sourceFallback=spiderSurfaceToNative(q.position.x,q.position.y);
    const auto source=q.nativeCenter.value_or(ContentPosition{sourceFallback.x,sourceFallback.y});
    std::optional<size_t> selected;
    std::tuple<int,int> best{};
    for(const auto& c:candidates) {
        if(!c.eligible || !c.classedActor || !c.visible || c.activationDelayMs>=1 ||
           c.classId!=*q.targetClass || c.cellX<x0 || c.cellX>x1 || c.cellY<y0 || c.cellY>y1 ||
           !std::isfinite(c.position.x) || !std::isfinite(c.position.y))continue;
        const auto key=std::tuple{c.cellX,c.cellY};if(selected && key>=best)continue;
        // 42da80 subtracts stored native centers, spills both deltas to float,
        // then tests extended squared distance and calls 4075c0 directly.
        const auto fallback=spiderSurfaceToNative(c.position.x,c.position.y);
        const auto center=c.nativeCenter.value_or(ContentPosition{fallback.x,fallback.y});
        const float dx=center.x-source.x,dy=center.y-source.y;
        if(static_cast<long double>(dx)*dx+static_cast<long double>(dy)*dy>range2)continue;
        const int direction=spiderContactHeading({dx,dy});
        int delta=0;if(direction>=0&&direction<24&&q.heading>=0&&q.heading<24){delta=std::abs(direction-q.heading);delta=std::min(delta,24-delta);}
        if(delta>cone)continue;
        selected=c.id;best=key;
    }
    return selected;
}

}
