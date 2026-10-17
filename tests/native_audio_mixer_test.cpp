#include "audio/mixer.h"
#include "audio/native_audio.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace vyruz;
static void need(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::exit(1);}}
static void near(float value,float expected,const char* why){need(std::abs(value-expected)<.000002f,why);}
static AudioClipPtr clip(unsigned milliseconds=1000,int16_t value=8192){
    Wave wave;wave.sampleRate=48000;wave.channels=1;wave.bitsPerSample=16;
    wave.pcm.resize(size_t(milliseconds)*48*2);
    for(size_t n=0;n<wave.pcm.size();n+=2){wave.pcm[n]=uint16_t(value)&255;wave.pcm[n+1]=uint16_t(value)>>8;}
    std::string error;auto result=makeAudioClip(std::move(wave),error);need(bool(result),"fixture clip");return result;
}
int main(){
    const auto sample=clip();
    {
        AudioMixer m;
        need(!m.playNative(sample,0,255),"zero native mask selects no channel");
        need(!m.playNative(sample,1u<<16,255),"native masks address sixteen channels only");
        need(!m.nativeChannelPlaying(0)&&!m.nativeChannelPlaying(17),"invalid native channel query returns false");
        const auto first=m.playNative(sample,0xffff,130),second=m.playNative(sample,0xffff,130);
        need(first&&second&&first!=second&&m.nativeChannelPlaying(1)&&m.nativeChannelPlaying(2)&&!m.nativeChannelPlaying(3),"first free channel follows ascending mask order");
        m.stopNativeChannels(1);need(!m.isPlaying(first)&&m.isPlaying(second),"mask stop affects only selected channel");
        const auto replacement=m.playNative(sample,3,1);
        need(replacement&&m.nativeChannelPlaying(1)&&m.isPlaying(second),"free channel wins before busy low priority candidates");
        m.stopNativeChannels(0xffff);need(m.activeVoices()==0,"native stop mask covers sixteen channels");
    }
    {
        AudioMixer m;std::array<uint64_t,16> handles;
        for(unsigned i=0;i<16;++i)handles[i]=m.playNative(sample,1u<<i,i==8?120:130);
        need(!m.playNative(sample,0xffff,119),"lower priority cannot steal occupied native channel");
        const auto replacement=m.playNative(sample,0xffff,130);
        need(replacement&&!m.isPlaying(handles[8]),"steal lowest numeric priority");
        for(unsigned i=0;i<16;++i)if(i!=8)need(m.isPlaying(handles[i]),"unselected native handles remain live");
        need(!m.playbackMilliseconds(handles[8]),"stolen serial cannot query replacement cursor");
        m.stop(handles[8]);need(m.isPlaying(replacement),"stale serial stop cannot stop replacement");
        m.updateNative(handles[8],0,32767);float output[2];m.mix(output,1);
        need(output[0]==output[1],"stale serial update cannot change replacement pan");
    }
    {
        AudioMixer m;
        const auto longVoice=m.playNative(clip(1000),1,130);
        const auto shortVoice=m.playNative(clip(200),2,130);
        const auto middleVoice=m.playNative(clip(500),4,130);
        const auto replacement=m.playNative(sample,7,130);
        need(replacement&&m.isPlaying(longVoice)&&!m.isPlaying(shortVoice)&&m.isPlaying(middleVoice),"equal priority steals shortest remaining duration");
        m.stopAll();const auto a=m.playNative(sample,1,130),b=m.playNative(sample,2,130);
        need(m.playNative(sample,3,130)&&!m.isPlaying(a)&&m.isPlaying(b),"complete remaining-time tie keeps earliest masked channel");
        m.stopAll();const auto loop=m.playNative(clip(100),1,130,65535,0,true);
        const auto other=m.playNative(clip(80),2,130);std::vector<float> advance(60*48*2);m.mix(advance.data(),advance.size()/2);
        need(m.playNative(sample,3,130)&&m.isPlaying(loop)&&!m.isPlaying(other),"replacement compares current remaining duration, including loop position");
    }
    {
        AudioMixer m;const auto handle=m.playNative(sample,1,130);float output[2];m.mix(output,1);
        near(output[0],.25f,"native centered mono left is unchanged below overload");near(output[1],.25f,"native centered mono right is unchanged below overload");
        m.updateNative(handle,32767,0);m.mix(output,1);
        near(output[0],.25f*nativeAudioVolumeGain(32767),"native volume uses DS decibel table");
        near(output[1],output[0],"native centered gain remains linked");
        m.updateNative(handle,65535,32767);m.mix(output,1);
        near(output[0],.25f*nativeAudioVolumeGain(0),"native positive pan attenuates left to DS floor");near(output[1],.25f,"native pan does not boost right");
        m.updateNative(handle,65535,-32767);m.mix(output,1);
        near(output[0],.25f,"negative pan keeps left full");near(output[1],.25f*nativeAudioVolumeGain(0),"negative pan attenuates right to DS floor");
        m.updateNative(handle,65535,0);m.setEffectsVolume(.5f);m.mix(output,1);
        near(output[0],.25f*nativeAudioVolumeGain(32767),"effect option scales native raw volume before lookup");
        m.setEffectsVolume(0);m.mix(output,1);near(output[0],0,"disabled effect bus is exactly silent despite DS floor");
    }
    {
        AudioMixer m;
        const auto effect=m.playNative(sample,1,130,65535,0,false,true,AudioBus::Effects);
        const auto speech=m.playNative(sample,0x10,191,65535,0,false,false,AudioBus::Voices);
        m.setPaused(true);std::vector<float> output(480*2);m.mix(output.data(),480);
        need(m.playbackMilliseconds(effect)==std::optional<double>{0},"native gameplay voice cursor freezes during pause");
        need(m.playbackMilliseconds(speech)==std::optional<double>{10},"native speech remains audible and advances during pause");
        near(output[0],.25f,"pause output contains speech only");
        m.setVoicesVolume(0);m.mix(output.data(),480);near(output[0],0,"disabled voice bus is exactly silent");
        need(m.playbackMilliseconds(speech)==std::optional<double>{20},"voice volume does not stop source clock");
        m.setPaused(false);m.mix(output.data(),480);need(m.playbackMilliseconds(effect)==std::optional<double>{10},"unpause resumes native effect cursor");
    }
    {
        AudioMixer m;m.setMusicNativeChannel(true);m.playMusic(sample,1,true);
        need(m.nativeChannelPlaying(1)&&!m.nativeChannelPlaying(0),"native WAV music occupies channel1 only");
        need(!m.playNative(sample,1,254),"lower priority effect cannot steal music255");
        need(m.playNative(sample,1,255)&&!m.musicState().playing,"equal255 request can replace WAV music");
        m.playMusic(sample,1,true);need(m.activeVoices()==0&&m.nativeChannelPlaying(1),"native WAV start clears previous effect channel1");
        m.stopNativeChannels(1);need(!m.musicState().playing&&!m.nativeChannelPlaying(1),"native mask stops WAV music");
        m.setMusicNativeChannel(false);m.playMusic(sample,1,true);
        need(!m.nativeChannelPlaying(1),"CD source does not occupy DS channel1");
        m.stopNativeChannels(1);need(m.musicState().playing,"DS channel stop does not stop CD");
        m.playMusicOverlay(sample,1);need(m.nativeChannelPlaying(1),"native overlay occupies channel1 while CD advances");
        need(!m.playNative(sample,1,254),"native overlay priority protects its channel");
        m.stopNativeChannels(1);need(m.musicState().playing&&!m.nativeChannelPlaying(1),"native overlay stop leaves underlying CD active");
    }
    {
        AudioMixer one,chunks;
        for(unsigned i=0;i<16;++i){need(one.playNative(sample,1u<<i,130)!=0,"limiter first fixture admission");need(chunks.playNative(sample,1u<<i,130)!=0,"limiter second fixture admission");}
        std::vector<float> expected(2000),actual(2000);one.mix(expected.data(),1000);chunks.mix(actual.data(),123);chunks.mix(actual.data()+246,877);
        need(expected==actual,"native overload output is independent of callback partition");
        near(expected[0],1,"native sixteen-channel overload has immediate linked peak control");
        for(size_t i=0;i<expected.size();i+=2)need(std::isfinite(expected[i])&&std::abs(expected[i])<=1&&expected[i]==expected[i+1],"native overlap is bounded with preserved stereo balance");
        one.stopAll();one.playNative(sample,1,130);float clean[2];one.mix(clean,1);near(clean[0],.25f,"stopAll resets output limiter without changing clean native gain");
    }
    std::cout<<"PASS native masks, allocator/serials, DS gain/pan, pause, music reservation and accepted output limiting\n";
}
