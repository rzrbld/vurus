#include "game/ordinary_enemy_ai_native.h"
#include "game/spider_contact_native.h"
#include "game/enemy_turn_native.h"
#include <bit>
#include <cmath>
#include <limits>
namespace vyruz {
namespace {
void invoke(const std::function<void()>& fn){if(fn)fn();}
int32_t add(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));}
int32_t subtract(int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)-uint32_t(b));}
}
NativeNavPoint resolveNativeEnemyGuardianPoint(NativeNavPoint authored,NativeNavPoint birth) {
    return authored.x<0 || authored.y<0?birth:authored;
}
NativeEnemyGuardianGoal nativeEnemyGuardianGoal(NativeNavPoint location) {
    const auto goal=spiderSurfaceToNative(location.x,location.y);
    return {{location.x,location.y},int32_t(uint32_t(int32_t(location.x))>>6),
        int32_t(uint32_t(int32_t(location.y))>>5),{goal.x,goal.y}};
}
NativeNavPoint nativeEnemyDetectionCenter(bool guardian,NativeNavPoint stored,NativeNavPoint current) {
    return guardian && stored.x>=0 && stored.y>=0?stored:current;
}
bool nativeEnemyTargetDetected(NativeNavPoint center,NativeNavPoint target,int32_t range,int facing,int cone) {
    const float dx=target.x-center.x;
    const long double dy=static_cast<long double>(target.y)-center.y;
    const float roundedY=static_cast<float>(dy);
    const int32_t squared=std::bit_cast<int32_t>(uint32_t(range)*uint32_t(range));
    if(static_cast<long double>(dx)*dx+dy*roundedY>squared)return false;
    // Actual4075c0 zero/zero reaches __ftol's unordered conversion: its low
    // return word is zero, unlike the accepted finite contact-helper sentinel.
    const int aim=dx==0 && roundedY==0?0:spiderContactHeading({dx,roundedY});
    if(aim==24 || facing==24)return true;
    const int gap=std::abs(aim-facing);
    return std::min(gap,24-gap)<=cone;
}
NativeEnemyWeaponAimResult nativeEnemyWeaponAim(const NativeEnemyWeaponAimInput& in) {
    NativeEnemyWeaponAimResult result{false,false,in.movementCommand};
    if(!in.targetPresent)return result;
    if(!in.guided || enemyTurnSubtypeExcluded(in.subtype)) {
        if(!in.terminal)return result;
        if((in.twoPieces?in.gunHeading:in.bodyHeading)!=in.desiredHeading) {
            if(!in.twoPieces && (in.status&4) && !in.canTurnOnPlace)result.movementCommand=200;
            return result;
        }
    }
    if(!in.weaponPresent)return result;
    const float x=in.target.x-in.source.x,y=in.target.y-in.source.y;
    const long double distance=static_cast<long double>(x)*x+static_cast<long double>(y)*y;
    if(distance<0 || distance>std::numeric_limits<uint32_t>::max())return result;
    if(uint32_t(distance)>uint32_t(in.range)*uint32_t(in.range))return result;
    result.admitted=true;
    if(in.ready && in.turnCommand==0) {
        const int aim=x==0 && y==0?0:spiderContactHeading({x,y});
        result.fire=in.subtype==0x1f?in.bodyPose-1==in.bodyHeading:
            aim==(in.twoPieces?in.gunHeading:in.bodyHeading);
    }
    return result;
}
bool nativeEnemyBaseActionAdmitted(uint32_t flags){return !(flags&1) || (flags&2);}
bool nativeEnemyTargetUnchanged(bool exists,bool nativeActor,bool invisible,bool terminal,
        ContentPosition current,ContentPosition sampled,int32_t cx,int32_t cy,int32_t sx,int32_t sy) {
    if(!exists)return false;
    return terminal?(!nativeActor || !invisible) && current.x==sampled.x && current.y==sampled.y:cx==sx && cy==sy;
}
bool reuseNativeEnemyRandomGoal(NativeEnemyRandomState& state,bool terminal,ContentPosition sample) {
    if(terminal)return false;
    if(sample.x!=0 && sample.y!=0) {
        state.reuseCount=add(state.reuseCount,1);
        if(state.reuseCount<2)return true;
    }
    state.reuseCount=0;return false;
}
std::optional<ContentPosition> selectNativeEnemyPatrolGoal(NativeEnemyPatrolState& state,
        std::span<const ContentPosition> path,ContentPosition position,bool reverse,bool closed,bool complete) {
    if(path.empty())return {};
    if(!state.initialized) {
        int best=std::numeric_limits<int>::max();state.point=-1;state.reverse=reverse;
        // 42e910 descends, retaining the last authored point on equal distance.
        for(int i=int(path.size())-1;i>=0;--i) {
            const int d=std::abs(int(position.x)-int(path[i].x))+std::abs(int(position.y)-int(path[i].y));
            if(d<best){best=d;state.point=i;}
        }
        if(state.point<0)return {};
        state.initialized=true;
    } else if(complete && path.size()>1) {
        int next=state.point+(state.reverse?-1:1);
        if(next<0 || next>=int(path.size())) {
            if(closed)next=state.reverse?int(path.size())-1:0;
            else {state.reverse=!state.reverse;next=state.point+(state.reverse?-1:1);}
        }
        state.point=next;
    } else if(state.goal)return state.goal;
    if(state.point<0 || state.point>=int(path.size()))return {};
    state.goal=path[state.point];state.goalCellX=int(state.goal->x)>>6;state.goalCellY=int(state.goal->y)>>5;
    return state.goal;
}
unsigned nativeEnemySteeringStatus(const NativeEnemySteeringInput& in) {
    const auto allowed=[&](float x,float y){
        if(in.canTurnOnPlace)return true;
        const auto transformed=spiderSurfaceToNative(x,y);
        const int direction=spiderContactHeading({transformed.x,transformed.y});
        if(direction==24 || in.orthogonalHeading==24)return true; //404330 sentinel.
        const int d=std::abs(direction-in.orthogonalHeading);
        return std::min(d,24-d)<=1;
    };
    if(in.target && in.targetTerminalOrGuided) {
        const float dx=in.target->x-in.position.x,dy=in.target->y-in.position.y;
        if(allowed(dx,dy)) {
            const long double distance=static_cast<long double>(dx)*dx+static_cast<long double>(dy)*dy;
            const long double minimum=uint32_t(in.minimumDistance)*uint32_t(in.minimumDistance);
            if(distance<=minimum*.25L)return 7;
            if(distance<=minimum)return 5;
        }
    }
    const float dx=in.endpoint.x-in.position.x,dy=in.endpoint.y-in.position.y;
    // 42cd30 spills the final squared endpoint distance, unlike42ce40.
    const long double dyExtended=static_cast<long double>(in.endpoint.y)-in.position.y;
    const float distance=static_cast<float>(static_cast<long double>(dx)*dx+dyExtended*dy);
    if(!in.terminal)return distance<=64?1:0;
    if(in.target && !allowed(dx,dy))return 0;
    const long double radius=uint32_t(in.arrivalRadius)*uint32_t(in.arrivalRadius);
    if(distance<=radius*.5625L)return 3;
    return distance<=radius?1:0;
}
void applyNativeEnemyDetectionDifficulty(int32_t& scan,int32_t& sensor,bool counted,float scanRatio,float sensorRatio) {
    if(!counted)return;
    if(scanRatio!=1.f)scan=static_cast<int32_t>(static_cast<long double>(scan)*scanRatio);
    if(sensorRatio!=1.f)sensor=static_cast<int32_t>(static_cast<long double>(uint32_t(sensor))*sensorRatio);
}
NativeEnemyTargetWindow nativeEnemyTargetWindow(int32_t range,int cx,int cy,int width,int height) {
    if(range<0||width<=0||height<=0)return {};
    const auto rx=uint32_t(static_cast<int64_t>((static_cast<long double>(float(range))+range)*.89442718029022216796875f))>>6;
    const auto ry=uint32_t(static_cast<int64_t>(static_cast<long double>(range)*2*.447213590145111083984375f))>>5;
    return {int(std::max<int64_t>(0,int64_t(cx)-rx)),int(std::min<int64_t>(width-1,int64_t(cx)+rx)),
        int(std::max<int64_t>(0,int64_t(cy)-ry)),int(std::min<int64_t>(height-1,int64_t(cy)+ry))};
}
NativeEnemyActionResult advanceNativeEnemyAction(NativeEnemyActionClock& state,
        NativeEnemyActionActor& actor,const NativeEnemyActionConfig& config,
        const NativeEnemyActionInput& input,const NativeEnemyActionHooks& hooks) {
    NativeEnemyActionResult result;
    const bool targeting=config.mode==NativeEnemyActionMode::Seeker || config.mode==NativeEnemyActionMode::Tracker;
    const auto finish=[&] {
        if(targeting && config.subtype==0x1f)actor.movementCommand=0;
        else if(actor.movementCommand==0 && (config.straightBase ||
            (config.mode==NativeEnemyActionMode::Patrol && config.moveDuringPathSearch)))actor.movementCommand=200;
        actor.lastAction=static_cast<int>(config.mode);
    };
    const auto incrementScan=[&]{state.scanRemainingMs=add(state.scanRemainingMs,config.scanPeriodMs);};
    const auto decrementScan=[&]{state.scanRemainingMs=subtract(state.scanRemainingMs,input.elapsedMs);};
    if(state.active) {
        if(!input.inCorridor) {
            actor.flags&=~uint32_t(1);state.searching=state.active=false;
            if(!targeting)state.scanRemainingMs=0; //42e030, retains route ownership.
            result.escaped=true;return result; //No default movement/lastAction write.
        }
        actor.flags|=1;
        if(state.scanRemainingMs>0) {
            decrementScan();invoke(hooks.steer);
            result.locked=targeting && input.targetIsPlayer;
        } else if(!targeting || !input.targetUnchanged)state.searching=state.active=false;
        else incrementScan();
        finish();return result;
    }
    actor.flags&=~uint32_t(1);
    if(!state.searching) {
        if(state.scanRemainingMs<=0) {
            incrementScan();
            if(hooks.beginSearch && hooks.beginSearch()) {
                state.pathRemainingMs=0;state.searching=true;
            } else if(targeting)actor.flags|=2;
            else actor.flags&=~uint32_t(2);
        } else decrementScan();
        finish();return result;
    }
    state.pathRemainingMs=subtract(state.pathRemainingMs,input.elapsedMs);
    if(state.pathRemainingMs>0){finish();return result;}
    state.pathRemainingMs=add(state.pathRemainingMs,17);
    const int wave=hooks.searchWave?hooks.searchWave():0;
    if(wave==0 || (wave==-2 && config.mode==NativeEnemyActionMode::Seeker)) {
        state.searching=false;incrementScan();invoke(hooks.rollback);
        if(!targeting)actor.flags&=~uint32_t(2);
    } else if(wave==-2)decrementScan();
    else if(wave==-1 || wave==1) {
        if(!hooks.selectAndAdvance || !hooks.selectAndAdvance()) {
            state.active=false;
            if(targeting)actor.flags|=2;else actor.flags&=~uint32_t(2);
            invoke(hooks.rollback);
        } else {
            state.active=true;
            if(targeting)actor.flags=(actor.flags&~uint32_t(2))|1;else actor.flags|=3;
            invoke(hooks.discardSaved);if(targeting)invoke(hooks.cancelBase);
        }
        // Unlike failure, rejection/rollback reaches these writes afterwards.
        state.searching=false;incrementScan();
    }
    finish();return result;
}
}
