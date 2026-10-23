#pragma once
#include "game/ammo_filter_native.h"
#include "game/spider_contact_native.h"
namespace vyruz {
// 41b770 keeps the previous candidate only when its displacement from the
// proposed center is strictly shorter. Component dominance avoids squaring in
// the same cases as native; squared values otherwise retain x87 intermediates.
inline bool nativeAmmoPreviousContactCloser(ContactVelocity previous,ContactVelocity next) {
    const float px=std::abs(previous.x),pz=std::abs(previous.z);
    const float nx=std::abs(next.x),nz=std::abs(next.z);
    // x87 C0 is also set for unordered comparisons. Preserve that branch
    // behavior when a distance-two circle produced an indefinite coordinate.
    const bool xLess=!(px>=nx),zLess=!(pz>=nz);
    if(!xLess&&!zLess)return false;
    if(xLess&&zLess)return true;
    return static_cast<long double>(px)*px+static_cast<long double>(pz)*pz <
           static_cast<long double>(nx)*nx+static_cast<long double>(nz)*nz;
}
// 4069d0 case5/5 keeps displacement, square root and radius division on the
// x87 stack until the final output-point stores. Existing actor movement keeps
// its accepted helper; this Ammo contract preserves the traced intermediates.
inline std::optional<ContactVelocity> nativeAmmoCirclePoint(ContactVelocity proposed,
        float radius,ContactVelocity center,float targetRadius,bool originalSingular=false) {
    const long double x=static_cast<long double>(center.x)-proposed.x;
    const long double z=static_cast<long double>(center.z)-proposed.z;
    const long double distance=std::sqrt(x*x+z*z)-2.L;
    if(static_cast<long double>(radius)+targetRadius<distance)return {};
    // Original divides by zero here. Keep a finite simulation fallback, whose
    // mixed-shape selection differences are explicitly retained in evidence.
    if(distance==0&&!originalSingular)return proposed;
    const long double scale=(distance-targetRadius)/distance;
    return ContactVelocity{static_cast<float>(proposed.x+scale*x),static_cast<float>(proposed.z+scale*z)};
}
// 407360 uses already rounded bounding extrema, not a reconstructed radius
// subtraction from the center, and keeps the crossing interpolation extended.
inline bool nativeAmmoCircleEdgeCrossing(ContactVelocity old,ContactVelocity p,float radius,
        ContactVelocity a,ContactVelocity b,ContactVelocity& point) {
    const NativeNavObstacle before{old.x-radius,old.z-radius,old.x+radius,old.z+radius};
    const NativeNavObstacle after{p.x-radius,p.z-radius,p.x+radius,p.z+radius};
    const long double dx=static_cast<long double>(p.x)-old.x,dz=static_cast<long double>(p.z)-old.z;
    if(a.x==b.x){
        if(!((before.maxX<=a.x&&a.x<=after.minX)||(after.maxX<=a.x&&a.x<=before.minX)))return false;
        const long double inset=static_cast<long double>(after.maxX)-p.x;
        const long double edge=dx<0?a.x+inset:a.x-inset;
        point={a.x,static_cast<float>((edge-old.x)*dz/dx+old.z)};
    }else{
        if(!((before.maxY<=a.z&&a.z<=after.minY)||(after.maxY<=a.z&&a.z<=before.minY)))return false;
        const long double inset=static_cast<long double>(after.maxY)-p.z;
        const long double edge=dz<0?a.z+inset:a.z-inset;
        point={static_cast<float>((edge-old.z)*dx/dz+old.x),a.z};
    }return true;
}
inline std::optional<ContactVelocity> nativeAmmoRectanglePoint(ContactVelocity previous,
        ContactVelocity proposed,float radius,ContactVelocity minimum,ContactVelocity maximum) {
    const ContactVelocity vertices[]={{minimum.x,minimum.z},{maximum.x,minimum.z},
        {maximum.x,maximum.z},{minimum.x,maximum.z}};
    std::optional<ContactVelocity> result;ContactVelocity next{};
    for(unsigned i=0;i<4;++i){
        if(!spiderCircleEdgeEndpoint(proposed,radius,vertices[i],vertices[(i+1)%4],next)&&
           !nativeAmmoCircleEdgeCrossing(previous,proposed,radius,vertices[i],vertices[(i+1)%4],next))continue;
        if(result && nativeAmmoPreviousContactCloser({result->x-proposed.x,result->z-proposed.z},
                                                     {next.x-proposed.x,next.z-proposed.z}))continue;
        result=next;
    }return result;
}
// 4105d0/410a00 traversal for Ammo. The previous native center is an
// independent input: averaging rounded old bounds can invent movement while
// stationary, notably at small fractional coordinates. Slope/intercept and
// proxy-coordinate float stores follow the original x87 instruction order.
struct NativeAmmoContactTraversal {
    std::vector<uint64_t> order;
    std::optional<size_t> closestNonContactNode;
};
inline NativeAmmoContactTraversal nativeAmmoPhysicalContactTraversal(const NativeNavGraph& graph,
        size_t source,ContactVelocity previous,ContactVelocity proposed,float radius) {
    NativeAmmoContactTraversal result;if(source>=graph.objects.size())return result;
    const auto old=graph.objects[source].bounds;
    const NativeNavObstacle next{proposed.x-radius,proposed.z-radius,proposed.x+radius,proposed.z+radius};
    const float half[2]={static_cast<float>((static_cast<long double>(next.maxX)-next.minX)*.5L),
                         static_cast<float>((static_cast<long double>(next.maxY)-next.minY)*.5L)};
    const float low[2]={std::min(old.minX,next.minX),std::min(old.minY,next.minY)};
    const float high[2]={std::max(old.maxX,next.maxX),std::max(old.maxY,next.maxY)};
    const float from[2]={previous.x,previous.z},to[2]={proposed.x,proposed.z};
    // 410a00 mutates this working copy after each proxy admission. 410cb0
    // measures rejected proxies against that copy, not the original proposal.
    NativeNavObstacle working=next;ContactVelocity workingCenter=proposed;
    for(unsigned axis=0;axis<2;++axis){
        const float oldMin=axis?old.minY:old.minX,newMin=axis?next.minY:next.minX,newMax=axis?next.maxY:next.maxX;
        if(oldMin==newMin)continue;
        const bool forward=oldMin<newMin;
        const float cutoff=forward?static_cast<float>(static_cast<long double>(half[axis])+newMax):
                                   static_cast<float>(static_cast<long double>(newMin)-half[axis]);
        const long double slope=(static_cast<long double>(to[1-axis])-from[1-axis])/
                                (static_cast<long double>(to[axis])-from[axis]);
        const float storedSlope=static_cast<float>(slope);
        const float intercept=static_cast<float>(static_cast<long double>(to[1-axis])-slope*to[axis]);
        const auto& edges=axis?graph.yEdges:graph.xEdges;
        const auto coordinate=[&](const auto&e){const auto& b=graph.objects[e.object].bounds;
            return axis?(e.maximum?b.maxY:b.minY):(e.maximum?b.maxX:b.minX);};
        const auto start=edges.endpoint(source,!forward);
        if(start==NativeNavGraphAxis::none)continue;
        size_t preceding=source;
        result.closestNonContactNode.reset();
        std::optional<size_t> closest;float closestDistance=0;
        for(size_t n=forward?edges.next(start):edges.previous(start);n!=NativeNavGraphAxis::none;n=forward?edges.next(n):edges.previous(n)) {
            const auto& edge=edges.edge(n);const float c=coordinate(edge);if(forward?c>cutoff:c<cutoff)break;
            if(edge.object==preceding||edge.object==source)continue;
            const auto& object=graph.objects[edge.object];if(!object.alive||object.point)continue;const auto b=object.bounds;
            if(b.minX>static_cast<long double>(high[0])-.5L || b.maxX<static_cast<long double>(low[0])-.5L ||
               b.minY>static_cast<long double>(high[1])-.5L || b.maxY<static_cast<long double>(low[1])-.5L)continue;
            float sampled[2];const long double inset=static_cast<long double>(half[axis])-half[axis]*.5L;
            sampled[axis]=static_cast<float>(forward?c-inset:c+inset);
            sampled[1-axis]=static_cast<float>(static_cast<long double>(sampled[axis])*storedSlope+intercept);
            if(b.minX>static_cast<long double>(sampled[0])+half[0] || b.maxX<static_cast<long double>(sampled[0])-half[0] ||
               b.minY>static_cast<long double>(sampled[1])+half[1] || b.maxY<static_cast<long double>(sampled[1])-half[1]){
                const long double distance=std::abs(static_cast<long double>(b.minX)-working.minX)+
                    std::abs(static_cast<long double>(b.minY)-working.minY)+
                    std::abs(static_cast<long double>(b.maxX)-working.maxX)+
                    std::abs(static_cast<long double>(b.maxY)-working.maxY);
                if(!closest||distance<=closestDistance){closest=edge.object;closestDistance=static_cast<float>(distance);}
                continue;
            }
            const long double dx=static_cast<long double>(sampled[0])-workingCenter.x;
            const float dz=static_cast<float>(static_cast<long double>(sampled[1])-workingCenter.z);
            working.minX=static_cast<float>(dx+working.minX);working.maxX=static_cast<float>(dx+working.maxX);
            working.minY=static_cast<float>(static_cast<long double>(dz)+working.minY);
            working.maxY=static_cast<float>(static_cast<long double>(dz)+working.maxY);
            workingCenter={sampled[0],sampled[1]};
            result.order.push_back(object.reference);preceding=edge.object;
            // The successful helper return bypasses its closest-hint output.
            closest.reset();
        }
        if(closest&&*closest!=source)result.closestNonContactNode=closest;
    }return result;
}
inline std::vector<uint64_t> nativeAmmoPhysicalContactOrder(const NativeNavGraph& graph,
        size_t source,ContactVelocity previous,ContactVelocity proposed,float radius) {
    return nativeAmmoPhysicalContactTraversal(graph,source,previous,proposed,radius).order;
}
struct NativeAmmoSelectedContact {uint64_t reference=0;ContactVelocity point;};
struct NativeAmmoContactSelector {
    ContactVelocity proposed;
    std::optional<NativeAmmoSelectedContact> selected;
    bool ordinary=false;
    // Native4105d0 gives non-Item precedence independent of distance. Within
    // a priority tier the closest impact coordinate wins; exact ties replace
    // the earlier candidate. Preserve repeated graph visits and their order.
    void consider(uint64_t reference,uint32_t subtype,ContactVelocity point) {
        const bool item=subtype==0x12;
        if(item && ordinary)return;
        if(selected && (item || ordinary)) {
            const ContactVelocity old{selected->point.x-proposed.x,selected->point.z-proposed.z};
            const ContactVelocity next{point.x-proposed.x,point.z-proposed.z};
            if(nativeAmmoPreviousContactCloser(old,next))return;
        }
        selected=NativeAmmoSelectedContact{reference,point};
        if(!item)ordinary=true;
    }
};
struct NativeAmmoCircleTarget {NativeAmmoContactObject filter;ContactVelocity center;float radius=0;bool disabled=false;};
struct NativeAmmoShapeTarget {
    NativeAmmoContactObject filter;
    ContactVelocity center,minimum,maximum;float radius=0;bool disabled=false;
};
struct NativeAmmoCircleQueryResult {
    std::optional<NativeAmmoSelectedContact> contact;
    std::optional<size_t> closestNonContactNode;
    bool hasAmmoCandidate=false;
    bool unsupportedShape=false;
};
// Bounded circle-only4105d0/410a00/41a470/4069d0 query. Source old bounds and
// insertion history are explicit graph inputs. Resolver supplies target native
// centers/identity/shape, never a replacement surface-space swept contact time.
// A caller must not use this as a complete world query when unsupportedShape is
// set: terrain segments/rectangles and point interactions require their paths.
template<class Resolver>
NativeAmmoCircleQueryResult queryNativeAmmoCircles(const NativeNavGraph& graph,
        size_t sourceNode,NativeAmmoContactObject source,ContactVelocity previous,ContactVelocity proposed,
        float radius,Resolver&& resolve,bool originalSingular=false) {
    NativeAmmoCircleQueryResult result;
    if(source.shape!=5){result.unsupportedShape=true;return result;}
    NativeAmmoContactSelector selector;selector.proposed=proposed;
    const auto traversal=nativeAmmoPhysicalContactTraversal(graph,sourceNode,previous,proposed,radius);
    result.closestNonContactNode=traversal.closestNonContactNode;
    for(const auto reference:traversal.order) {
        const auto target=resolve(reference);
        if(!target || target->disabled || !nativeAmmoContactAdmitted(source,target->filter))continue;
        if(target->filter.shape!=5){result.unsupportedShape=true;continue;}
        if(const auto point=nativeAmmoCirclePoint(proposed,radius,target->center,target->radius,originalSingular)){
            if(target->filter.kind==3&&target->filter.subtype==0x10)result.hasAmmoCandidate=true;
            selector.consider(reference,target->filter.subtype,*point);
        }
    }
    result.contact=selector.selected;return result;
}
//406e7e..406f6d point-source/circle-target branch. Preserve the independent
//float spills of dz, center deltas, projection ratio and final X displacement.
inline std::optional<ContactVelocity> nativeAmmoPointCircle(ContactVelocity previous,
        ContactVelocity proposed,ContactVelocity center,float radius,bool originalSingular=false) {
    const long double dx=static_cast<long double>(proposed.x)-previous.x;
    const float dz=static_cast<float>(static_cast<long double>(proposed.z)-previous.z);
    const float cx=static_cast<float>(static_cast<long double>(center.x)-previous.x);
    const long double wideZ=static_cast<long double>(center.z)-previous.z;
    const float cz=static_cast<float>(wideZ);
    const long double r2=static_cast<long double>(radius)*radius;
    if(previous.x==proposed.x && previous.z==proposed.z &&
       static_cast<long double>(cx)*cx+static_cast<long double>(cz)*cz<=r2)return previous;
    const long double ratio=(wideZ*dz+static_cast<long double>(cx)*dx)/(static_cast<long double>(dz)*dz+dx*dx);
    const long double px=ratio*dx,pz=static_cast<long double>(static_cast<float>(ratio))*dz;
    const long double ex=px-cx,ez=pz-cz,d2=ex*ex+ez*ez;
    if(r2<d2)return {};
    const long double factor=1.L-std::sqrt((r2-d2)/(pz*pz+px*px));
    const float moveX=static_cast<float>(factor*px);
    ContactVelocity hit{static_cast<float>(static_cast<long double>(moveX)+previous.x),
                        static_cast<float>(factor*pz+previous.z)};
    if(!originalSingular && (!std::isfinite(hit.x)||!std::isfinite(hit.z)))return proposed;
    return hit;
}
//406c54..406e1d: point/axis edge crossing, in authored edge order. Point versus
//rectangle returns the FIRST crossing edge, unlike the circle's closest-edge path.
inline std::optional<ContactVelocity> nativeAmmoPointEdge(ContactVelocity previous,
        ContactVelocity proposed,ContactVelocity a,ContactVelocity b) {
    const auto between=[](float x,float a,float b){return (a<=x&&x<=b)||(b<=x&&x<=a);};
    const long double dx=static_cast<long double>(proposed.x)-previous.x;
    const float dz=static_cast<float>(static_cast<long double>(proposed.z)-previous.z);
    long double ratio;
    if(a.x==b.x) {
        if(!between(a.x,previous.x,proposed.x))return {};
        ratio=(static_cast<long double>(a.x)-previous.x)/dx;
    }else {
        if(!between(a.z,previous.z,proposed.z))return {};
        ratio=(static_cast<long double>(a.z)-previous.z)/dz;
    }
    const float zStep=static_cast<float>(static_cast<long double>(static_cast<float>(ratio))*dz);
    const ContactVelocity hit{static_cast<float>(ratio*dx+previous.x),
        static_cast<float>(static_cast<long double>(zStep)+previous.z)};
    // Native x87's C0/C3 tests admit unordered collinear intersections.
    // Keep that result for the caller's established nonfinite impact boundary.
    const auto nativeBetween=[](float x,float a,float b){return (!(x<a)&&!(x>b))||(!(x<b)&&!(x>a));};
    if(a.x==b.x?nativeBetween(hit.z,a.z,b.z):nativeBetween(hit.x,a.x,b.x))return hit;
    return {};
}
// Expanded circle-source query for native point/circle/axis-segment/rectangle
// targets. Segment endpoints retain their authored order in minimum/maximum;
// callers must not sort them because406fd0 has distinct descending branches.
template<class Resolver>
NativeAmmoCircleQueryResult queryNativeAmmoShapes(const NativeNavGraph& graph,
        size_t sourceNode,NativeAmmoContactObject source,ContactVelocity previous,ContactVelocity proposed,
        float radius,Resolver&& resolve,bool originalSingular=false,bool allowPointSource=false) {
    NativeAmmoCircleQueryResult result;
    if(source.shape!=5 && !(allowPointSource&&source.shape==2)){result.unsupportedShape=true;return result;}
    if(sourceNode>=graph.objects.size())return result;
    NativeAmmoContactSelector selector;selector.proposed=proposed;
    const auto traversal=nativeAmmoPhysicalContactTraversal(graph,sourceNode,previous,proposed,radius);
    result.closestNonContactNode=traversal.closestNonContactNode;
    for(const auto reference:traversal.order) {
        const auto target=resolve(reference);
        if(!target || target->disabled || !nativeAmmoContactAdmitted(source,target->filter))continue;
        std::optional<ContactVelocity> point;
        if(source.shape==2) {
            switch(target->filter.shape) {
            case 1:break;
            case 2:point=target->center;break;
            case 3:point=nativeAmmoPointEdge(previous,proposed,target->minimum,target->maximum);break;
            case 4:{const ContactVelocity corners[]={target->minimum,{target->maximum.x,target->minimum.z},target->maximum,{target->minimum.x,target->maximum.z}};
                for(unsigned n=0;n<4 && !point;++n)
                    point=nativeAmmoPointEdge(previous,proposed,corners[n],corners[(n+1)%4]);
                break;}
            case 5:point=nativeAmmoPointCircle(previous,proposed,target->center,target->radius,originalSingular);break;
            default:result.unsupportedShape=true;break;
            }
        }else
        switch(target->filter.shape) {
        case 1:break;
        case 2:point=target->center;break;
        case 3:{ContactVelocity hit{};
            if(spiderCircleEdgeEndpoint(proposed,radius,target->minimum,target->maximum,hit) ||
               nativeAmmoCircleEdgeCrossing(previous,proposed,radius,target->minimum,target->maximum,hit))point=hit;
            break;}
        case 4:point=nativeAmmoRectanglePoint(previous,proposed,radius,target->minimum,target->maximum);break;
        case 5:point=nativeAmmoCirclePoint(proposed,radius,target->center,target->radius,originalSingular);break;
        default:result.unsupportedShape=true;break;
        }
        if(point){
            if(target->filter.kind==3&&target->filter.subtype==0x10)result.hasAmmoCandidate=true;
            selector.consider(reference,target->filter.subtype,*point);
        }
    }
    result.contact=selector.selected;return result;
}

}
