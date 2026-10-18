#include "game/aim_assist.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
int main(){
 using namespace vyruz;
 const std::array enemies{AimCandidate{1,100,5},AimCandidate{2,0,100}};
 auto a=assistAim(0,1,0,true,SIZE_MAX,enemies);assert(a.x==1&&a.y==0&&a.target==SIZE_MAX);
 a=assistAim(1,1,0,true,SIZE_MAX,enemies);assert(a.target==1&&a.x==100&&a.y==5);
 const std::array wider{AimCandidate{3,100,20}};
 assert(assistAim(1,1,0,true,SIZE_MAX,wider).target==SIZE_MAX);
 a=assistAim(2,1,0,true,SIZE_MAX,wider);assert(a.target==3&&a.x==100&&a.y==20);
 a=assistAim(3,0,1,true,SIZE_MAX,enemies);assert(a.target==2&&a.y==100);
 a=assistAim(3,1,0,false,2,enemies);assert(a.target==2);
 a=assistAim(3,1,0,true,2,enemies);assert(a.target==1);
 assert(assistAim(3,1,0,false,2,{}).target==SIZE_MAX);
 a=assistAim(4,0,0,false,2,enemies);assert(a.target==2&&a.x==0&&a.y==100);
 assert(assistAim(4,1,0,true,SIZE_MAX,enemies).target==SIZE_MAX);
 assert(assistAim(4,1,0,true,99,enemies).target==SIZE_MAX);
 assert(assistAim(3,0,0,true,SIZE_MAX,enemies).target==SIZE_MAX);
}
