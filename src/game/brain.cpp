#include "game/brain.h"
#include "game/controls.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace vyruz {
namespace {
bool equal(const std::string& a, const std::string& b) {
    if (a.size()!=b.size()) return false;
    for(size_t i=0;i<a.size();++i) if(std::tolower(static_cast<unsigned char>(a[i]))!=std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
const TextNode* attribute(const ObjectPlacement& o, const char* name) {
    for(const auto& n:o.properties) if(equal(n.key,"TYPE")&&!n.arguments.empty()&&equal(n.arguments[0],name)) return &n;
    return nullptr;
}
float value(const TextNode* n,const char* key,float fallback) {
    const auto* p=n?contentProperty(n->children,key):nullptr;
    if(!p||p->values.empty()) return fallback;
    char* end=nullptr; float f=std::strtof(p->values[0].c_str(),&end);
    return end!=p->values[0].c_str()&&*end=='\0'&&std::isfinite(f)?f:fallback;
}
ContentPosition position(const TextNode* n,const char* key,ContentPosition fallback) {
    const auto* p=n?contentProperty(n->children,key):nullptr;
    if(!p||p->values.size()<2) return fallback;
    float x=std::strtof(p->values[0].c_str(),nullptr),y=std::strtof(p->values[1].c_str(),nullptr);
    return x<0||y<0?fallback:ContentPosition{x,y};
}
int directionDifference(int a,int b) { int d=std::abs(a-b)%24; return std::min(d,24-d); }
}
float nativeDistanceSquared(ContentPosition a,ContentPosition b) {
    float x=a.x-b.x,y=a.y-b.y; return .625f*x*x+2.5f*y*y;
}
BrainConfig makeBrainConfig(const ObjectPlacement& o,const SceneContent& scene) {
    BrainConfig c;c.home=c.center=o.position;
    for(const auto& b:o.behaviours) {
        if(equal(b,"Fixed"))c.base=BrainBase::Fixed;
        else if(equal(b,"Patrol"))c.base=BrainBase::Patrol;
        else if(equal(b,"Straight"))c.base=BrainBase::Straight;
        else if(equal(b,"Random"))c.base=BrainBase::Random;
        else if(equal(b,"Seeker"))c.seeker=true;
        else if(equal(b,"Tracker"))c.tracker=true;
        else if(equal(b,"Guardian"))c.guardian=true;
    }
    const auto* seek=attribute(o,c.tracker?"Tracker":"Seeker");
    c.sensorRange=value(seek,"Sensor-Range",0); c.minimumDistance=value(seek,"Min-distance",0);
    c.scanDelayMs=std::max(1.f,value(seek,"Scan-Delay",250));c.visionDegrees=value(seek,"Vision-Angle",360);
    const auto* target=seek?contentProperty(seek->children,"Target"):nullptr;
    if(target&&!target->values.empty()) for(const auto& cl:scene.classes) if(equal(cl.name,target->values[0])) {c.targetClass=cl.id;break;}
    const auto* patrol=attribute(o,"Patrol");c.pathId=static_cast<int>(value(patrol,"Path-ID",-1));
    c.reverse=value(patrol,"Reverse",0)!=0;c.closed=value(patrol,"Closed",0)!=0;
    const auto* guard=attribute(o,"Guardian");c.home=position(guard,"Location",o.position);c.center=position(guard,"Center",o.position);c.guardianRange=value(guard,"Range",0);
    // 00413400 resolves negative authored center/location to the spawn position.
    c.guardianCenterFollowsActor=false;
    const auto* random=attribute(o,"Random");c.randomRange=value(random,"Range",0);c.randomDelayMs=value(random,"Delay",0);
    return c;
}
BrainIntent advanceBrain(const BrainConfig& c,BrainState& s,const BrainInput& in) {
    BrainIntent out;
    // Random route selection and the native incremental grid search are separate
    // from this direct-route vertical slice; never silently claim their parity.
    out.unsupported=c.base==BrainBase::Unsupported||c.base==BrainBase::Random;
    if(!s.initialized) {
        s.initialized=true;s.reverse=c.reverse;
        float nearest=std::numeric_limits<float>::infinity();
        for(int i=static_cast<int>(in.path.size())-1;i>=0;--i) {
            float d=std::abs(in.position.x-in.path[i].x)+std::abs(in.position.y-in.path[i].y);
            if(d<nearest){nearest=d;s.point=i;}
        }
    }
    if(!in.targetAlive||!in.targetVisible||!c.targetClass||*c.targetClass!=in.targetClass) s.acquired=false;
    s.scanRemainingMs-=std::max(0.f,in.dtMs);
    if((c.seeker||c.tracker)&&s.scanRemainingMs<=0) {
        s.scanRemainingMs=c.scanDelayMs;
        auto center=c.guardian&&!c.guardianCenterFollowsActor?c.center:in.position;
        float range=c.guardian?c.guardianRange:c.sensorRange;
        int d=nativeDirectionFromVector(in.targetPosition.x-in.position.x,in.targetPosition.y-in.position.y);
        bool cone=c.guardian||d==24||directionDifference(d,in.facing)<=std::clamp(c.visionDegrees/30.f,0.f,12.f);
        s.acquired=in.targetAlive&&in.targetVisible&&c.targetClass&&*c.targetClass==in.targetClass&&range>=0&&nativeDistanceSquared(center,in.targetPosition)<=range*range&&cone;
    }
    if(s.acquired && in.seekerRouteAccepted) {
        out.aim=in.targetPosition;
        float distance=nativeDistanceSquared(in.position,in.targetPosition);
        int aim=nativeDirectionFromVector(in.targetPosition.x-in.position.x,in.targetPosition.y-in.position.y);
        const bool aligned=in.spiderFacing ? (in.facing%6==0 && aim==in.facing)
                                          : (aim==24||aim==in.facing);
        out.fire=in.directRoute&&in.weaponRange>0&&distance<=in.weaponRange*in.weaponRange&&aligned;
        if(!in.stationaryAttack&&(c.base!=BrainBase::Fixed||in.fixedSeekerMoves)&&in.directRoute&&distance>c.minimumDistance*c.minimumDistance)out.destination=in.targetPosition;
        if(!in.directRoute)out.unsupported=true;
        return out;
    }
    if(c.guardian&&nativeDistanceSquared(in.position,c.home)>1) {out.destination=c.home;return out;}
    if(c.base==BrainBase::Patrol) {
        if(in.path.empty()||s.point<0||s.point>=static_cast<int>(in.path.size())){out.unsupported=true;return out;}
        if(in.reachedDestination&&in.path.size()>1) {
            int next=s.point+(s.reverse?-1:1);
            if(next<0||next>=static_cast<int>(in.path.size())) {
                if(c.closed)next=s.reverse?static_cast<int>(in.path.size())-1:0;
                else {s.reverse=!s.reverse;next=s.point+(s.reverse?-1:1);}
            }
            s.point=next;
        }
        out.destination=in.path[s.point];out.aim=out.destination;out.patrolDestination=true;
    } else if(c.base==BrainBase::Straight)out.unsupported=true;
    return out;
}
std::optional<ContentPosition> chooseRandomDestination(ContentPosition p,float range,int width,int height,
    const std::function<int(int,int)>& random,const std::function<bool(int,int)>& eligible) {
    if(width<=0||height<=0||!random||!eligible||!std::isfinite(range)||range<0||
       !std::isfinite(p.x)||!std::isfinite(p.y))return {};
    int left=0,top=0,right=width-1,bottom=height-1;
    if(range>0) {
        const int tx=static_cast<int>(std::clamp(std::floor(p.x/64),0.f,static_cast<float>(width-1)));
        const int ty=static_cast<int>(std::clamp(std::floor(p.y/32),0.f,static_cast<float>(height-1)));
        const int rx=static_cast<int>(std::min(std::floor((range+32)/64),static_cast<float>(width)));
        const int ry=static_cast<int>(std::min(std::floor((range+16)/32),static_cast<float>(height)));
        left=std::max(0,tx-rx);right=std::min(width-1,tx+rx);
        top=std::max(0,ty-ry);bottom=std::min(height-1,ty+ry);
    }
    for(int attempt=0;attempt<10;++attempt) {
        const int x=random(left,right),y=random(top,bottom);
        if(x<left||x>right||y<top||y>bottom)return {};
        bool accepted=true;
        for(int xx=std::max(0,x-2);xx<=std::min(width-1,x+2)&&accepted;++xx)
            for(int yy=std::max(0,y-2);yy<=std::min(height-1,y+2);++yy)
                if(!eligible(xx,yy)){accepted=false;break;}
        if(accepted)return ContentPosition{static_cast<float>(x*64+32),static_cast<float>(y*32+16)};
    }
    return {};
}
} // namespace vyruz
