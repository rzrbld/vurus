#pragma once
#include "game/motion_contact.h"
#include "game/controls.h"
#include "game/native_navigation.h"
#include <optional>
#include <cstddef>

namespace vyruz {
// 41a5d0/41b4b0 and4406e0 use these PE float constants with x87
// intermediates. Do not replace with the player's algebraically folded map.
inline Displacement spiderSurfaceToNative(float x,float y) {
    constexpr float scale=35.777088165283203125f; //0050952c
    const long double sx=static_cast<long double>(x)*.015625L;
    const long double sy=static_cast<long double>(y)*.03125L;
    return {static_cast<float>((sx+sy)*scale),static_cast<float>((sy-sx)*scale)};
}
inline Displacement spiderNativeToSurface(float x,float z) {
    constexpr float xScale=.89442718029022216796875f; //00509544
    constexpr float yScale=.447213590145111083984375f; //00509540
    return {static_cast<float>((static_cast<long double>(x)-z)*xScale),
            static_cast<float>((static_cast<long double>(x)+z)*yScale)};
}
// 4105d0/410a00: preserve the two axis-linked boundary traversals. The
// returned references retain repeated visits because equal-distance selection
// uses the later admitted candidate. Native query proxy is offset half a radius
// from the visited boundary; it is not the proposed narrowphase position.
inline std::vector<uint64_t> spiderPhysicalContactOrder(const NativeNavGraph& graph,
        size_t source,NativeNavPoint proposed,float radius) {
    std::vector<uint64_t> order;
    if(source>=graph.objects.size())return order;
    const auto& old=graph.objects[source].bounds;
    const float oldX=(old.minX+old.maxX)*.5f,oldY=(old.minY+old.maxY)*.5f;
    const float dx=proposed.x-oldX,dy=proposed.y-oldY;
    const float minX=std::min(old.minX,proposed.x-radius),maxX=std::max(old.maxX,proposed.x+radius);
    const float minY=std::min(old.minY,proposed.y-radius),maxY=std::max(old.maxY,proposed.y+radius);
    for(int axis=0;axis<2;++axis) {
        const float movement=axis?dy:dx;
        if(movement==0)continue;
        const bool forward=movement>0;
        const auto& edges=axis?graph.yEdges:graph.xEdges;
        const auto coordinate=[&](const NativeNavGraphEdge& edge){const auto& b=graph.objects[edge.object].bounds;
            return axis?(edge.maximum?b.maxY:b.minY):(edge.maximum?b.maxX:b.minX);};
        const auto start=edges.endpoint(source,!forward);
        if(start==NativeNavGraphAxis::none)continue;
        const float goal=axis?proposed.y:proposed.x;
        const float cutoff=goal+(forward?2*radius:-2*radius);
        size_t previous=source;
        for(size_t n=forward?edges.next(start):edges.previous(start);n!=NativeNavGraphAxis::none;n=forward?edges.next(n):edges.previous(n)) {
            const auto& edge=edges.edge(n);const float c=coordinate(edge);
            if(forward?c>cutoff:c<cutoff)break;
            if(edge.object==previous || edge.object==source)continue;
            const auto& object=graph.objects[edge.object];if(!object.alive || object.point)continue;
            const auto& b=object.bounds;
            if(b.minX>maxX-.5f || b.maxX<minX-.5f || b.minY>maxY-.5f || b.maxY<minY-.5f)continue;
            float x,y;
            if(axis){y=c+(forward?-.5f*radius:.5f*radius);x=proposed.x+(y-proposed.y)*dx/dy;}
            else{x=c+(forward?-.5f*radius:.5f*radius);y=proposed.y+(x-proposed.x)*dy/dx;}
            if(b.minX>x+radius || b.maxX<x-radius || b.minY>y+radius || b.maxY<y-radius)continue;
            order.push_back(object.reference);previous=edge.object;
        }
    }
    return order;
}
// 4075c0, including the original float constants and zero-vector sentinel.
inline int spiderContactHeading(ContactVelocity v) {
    if (!finiteContactVector(v) || (v.x==0 && v.z==0)) return 24;
    const long double angle=std::atan(static_cast<long double>(v.z)/v.x)
        - (v.x<0?4.71238899230957L:1.5707963705062866L);
    if(angle>0)return 24;
    int value=static_cast<int>((angle-.13089969754219055L)*3.819718599319458L);
    if(value==-24)value=0;
    value=-value;
    return value>=0 && value<24?value:24;
}
// 436a50/404330: the sentinel also admits the contact. There is no dot guard.
inline bool spiderContactAccepted(ContactVelocity velocity,ContactVelocity inward) {
    const int a=spiderContactHeading(velocity),b=spiderContactHeading(inward);
    if(a==24 || b==24)return true;
    const int d=std::abs(a-b);
    return std::min(d,24-d)<7;
}
// 4069d0 case5/case5: proposed endpoint circles, not swept time-of-impact.
inline std::optional<ContactVelocity> spiderCircleContact(ContactVelocity proposed,
        float radius,ContactVelocity target,float targetRadius) {
    const float x=target.x-proposed.x,z=target.z-proposed.z;
    const float d=std::sqrt(x*x+z*z)-2.f;
    if(radius+targetRadius<d)return std::nullopt;
    // Original divides by zero for this degenerate placement; keep finite state.
    if(d==0)return proposed;
    const float f=(d-targetRadius)/d;
    return ContactVelocity{proposed.x+f*x,proposed.z+f*z};
}
// 406fd0: endpoint-circle against an axis-aligned native edge. `out` is
// retained across square edges, matching 4069d0's local contact storage.
inline bool spiderCircleEdgeEndpoint(ContactVelocity p,float r,ContactVelocity a,
        ContactVelocity b,ContactVelocity& out) {
    const auto inside=[&](ContactVelocity q){const float x=q.x-p.x,z=q.z-p.z;return x*x+z*z<=r*r;};
    const bool endpoint=inside(a)||inside(b);
    if(a.x==b.x) {
        if(!endpoint) {
            if(a.x<p.x-r || p.x+r<a.x || p.z<std::min(a.z,b.z) || std::max(a.z,b.z)<p.z)return false;
            out={a.x,p.z};return true;
        }
        out.x=a.x;
        if(a.z<=b.z)out.z=std::clamp(p.z,a.z,b.z);
        else { // descending native branch compares the X components
            if(a.x<=p.x && p.x<=b.x)out.x=p.x;
            else out.x=std::abs(p.x-b.x)<=std::abs(p.x-a.x)?b.x:a.x;
        }
        return true;
    }
    if(!endpoint) {
        if(a.z<p.z-r || p.z+r<a.z || p.x<std::min(a.x,b.x) || std::max(a.x,b.x)<p.x)return false;
        out={p.x,a.z};return true;
    }
    out.z=a.z;
    if(a.x<=b.x)out.x=std::clamp(p.x,a.x,b.x);
    else {
        if(a.z<=p.z && p.z<=b.z)out.z=p.z;
        else out.z=std::abs(p.z-a.z)<std::abs(p.z-b.z)?a.z:b.z;
    }
    return true;
}
// 407360: a circle crossing the complete edge between old and new bounds.
inline bool spiderCircleEdgeCrossing(ContactVelocity old,ContactVelocity p,float r,
        ContactVelocity a,ContactVelocity b,ContactVelocity& out) {
    if(a.x==b.x) {
        if(!((old.x+r<=a.x && a.x<=p.x-r)||(p.x+r<=a.x && a.x<=old.x-r)))return false;
        const float edge=p.x-old.x<0?a.x+r:a.x-r;
        out={a.x,(edge-old.x)*(p.z-old.z)/(p.x-old.x)+old.z};return true;
    }
    if(!((old.z+r<=a.z && a.z<=p.z-r)||(p.z+r<=a.z && a.z<=old.z-r)))return false;
    const float edge=p.z-old.z<0?a.z+r:a.z-r;
    out={(edge-old.z)*(p.x-old.x)/(p.z-old.z)+old.x,a.z};return true;
}
inline std::optional<ContactVelocity> spiderRectangleContact(ContactVelocity old,ContactVelocity p,
        float r,ContactVelocity minimum,ContactVelocity maximum) {
    const ContactVelocity v[]={{minimum.x,minimum.z},{maximum.x,minimum.z},
        {maximum.x,maximum.z},{minimum.x,maximum.z}};
    std::optional<ContactVelocity> result;
    ContactVelocity next{};
    for(int i=0;i<4;++i) {
        if(!spiderCircleEdgeEndpoint(p,r,v[i],v[(i+1)%4],next) &&
           !spiderCircleEdgeCrossing(old,p,r,v[i],v[(i+1)%4],next))continue;
        const auto distance=[&](ContactVelocity q){const float x=q.x-p.x,z=q.z-p.z;return x*x+z*z;};
        if(!result || distance(next)<=distance(*result))result=next;
    }
    return result;
}
inline std::optional<ContactVelocity> spiderSquareContact(ContactVelocity old,ContactVelocity p,
        float r,ContactVelocity target,float size) {
    const float h=size*.7071067690849304f;
    return spiderRectangleContact(old,p,r,{target.x-h,target.z-h},{target.x+h,target.z+h});
}
// 436b60 adjusts static rectangle contacts using the rolled-back center (zero).
inline ContactVelocity spiderRectangleNormal(ContactVelocity minimum,ContactVelocity maximum,
        ContactVelocity point,float radius) {
    const bool outsideX=0<minimum.x || maximum.x<0;
    const bool outsideZ=0<minimum.z || maximum.z<0;
    if(outsideX && outsideZ)return {0<minimum.x?minimum.x:maximum.x,0<minimum.z?minimum.z:maximum.z};
    if(outsideX)return {0<minimum.x?radius:-radius,0};
    if(outsideZ)return {0,0<minimum.z?radius:-radius};
    return point;
}
// 436dd0 projects each velocity onto its own center/contact normal. Since the
// moving actor has already rolled back, these normals need not be collinear.
inline ActorContactVelocities resolveSpiderActorContact(ContactVelocity first,
        ContactVelocity second,ContactVelocity sourceCenter,ContactVelocity targetCenter,
        ContactVelocity point,float firstWeight,float secondWeight,float firstElasticity,
        float secondElasticity,bool firstMobile,bool secondMobile) {
    ActorContactVelocities result{first,second,false};
    const ContactVelocity inward{point.x-sourceCenter.x,point.z-sourceCenter.z};
    if(!spiderContactAccepted(first,inward))return result;
    const auto n1=contactUnit(inward);
    const auto n2=contactUnit({point.x-targetCenter.x,point.z-targetCenter.z});
    const float a=contactDot(first,n1),b=contactDot(second,n2);
    const ContactVelocity p1{n1.x*a,n1.z*a},p2{n2.x*b,n2.z*b};
    result.applied=true;
    if(!secondMobile) {result.first={-firstElasticity*p1.x,-firstElasticity*p1.z};return result;}
    if((second.x!=0 || second.z!=0) && !firstMobile) {
        result.second={-secondElasticity*p2.x,-secondElasticity*p2.z};return result;
    }
    const float m1=firstWeight*.1f,m2=secondWeight*.1f,total=m1+m2;
    if(!(total>0))return {first,second,false};
    const auto component=[&](float a,float b,float& out1,float& out2) {
        const float shared=(m1*a+m2*b)/total,relative=(a-b)/total;
        out1=shared-firstElasticity*m2*relative;
        out2=shared+secondElasticity*m1*relative;
    };
    component(p1.x,p2.x,result.first.x,result.second.x);
    component(p1.z,p2.z,result.first.z,result.second.z);
    return result;
}
struct SpiderTerrainResponse {
    ContactVelocity velocity;
    bool applied=false,clearCommand=false;
};
// 4367d0 after 436a50 admission and 436b60 impact-point adjustment. The
// stored float normal angle is intentional: dot-product reflection changes
// exact-zero handling at tangency and can manufacture the native 0.2 kick.
inline SpiderTerrainResponse resolveNativeTerrainContact(ContactVelocity effective,
        ContactVelocity inward,float thrust,float elasticity,bool commanded,
        bool axisOrRectangle=true) {
    SpiderTerrainResponse out{effective};
    if(!finiteContactVector(effective)||!finiteContactVector(inward)||
       !std::isfinite(thrust)||!std::isfinite(elasticity)||
       (inward.x==0 && inward.z==0))return out;
    constexpr float pi=3.1415927410125732421875f;
    float normalAngle=static_cast<float>(std::atan(static_cast<long double>(inward.z)/inward.x));
    if(inward.x<0)normalAngle=normalAngle+pi; // explicit native float store
    ContactVelocity source=effective;
    if(source.x==0 && source.z==0) {
        if(!commanded)return out;
        const float factor=static_cast<float>(-.100000001490116119384765625L/
            std::sqrt(static_cast<long double>(inward.x)*inward.x+static_cast<long double>(inward.z)*inward.z));
        source={static_cast<float>(static_cast<long double>(thrust)*factor*inward.x),
                static_cast<float>(static_cast<long double>(thrust)*factor*inward.z)};
        out.clearCommand=true;
    }
    // Original x87 unordered comparison takes the zero-projection branch for
    // the zero-thrust synthetic vector (confirmed by executing4367d0).
    if(source.x==0 && source.z==0) {
        const auto heading=nativeHeading(spiderContactHeading(inward));
        out.velocity={-2.f*heading.x,-2.f*heading.y};out.applied=true;return out;
    }
    long double velocityAngle=std::atan(static_cast<long double>(source.z)/source.x);
    if(source.x<0)velocityAngle+=static_cast<long double>(pi);
    long double projection=std::cos(static_cast<long double>(normalAngle)-velocityAngle)*
        std::sqrt(static_cast<long double>(source.x)*source.x+static_cast<long double>(source.z)*source.z);
    if(projection==0) {
        const auto heading=nativeHeading(spiderContactHeading(inward));
        out.velocity={-2.f*heading.x,-2.f*heading.y};out.applied=true;return out;
    }
    ContactVelocity component=effective;
    if(axisOrRectangle) {
        projection=std::abs(projection);
        component={static_cast<float>(std::cos(static_cast<long double>(normalAngle))*projection),
                   static_cast<float>(std::sin(static_cast<long double>(normalAngle))*projection)};
    }
    const float x=static_cast<float>((static_cast<long double>(source.x)-component.x)-component.x);
    const float z=static_cast<float>((static_cast<long double>(source.z)-component.z)-component.z);
    out.velocity={x*elasticity,z*elasticity};out.applied=true;return out;
}
// Compatibility entry point for retained Spider probes; the routine is shared
// by ordinary native actor families, not specific to Spider AI.
inline SpiderTerrainResponse resolveSpiderTerrainContact(ContactVelocity effective,
        ContactVelocity inward,float thrust,float elasticity,bool commanded,bool axisOrRectangle=true) {
    return resolveNativeTerrainContact(effective,inward,thrust,elasticity,commanded,axisOrRectangle);
}

}
