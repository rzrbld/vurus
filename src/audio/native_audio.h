#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace vyruz {
struct NativeAudioChannel { bool active=false; int priority=0; int32_t remainingMs=0; };
// 45f7e0: masks select channels, without implicit reservations. Equal-priority
// replacement favors shortest remaining time; complete ties keep the first.
inline std::optional<size_t> selectNativeAudioChannel(std::span<const NativeAudioChannel> channels,
                                                      uint32_t mask,int priority) {
    std::optional<size_t> selected;
    for(size_t i=0;i<std::min<size_t>(channels.size(),32);++i)
        if((mask&(uint32_t(1)<<i))&&!channels[i].active)return i;
    for(size_t i=0;i<std::min<size_t>(channels.size(),32);++i) {
        const auto& c=channels[i];
        if(!(mask&(uint32_t(1)<<i))||c.priority>priority)continue;
        if(!selected||c.priority<channels[*selected].priority||
           (c.priority==channels[*selected].priority&&c.remainingMs<channels[*selected].remainingMs))selected=i;
    }
    return selected;
}
// 47ea57's table, verified against all256 entries executed from the PE.
inline int nativeAudioDecibels(unsigned index) {
    if(index==0)return -10000;
    if(index>=255)return 0;
    return int(873*std::log(double(index+1)/256));
}
inline float nativeAudioVolumeGain(uint16_t raw) {
    return float(std::pow(10.0,double(nativeAudioDecibels(raw>>8))/2000));
}
inline std::pair<float,float> nativeAudioPanGains(int raw) {
    raw=std::clamp(raw,-32767,32767);
    const unsigned magnitude=unsigned(raw<0?-raw:raw)>>7;
    const float attenuated=float(std::pow(10.0,double(nativeAudioDecibels(255-magnitude))/2000));
    return raw<0?std::pair{1.f,attenuated}:std::pair{attenuated,1.f};
}
// Native __ftol produces signed64-bit indefinite on invalid conversion; callers
// here read its low32 bits, zero. Do not cast nonfinite C++ values to integers.
inline int32_t nativeAudioTruncate(long double value) {
    if(!std::isfinite(value)||value>=9223372036854775808.0L||value< -9223372036854775808.0L)return 0;
    return int32_t(uint32_t(uint64_t(int64_t(value))));
}
inline int nativeEngineVolume(int fullVolume,float nativeThrust,float thrustMax) {
    const int minimum=fullVolume/3;
    const auto value=nativeAudioTruncate(static_cast<long double>(fullVolume-minimum)*nativeThrust/thrustMax+minimum);
    return std::max(minimum,value);
}
using NativeAudioPosition=std::array<float,3>;
struct NativeSpatialParameters {
    NativeAudioPosition near={400,400,400},far={800,800,800};
    int volume=65535;
    float normNear=0,normFar=0;
};
// 45e4e0: keep its observed double subtraction, float stores and zero-norm
// result. This is deliberately not a conventional distance attenuation curve.
inline int nativeSpatialVolume(const NativeSpatialParameters& p,
                               NativeAudioPosition source,NativeAudioPosition listener) {
    NativeAudioPosition delta;
    for(size_t i=0;i<3;++i) {
        delta[i]=float(std::abs(static_cast<long double>(listener[i])-source[i]));
        if(!(delta[i]<p.far[i]))return 0;
    }
    if(delta[0]<=p.near[0]&&delta[1]<=p.near[1]&&delta[2]<=p.near[2])return p.volume;
    const long double x=std::abs(static_cast<long double>(delta[0])-source[0]);
    const float y=float(std::abs(static_cast<long double>(delta[1])-source[1]));
    const long double z=std::abs(static_cast<long double>(delta[2])-source[2]);
    const long double length=std::sqrt(static_cast<long double>(y)*y+z*z+x*x);
    return nativeAudioTruncate((length-p.normNear)*p.volume/(static_cast<long double>(p.normFar)-p.normNear));
}
inline int nativeSpatialPan(float dx,unsigned halfWidth) {
    if(!halfWidth)return 0;
    if(!std::isfinite(dx))return 0; // malformed port input; not an authored coordinate
    if(dx>=halfWidth)return 32767;
    if(dx<=-static_cast<long double>(halfWidth))return -32767;
    return nativeAudioTruncate(static_cast<long double>(dx)*32767/float(halfWidth));
}
}
