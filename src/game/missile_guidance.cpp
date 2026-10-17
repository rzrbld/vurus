#include "game/missile_guidance.h"
#include <algorithm>
namespace vyruz {
MissileGuidanceResult advanceMissileGuidance(MissileGuidanceState& s,
 const MissileGuidanceInput& in,const MissileGuidanceHooks& hooks) {
    MissileGuidanceResult out;
    const auto invalidate=[&]{s.phase=MissileGuidancePhase::Idle;out.clearRoute=true;};
    if(s.phase==MissileGuidancePhase::Following) {
        if(!in.inCorridor) {invalidate();return out;}
        if(s.scanRemainingMs>0) {
            s.scanRemainingMs-=in.dtMs;
            out.steer=true;
        } else {
            const bool changed=in.routeAtEnd
                ? in.targetInvisible || in.target.x!=s.sampledTarget.x || in.target.y!=s.sampledTarget.y
                : in.target.tileX!=s.sampledTarget.tileX || in.target.tileY!=s.sampledTarget.tileY;
            if(!in.targetValid || changed) invalidate();
            else s.scanRemainingMs+=in.scanPeriodMs;
        }
    } else if(s.phase==MissileGuidancePhase::Idle) {
        if(s.scanRemainingMs<=0) {
            s.scanRemainingMs+=in.scanPeriodMs;
            auto target=hooks.beginSearch ? hooks.beginSearch() : std::nullopt;
            if(target) {
                s.sampledTarget=*target;
                s.pathRemainingMs=0;
                s.phase=MissileGuidancePhase::Searching;
            }
        } else s.scanRemainingMs-=in.dtMs;
    } else {
        s.pathRemainingMs-=in.dtMs;
        if(s.pathRemainingMs<=0) {
            s.pathRemainingMs+=17;
            const auto result=hooks.stepSearch ? hooks.stepSearch() : MissilePathResult::Failed;
            if(result==MissilePathResult::Pending) s.scanRemainingMs-=in.dtMs;
            else {
                if(result==MissilePathResult::Ready) s.phase=MissileGuidancePhase::Following;
                else invalidate();
                s.scanRemainingMs+=in.scanPeriodMs;
            }
        }
    }
    return out;
}
int missileGuidanceTurn(MissileGuidanceState& s,int desiredHeading,int dtMs,
 int turnPeriodMs,bool mayTurn,bool fastTurn) {
    if(!mayTurn || desiredHeading<0 || desiredHeading>=24) return (s.pose48-1)/2;
    s.turnRemainingMs-=dtMs;
    if(s.turnRemainingMs<=0) {
        s.turnRemainingMs=std::max(0,s.turnRemainingMs+turnPeriodMs);
        const int desired=desiredHeading*2+1;
        if(s.pose48!=desired) {
            const bool positive=s.pose48<desired ? desired-s.pose48<24 : s.pose48-desired>=24;
            s.pose48+= (positive?1:-1)*(fastTurn?2:1);
            if(s.pose48<1)s.pose48+=48;
            if(s.pose48>48)s.pose48-=48;
        }
    }
    return (s.pose48-1)/2;
}
bool missileGuidanceInsideCorridor(float x,float y,float sx,float sy,float ex,float ey,float width,float speed) {
 const float pad=width+speed;
 return x>=std::min(sx,ex)-pad && x<=std::max(sx,ex)+pad &&
        y>=std::min(sy,ey)-pad && y<=std::max(sy,ey)+pad;
}
}
