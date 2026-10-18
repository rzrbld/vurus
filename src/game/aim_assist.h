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
    if(mode==0||(length==0&&mode!=4))return result;
    const AimCandidate* best=nullptr;
    float score=mode==1?.105f:mode==2?.245f:3.142f;
    for(const auto& enemy:enemies){
        if(std::hypot(enemy.x,enemy.y)<1)continue;
        if(mode==4){if(enemy.id==retained){best=&enemy;break;}continue;}
        if(mode==3&&!stickActive&&enemy.id==retained){best=&enemy;break;}
        const float angle=std::abs(std::atan2(x*enemy.y-y*enemy.x,x*enemy.x+y*enemy.y));
        if(angle<score){score=angle;best=&enemy;}
    }
    if(!best)return result;
    // Strength controls acquisition width, not an off-target final direction.
    // Partial angular blends can quantize back to the unassisted 24-way heading.
    result.x=best->x;
    result.y=best->y;
    result.target=best->id;
    return result;
}
}
