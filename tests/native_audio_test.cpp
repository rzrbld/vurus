#include "audio/native_audio.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>
using namespace vyruz;
int main() {
    std::array<NativeAudioChannel,16> c;
    for(size_t i=0;i<c.size();++i)c[i]={true,130,int32_t(100+i)};
    c[6].active=false;assert(selectNativeAudioChannel(c,0xffe0,130)==6);c[6].active=true;
    c[5].remainingMs=90;c[6].remainingMs=80;assert(selectNativeAudioChannel(c,0xffe0,130)==6);
    c[5].priority=129;c[6].priority=128;assert(selectNativeAudioChannel(c,0xffe0,130)==6);
    c[5].priority=c[6].priority=130;c[5].remainingMs=80;assert(selectNativeAudioChannel(c,0xffe0,130)==5);
    assert(!selectNativeAudioChannel(c,0xffe0,129));assert(!selectNativeAudioChannel(c,0,255));
    c[1].active=false;assert(selectNativeAudioChannel(c,2,0)==1);
    c[4].priority=191;assert(selectNativeAudioChannel(c,0x10,191)==4);
    assert(!selectNativeAudioChannel(c,0x10,190));
    assert(nativeAudioDecibels(0)==-10000&&nativeAudioDecibels(1)==-4235);
    assert(nativeAudioDecibels(63)==-1210&&nativeAudioDecibels(127)==-605);
    assert(nativeAudioDecibels(254)==-3&&nativeAudioDecibels(255)==0);
    for(unsigned i=0;i<256;++i) {
        const int expected=i==0?-10000:i==255?0:int(873*std::log(double(i+1)/256));
        assert(nativeAudioDecibels(i)==expected);
        assert(nativeAudioVolumeGain(uint16_t(i<<8))==nativeAudioVolumeGain(uint16_t((i<<8)|255)));
    }
    assert(nativeAudioVolumeGain(65535)==1);
    auto [left,right]=nativeAudioPanGains(32767);assert(left==nativeAudioVolumeGain(0)&&right==1);
    auto [left2,right2]=nativeAudioPanGains(-32767);assert(left2==1&&right2==left);
    assert(nativeAudioPanGains(0)==std::pair(1.f,1.f));
    NativeSpatialParameters p;
    const std::array<float,10> xs={0,399,400,400.01f,500,799,800,900,-400,-500};
    const std::array<int,10> volumes={65535,65535,65535,0,0,0,0,0,65535,0};
    for(size_t i=0;i<xs.size();++i)assert(nativeSpatialVolume(p,{xs[i],0,0},{0,0,0})==volumes[i]);
    p.normFar=800;
    assert(nativeSpatialVolume(p,{500,0,0},{0,0,0})==0);
    assert(nativeSpatialVolume(p,{500,0,0},{-100,0,0})==8191);
    assert(nativeSpatialVolume(p,{500,0,0},{-200,0,0})==16383);
    assert(nativeSpatialVolume(p,{500,0,0},{-300,0,0})==0);
    const std::array<float,9> dx={-400,-320,-160,-1,0,1,160,320,400};
    const std::array<int,9> pans={-32767,-32767,-16383,-102,0,102,16383,32767,32767};
    for(size_t i=0;i<dx.size();++i)assert(nativeSpatialPan(dx[i],320)==pans[i]);
    assert(nativeEngineVolume(65535,0,100)==21845);
    assert(nativeEngineVolume(65535,50,100)==43690);
    assert(nativeEngineVolume(65535,100,100)==65535);
    assert(nativeEngineVolume(65535,-100,100)==21845);
    assert(nativeEngineVolume(65535,0,0)==21845);
    assert(nativeAudioTruncate(std::numeric_limits<long double>::infinity())==0);
}
