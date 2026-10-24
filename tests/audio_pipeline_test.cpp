#include "audio/mixer.h"
#include "audio/scene_audio.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstdlib>
#include <new>
static thread_local bool audioGuard=false;
static thread_local unsigned allocations=0,releases=0;
void* operator new(std::size_t n){if(audioGuard)++allocations;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{if(p&&audioGuard)++releases;std::free(p);}
void operator delete(void*p,std::size_t)noexcept{::operator delete(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void*p)noexcept{::operator delete(p);}
void operator delete[](void*p,std::size_t)noexcept{::operator delete(p);}
using namespace vyruz;
static void need(bool b,const char*s){if(!b)throw std::runtime_error(s);}
static AudioClipPtr tone(unsigned rate,float frequency,float amplitude=.1f){
 Wave w{rate,1,16,{}};w.pcm.resize(rate*2*2);
 for(unsigned i=0;i<rate*2;++i){int16_t v=int16_t(std::lround(std::sin(6.283185307179586*frequency*i/rate)*amplitude*32767));w.pcm[2*i]=uint16_t(v);w.pcm[2*i+1]=uint16_t(v)>>8;}
 std::string e;return makeAudioClip(std::move(w),e);
}
static AudioClipPtr constant(){std::string e;return makeAudioClip(Wave{48000,1,8,{160}},e);}
static double amplitude(const std::vector<float>&s,double hz,unsigned rate,size_t start,size_t count){
 double x=0,y=0;for(size_t i=start;i<start+count;++i){double p=6.283185307179586*hz*i/rate;x+=s[2*i]*std::sin(p);y+=s[2*i]*std::cos(p);}return 2*std::hypot(x,y)/count;
}
static std::vector<float> render(AudioMixer&m,size_t count){std::vector<float>o(count*2);m.mix(o.data(),count);return o;}
int main(){
 const auto origin=NativeAudioPosition{1000,500,0};
 auto center=sceneAudioParameters(origin,origin,320);need(center.volume==65535&&center.pan==0,"coincident scene source");
 auto left=sceneAudioParameters({800,500,0},origin,320),right=sceneAudioParameters({1200,500,0},origin,320);
 need(left.volume==right.volume&&left.pan==-right.pan&&left.pan<0,"screen stereo symmetry");
 auto translated=sceneAudioParameters({9200,3500,0},{9000,3500,0},320);need(translated.volume==right.volume&&translated.pan==right.pan,"translation-invariant scene sound");
 need(sceneAudioParameters({1000,600,0},origin,320).volume==right.volume,"isometric ground depth");
 need(sceneAudioParameters({1400,500,0},origin,320).volume>0,"nearby offscreen sound audible");
 need(sceneAudioParameters({3000,500,0},origin,320).volume==0,"far sounds culled");
 const AudioMixSettings balanced{true,true,.5f,.5f,.7f};
 {
  AudioMixer m;m.configureMix(balanced);auto c=constant();
  auto speech=m.playNative(c,16,191,65535,0,true,false,AudioBus::Voices);
  need(speech&&!m.playNative(c,16,255,65535,0,false,false,AudioBus::Ui)&&m.isPlaying(speech),"balanced dialogue cannot be stolen by UI/FX");
 }
 {
  AudioMixer m;m.configureMix(balanced);auto c=constant();
  need(m.playNative(c,0xffff,130)&&m.playNative(c,0xffff,130),"two simultaneous nearby effects remain distinct");
  need(!m.playNative(c,0xffff,130)&&m.statistics().repetitions==1,"coherent repeat burst culled");
  need(m.playNative(c,0xffff,191,65535,0,false,true,AudioBus::Cues)!=0,"gameplay cue exempt from repeat cull");
 }
 {
  AudioMixer m;m.configureMix(balanced);auto h=m.play(constant(),0,1,0,true);auto a=render(m,4800);
  need(std::abs(a.back()-.175f)<1e-5,"quiet effect mix level");need(a.front()>0&&a.front()<.01f,"short onset ramp");
  m.update(h,1,1);auto pan=render(m,4800);need(std::abs(pan[0]-a[a.size()-2])<.01f&&pan[pan.size()-2]<1e-5,"smooth pan movement");
  m.setEffectsVolume(0);auto muted=render(m,4800);need(muted.back()==0&&m.isPlaying(h),"modern mute reaches exact zero without freezing source");m.setEffectsVolume(1);render(m,4800);
  m.stop(h);need(!m.isPlaying(h),"stop immediately releases logical handle");auto tail=render(m,480);
  need(tail[1]>.15f&&tail.back()==0,"bounded click-free stop tail");
 }
 {
  AudioMixer m;m.configureMix(balanced);m.playMusic(tone(48000,200,.05f),1,true);
  auto quiet=render(m,24000);m.play(constant(),191,1,0,true,false,AudioBus::Voices);auto speaking=render(m,24000);
  double before=amplitude(quiet,200,48000,14400,9600),during=amplitude(speaking,200,48000,14400,9600);
  need(during/before>.7&&during/before<.85,"gentle dialogue-only music duck");
  m.stopEffects();auto recovered=render(m,96000);
  need(amplitude(recovered,200,48000,86400,9600)>before*.99,"duck releases without persistent attenuation");
 }
 {
  AudioMixer m;m.configureMix(balanced);auto c=tone(48000,233.3f,.2f);
  auto handle=m.play(c);m.playMusic(c);auto running=render(m,4800);
  const auto voiceClock=m.playbackMilliseconds(handle);const auto musicClock=m.musicState().milliseconds;
  m.setPaused(true);auto stopped=render(m,480);
  need(std::abs(stopped.front())>0&&stopped.back()==0,"pause has bounded output fade");
  need(m.playbackMilliseconds(handle)==voiceClock&&m.musicState().milliseconds==musicClock,"pause fade never advances source clocks");
  m.play(constant(),255,1,0,true,false,AudioBus::Ui);auto ui=render(m,480);need(std::abs(ui.back()-.25f)<1e-5,"UI audible while world paused");
  m.setPaused(false);render(m,480);need(*m.playbackMilliseconds(handle)>*voiceClock,"resume advances retained source");
 }
 {
  AudioMixer full,split;full.configureMix(balanced);split.configureMix(balanced);
  for(auto*m:{&full,&split}){for(unsigned i=0;i<16;++i)m->playNative(tone(22050,211+i*137,.16f),1u<<i,130,65535,int(i)*3000-22000,true);m->playMusic(tone(44100,93,.06f));}
  auto expected=render(full,4800);std::vector<float>actual(9600);size_t at=0;
  for(size_t n:{size_t(1),size_t(31),size_t(511),size_t(2048),size_t(2209)}){split.mix(actual.data()+at*2,n);at+=n;}
  need(expected==actual,"modern DSP independent of callback partition");
  for(float x:expected)need(std::isfinite(x)&&std::abs(x)<=.98001f,"busy bounded output");
  auto stats=full.statistics();need(stats.requests==16&&stats.active[0]==16&&stats.clipped==0&&stats.outputPeak>.1f,"admission and peak diagnostics");
  need(!full.playNative(constant(),0,1)&&full.statistics().rejected==1,"rejected sound diagnostics");
 }
 {
  auto source=tone(22050,8000);AudioMixer linear,quality;quality.configureMix({false,true});linear.play(source);quality.play(source);
  auto a=render(linear,48000),b=render(quality,48000);
  double original=amplitude(a,8000,48000,12000,24000),improved=amplitude(b,8000,48000,12000,24000);
  double imageA=amplitude(a,14050,48000,12000,24000),imageB=amplitude(b,14050,48000,12000,24000);
  std::cout<<"upsample tone "<<original<<" -> "<<improved<<" image "<<imageA<<" -> "<<imageB<<'\n';
  need(improved>original&&imageB<imageA*.85,"cubic improves presence and suppresses interpolation image");
  AudioMixer downA,downB;downA.setSampleRate(11025);downB.setSampleRate(11025);downB.configureMix({false,true});downA.play(source);downB.play(source);
  auto x=render(downA,11025),y=render(downB,11025);double aa=amplitude(x,3025,11025,1000,9000),bb=amplitude(y,3025,11025,1000,9000);
  std::cout<<"downsample alias "<<aa<<" -> "<<bb<<'\n';need(bb<aa*.02,"anti-alias downsampling");
 }
 {
  AudioMixer m;m.configureMix(balanced);auto effect=tone(22050,700);m.playNative(effect,2,191);effect.reset();
  auto first=constant(),next=constant();m.playMusic(first,1,false);m.queueMusic(next);first.reset();next.reset();
  float output[4096];allocations=releases=0;audioGuard=true;m.mix(output,2048);audioGuard=false;
  need(allocations==0&&releases==0,"no callback allocations or ownership destruction, including music handoff");
 }
 {
  AudioMixer m;m.configureMix({false,true});auto h=m.play(tone(22050,700));render(m,24000);
  auto before=*m.playbackMilliseconds(h);m.setSampleRate(11025);render(m,11025);
  need(std::abs(*m.playbackMilliseconds(h)-before-1000)<.01,"quality/rate changes retain source time");
  SDL_setenv("SDL_AUDIODRIVER","dummy",1);AudioDevice device;std::string error;need(device.open(error),"modern callback device");
  device.configureMix(balanced);device.setPaused(true);auto voice=device.play(tone(48000,700));
  need(device.playbackMilliseconds(voice)==std::optional<double>{0},"modern paused device cursor");
  device.setPaused(false);SDL_Delay(40);auto stats=device.statistics();
  need(stats.callbacks>0&&stats.callbackTicks>0&&stats.clipped==0,"live callback timing and clipping diagnostics");
 }
 std::cout<<"PASS balanced scene, headroom, fades, selective ducking, clocks, partitioning and resampling\n";
}
