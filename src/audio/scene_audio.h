#pragma once
#include "audio/native_audio.h"
namespace vyruz {
struct SceneAudioParameters { unsigned volume;int pan; };
// Camera axes are fixed: surface X is screen right, surface Y is compressed
// ground depth. Vehicle heading must not rotate the stereo field.
inline SceneAudioParameters sceneAudioParameters(NativeAudioPosition source,
        NativeAudioPosition listener,unsigned halfWidth) {
    for(unsigned i=0;i<3;++i)if(!std::isfinite(source[i])||!std::isfinite(listener[i]))return {0,0};
    const float width=float(std::max(halfWidth,1u));
    const float x=(source[0]-listener[0])/width;
    const float y=2*(source[1]-listener[1])/width;
    const float z=(source[2]-listener[2])/width;
    const float distance=std::sqrt(x*x+y*y+z*z);
    const float t=std::clamp((distance-.25f)/2.75f,0.f,1.f);
    const float attenuation=(1-t)*(1-t)*(1+2*t);
    return {unsigned(std::lround(65535*attenuation)),int(std::lround(std::clamp(x,-.9f,.9f)*32767))};
}
}
