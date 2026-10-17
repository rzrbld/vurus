#pragma once
#include "game/spider_contact_native.h"

namespace vyruz {
struct NativeActorContactResponse {
    ContactVelocity first{},second{};
    bool applied=false,writeFirst=false,writeSecond=false;
};

// 436dd0: independent center-to-contact projections, not a shared centerline.
// The source angle stays extended; the target angle is explicitly stored as a
// float before cosine. Preserve these stores and the stationary-target branch.
// Caller supplies 436ac0 effective velocities after movement rollback.
inline NativeActorContactResponse resolveNativeActorContact(ContactVelocity first,
        ContactVelocity second,ContactVelocity sourceCenter,ContactVelocity targetCenter,
        ContactVelocity point,float firstWeight,float secondWeight,float firstElasticity,
        float secondElasticity,bool firstMobile,bool secondMobile) {
    NativeActorContactResponse out{first,second};
    if(!finiteContactVector(first)||!finiteContactVector(second)||!finiteContactVector(sourceCenter)||
       !finiteContactVector(targetCenter)||!finiteContactVector(point)||
       !std::isfinite(firstWeight)||!std::isfinite(secondWeight)||
       !std::isfinite(firstElasticity)||!std::isfinite(secondElasticity)||
       firstWeight<=0||secondWeight<=0||firstElasticity<0||secondElasticity<0)return out;
    const ContactVelocity inward{point.x-sourceCenter.x,point.z-sourceCenter.z};
    if(!spiderContactAccepted(first,inward))return out;
    constexpr float pi=3.1415927410125732421875f;
    const float dx=sourceCenter.x-point.x;
    const long double dy=static_cast<long double>(sourceCenter.z)-point.z;
    // Original can produce NaNs at coincident centers/points or zero source
    // velocity. Retain finite state for these unsupported degenerate inputs.
    if((dx==0 && dy==0)||(first.x==0 && first.z==0))return out;
    long double angle=std::atan(dy/dx);if(dx<0)angle+=pi;
    long double velocityAngle=std::atan(static_cast<long double>(first.z)/first.x);
    if(first.x<0)velocityAngle+=pi;
    const float projection=static_cast<float>(std::cos(angle-velocityAngle)*
        std::sqrt(static_cast<long double>(first.x)*first.x+static_cast<long double>(first.z)*first.z));
    const ContactVelocity p1{static_cast<float>(std::cos(angle)*projection),
                             static_cast<float>(std::sin(angle)*projection)};
    out.applied=out.writeFirst=true;
    if(!secondMobile) {
        out.first={-firstElasticity*p1.x,-firstElasticity*p1.z};return out;
    }
    const float m1=firstWeight*.1f,m2=secondWeight*.1f;
    const float total=static_cast<float>(static_cast<long double>(secondWeight)*.1f+m1);
    if(!std::isfinite(total)||total<=0)return {first,second};
    ContactVelocity p2{};
    const bool stationary=second.x==0 && second.z==0;
    if(!stationary) {
        const long double tx=static_cast<long double>(targetCenter.x)-point.x;
        const long double ty=static_cast<long double>(targetCenter.z)-point.z;
        if(tx==0 && ty==0)return {first,second};
        float targetAngle=static_cast<float>(std::atan(ty/tx));
        if(tx<0)targetAngle=targetAngle+pi;
        long double targetVelocityAngle=std::atan(static_cast<long double>(second.z)/second.x);
        if(second.x<0)targetVelocityAngle+=pi;
        const long double magnitude=std::cos(static_cast<long double>(targetAngle)-targetVelocityAngle)*
            std::sqrt(static_cast<long double>(second.x)*second.x+static_cast<long double>(second.z)*second.z);
        p2={static_cast<float>(std::cos(static_cast<long double>(targetAngle))*magnitude),
            static_cast<float>(std::sin(static_cast<long double>(targetAngle))*magnitude)};
        if(!firstMobile) {
            out.first=first;out.writeFirst=false;out.writeSecond=true;
            out.second={-secondElasticity*p2.x,-secondElasticity*p2.z};return out;
        }
    }
    const auto component=[&](float a,float b,float& v1,float& v2) {
        const long double shared=(static_cast<long double>(m1)*a+static_cast<long double>(b)*m2)/total;
        const long double relative=(static_cast<long double>(a)-b)/total;
        v1=static_cast<float>(shared-relative*firstElasticity*m2);
        v2=static_cast<float>(shared+relative*secondElasticity*m1);
    };
    component(p1.x,p2.x,out.first.x,out.second.x);
    component(p1.z,p2.z,out.first.z,out.second.z);out.writeSecond=true;
    if(!finiteContactVector(out.first)||!finiteContactVector(out.second))return {first,second};
    return out;
}
}
