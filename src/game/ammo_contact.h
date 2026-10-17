#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
namespace vyruz {
struct NativeContactPoint { double x=0,z=0; };
inline NativeContactPoint contactNativePoint(float x,float y) {
    return {x*.5590169943749474+y*1.118033988749895,
            -x*.5590169943749474+y*1.118033988749895};
}
// 4069d0 circle/circle contact coordinate, separate from candidate selection.
inline NativeContactPoint nativeAmmoImpactPoint(NativeContactPoint center,NativeContactPoint other,double otherRadius) {
    const double dx=other.x-center.x,dz=other.z-center.z,distance=std::hypot(dx,dz)-2;
    const double scale=distance!=0?(distance-otherRadius)/distance:0;
    return {center.x+dx*scale,center.z+dz*scale};
}
// Circle subset of 4105d0 -> 410a00 -> 1a470 -> 4069d0. Broadphase
// samples bound crossings; narrowphase uses the actual proposed endpoint.
inline std::optional<NativeContactPoint> nativeAmmoCircleContact(
    NativeContactPoint from,NativeContactPoint to,double radius,
    NativeContactPoint other,double otherRadius) {
    if(from.x==to.x && from.z==to.z)return {};
    const double lo[2]={std::min(from.x,to.x)-radius,std::min(from.z,to.z)-radius};
    const double hi[2]={std::max(from.x,to.x)+radius,std::max(from.z,to.z)+radius};
    const double c[2]={other.x,other.z},a[2]={from.x,from.z},b[2]={to.x,to.z};
    for(int k=0;k<2;++k)if(c[k]-otherRadius>hi[k]-.5 || lo[k]-.5>c[k]+otherRadius)return {};
    bool candidate=false;
    for(int axis=0;axis<2;++axis){
        if(a[axis]==b[axis])continue;
        const bool positive=b[axis]>a[axis];
        const double start=a[axis]+(positive?-radius:radius);
        const double limit=b[axis]+(positive?2*radius:-2*radius);
        for(double bound:{c[axis]-otherRadius,c[axis]+otherRadius}){
            if(positive?(bound<start || bound>limit):(bound>start || bound<limit))continue;
            const double sampled=bound+(positive?-.5*radius:.5*radius);
            const double cross=b[1-axis]+(sampled-b[axis])*(b[1-axis]-a[1-axis])/(b[axis]-a[axis]);
            if(c[axis]-otherRadius<=sampled+radius && sampled-radius<=c[axis]+otherRadius &&
               c[1-axis]-otherRadius<=cross+radius && cross-radius<=c[1-axis]+otherRadius)candidate=true;
        }
    }
    if(!candidate)return {};
    const double dx=other.x-to.x,dz=other.z-to.z,distance=std::hypot(dx,dz)-2;
    if(distance>radius+otherRadius)return {};
    // Native's zero denominator has no useful impact coordinate. The boolean
    // contact still holds; use endpoint only to keep selection finite.
    return nativeAmmoImpactPoint(to,other,otherRadius);
}
}
