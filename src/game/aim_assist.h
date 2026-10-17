#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
namespace vyruz {
// Optional controller adaptation, deliberately outside native enemy/weapon rules.
struct AimCandidate {size_t id;float x,y;};
struct AssistedAim {float x=0,y=0;size_t target=SIZE_MAX;};
inline AssistedAim assistAim(int mode,float x,float y,bool stickActive,
                            size_t retained,std::span<const AimCandidate> enemies) {
    AssistedAim result{x,y};
    const float length=std::hypot(x,y);
    if(mode==0||length==0)return result;
    const AimCandidate* best=nullptr;
    float score=mode==1?.105f:mode==2?.245f:3.142f;
    for(const auto& enemy:enemies){
        if(std::hypot(enemy.x,enemy.y)<1)continue;
        if(mode==3&&!stickActive&&enemy.id==retained){best=&enemy;break;}
        const float angle=std::abs(std::atan2(x*enemy.y-y*enemy.x,x*enemy.x+y*enemy.y));
        if(angle<score){score=angle;best=&enemy;}
    }
    if(!best)return result;
    const float distance=std::hypot(best->x,best->y);
    const float weight=mode==1?.45f:mode==2?.75f:1.f;
    result.x=(x/length*(1-weight)+best->x/distance*weight)*distance;
    result.y=(y/length*(1-weight)+best->y/distance*weight)*distance;
    result.target=best->id;
    return result;
}
}
