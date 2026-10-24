#include "audio/mixer.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace vyruz;
void check(bool value) { if (!value) { std::cerr << "Mixer check failed\n"; std::exit(1); } }
void near(float actual, float expected) { check(std::abs(actual-expected)<0.00001f); }
AudioClipPtr clip(std::vector<uint8_t> bytes, unsigned rate=48000, unsigned channels=1, unsigned bits=8) {
    std::string error; auto result=makeAudioClip(Wave{rate,uint16_t(channels),uint16_t(bits),std::move(bytes)},error);
    check(bool(result)); return result;
}
int main() {
    AudioMixer m; float out[16]{};
    auto c=clip({0,128,255}); m.play(c); m.mix(out,4);
    near(out[0],-1);near(out[1],-1);near(out[2],0);near(out[4],127/128.f);near(out[6],0);check(m.activeVoices()==0);
    auto stereo=clip({0,128,255,127},48000,2,16);m.play(stereo);m.mix(out,1);near(out[0],-1);near(out[1],32767/32768.f);
    auto half=clip({128,192},24000);m.play(half);m.mix(out,5);near(out[0],0);near(out[2],.25f);near(out[4],.5f);near(out[6],.5f);near(out[8],0);
    auto constant=clip({192});auto handle=m.play(constant,0,.5f,-1,true);
    m.mix(out,3);near(out[0],.25f);near(out[1],0);near(out[4],.25f);
    m.setPaused(true);m.mix(out,1);near(out[0],0);m.setPaused(false);m.mix(out,1);near(out[0],.25f);
    check(m.isPlaying(handle)); m.update(handle, .25f, 1);m.mix(out,1);near(out[0],0);near(out[1],.125f);
    m.stop(handle);check(!m.isPlaying(handle));m.mix(out,1);near(out[0],0);
    for(size_t i=0;i<AudioMixer::MaxVoices;++i) check(m.play(constant,10,1,0,true)!=0);
    check(m.play(constant,9)==0);check(m.play(constant,10)!=0);check(m.activeVoices()==32);
    m.mix(out,1);near(out[0],1);m.stopAll();
    m.playMusic(constant,.5f);m.play(constant,0,.5f);m.setMasterVolume(.5f);m.mix(out,1);near(out[0],.25f);
    m.play(constant);m.stopEffects();m.mix(out,1);near(out[0],.125f);m.stopMusic();m.mix(out,1);near(out[0],0);
    m.setMasterVolume(std::numeric_limits<float>::quiet_NaN());m.play(constant);m.mix(out,1);near(out[0],0);
    // Pause freezes playback cursor, not just output volume.
    m.setMasterVolume(1);m.play(c);m.mix(out,1);m.setPaused(true);m.mix(out,2);m.setPaused(false);m.mix(out,1);near(out[0],0);
    // UI cues advance while paused; gameplay and music retain their positions.
    // Keep this pause/clock fixture below overload; peak control is tested below.
    m.stopAll();m.play(c,0,.5f);m.playMusic(c, .5f, false);m.mix(out,1);near(out[0],-1);
    m.setPaused(true);m.play(constant,240,1,0,false,false);m.mix(out,1);near(out[0],.5f);
    m.mix(out,1);near(out[0],0);m.setPaused(false);m.mix(out,1);near(out[0],0);
    m.mix(out,1);near(out[0],127/128.f);m.mix(out,1);near(out[0],0);
    // Clock is measured in source time even when the source needs resampling.
    m.stopAll();auto clockHandle=m.play(half);check(m.playbackMilliseconds(clockHandle)==0);
    m.mix(out,1);near(float(*m.playbackMilliseconds(clockHandle)),1000.f/48000);
    m.setPaused(true);m.mix(out,4);near(float(*m.playbackMilliseconds(clockHandle)),1000.f/48000);
    m.setPaused(false);m.mix(out,4);check(!m.isPlaying(clockHandle));
    near(float(*m.playbackMilliseconds(clockHandle)),2000.f/24000);
    m.mix(out,4);near(float(*m.playbackMilliseconds(clockHandle)),2000.f/24000);
    auto replacement=m.play(c);check(!m.playbackMilliseconds(clockHandle));check(m.playbackMilliseconds(replacement)==0);
    m.stop(replacement);check(!m.playbackMilliseconds(replacement));check(!m.playbackMilliseconds(0));
    // Downsampling may step past the source end; the final clock is clamped.
    auto fast=clip({128,192,255},96000);clockHandle=m.play(fast);m.mix(out,4);
    near(float(*m.playbackMilliseconds(clockHandle)),3000.f/96000);
    m.stopEffects();check(!m.playbackMilliseconds(clockHandle));
    // Preserve only the exact admitted departure voice across a level reset.
    const auto departure=m.play(constant,191,1,0,true,false,AudioBus::Voices);
    const auto oldEffect=m.play(constant,127,1,0,true);
    m.stopEffects(departure);check(m.isPlaying(departure)&&!m.isPlaying(oldEffect));
    m.stop(departure);const auto newOwner=m.play(constant,191,1,0,true);
    m.stopEffects(departure);check(!m.isPlaying(newOwner)); // A reused channel is not that voice.
    m.stopAll();
    // Suspending background music leaves ordinary voices running, and resumes
    // music from the same sample. World pause still suspends both channels.
    m.playMusic(c,1,false);m.setMusicPaused(true);clockHandle=m.play(constant);
    m.mix(out,1);near(out[0],.5f);check(!m.isPlaying(clockHandle));
    m.setMusicPaused(false);m.mix(out,1);near(out[0],-1);
    m.setPaused(true);m.mix(out,1);near(out[0],0);
    m.setPaused(false);m.mix(out,1);near(out[0],0);m.mix(out,1);near(out[0],127/128.f);
    // Independent option buses affect already playing clips without freezing
    // muted source clocks (including the movie clock).
    m.stopAll(); m.setEffectsVolume(0); m.setVoicesVolume(.5f); m.setMusicVolume(.25f);
    auto effect=m.play(constant,0,1,0,true);
    auto voice=m.play(constant,0,1,0,true,true,AudioBus::Voices);
    m.playMusic(constant,1,true);m.mix(out,1);near(out[0],.375f);
    m.setVoicesVolume(0);m.mix(out,1);near(out[0],.125f);
    m.setMusicVolume(0);m.setEffectsVolume(.5f);m.mix(out,1);near(out[0],.25f);
    m.update(effect,1,0);m.mix(out,1);near(out[0],.25f); // spatial update retains bus gain
    m.stopAll();m.setEffectsVolume(0);m.setVoicesVolume(0);
    auto movie=m.play(half,0,1,0,false,false,AudioBus::Presentation);
    m.mix(out,1);m.mix(out,1);near(out[0],.25f);
    near(float(*m.playbackMilliseconds(movie)),2000.f/48000);
    m.setMasterVolume(0);m.mix(out,1);near(out[0],0);
    near(float(*m.playbackMilliseconds(movie)),3000.f/48000);
    // Output-rate changes retain source position and correct real-time duration.
    m.stopAll();m.setMasterVolume(1);m.setEffectsVolume(1);
    auto longClip=clip(std::vector<uint8_t>(48000,192));auto retained=m.play(longClip);
    m.mix(out,4);const double beforeRate=*m.playbackMilliseconds(retained);
    m.setSampleRate(22050);m.mix(out,4);
    near(float(*m.playbackMilliseconds(retained)-beforeRate),4000.f/22050);
    m.setSampleRate(11025);const double beforeLowRate=*m.playbackMilliseconds(retained);m.mix(out,4);
    near(float(*m.playbackMilliseconds(retained)-beforeLowRate),4000.f/11025);
    // CD succession crosses a track boundary within one callback without a gap.
    // A retired PCM stays alive until the main thread supplies/clears the queue.
    {
        AudioMixer cd;
        auto first=clip({160,160});auto second=clip({192,192,192,192});
        std::weak_ptr<const AudioClip> retired=first;
        cd.playMusic(first,1,false);first.reset();
        const auto serial=cd.musicState().serial;
        cd.queueMusic(second,1);cd.mix(out,4);
        near(out[0],.25f);near(out[2],.25f);near(out[4],.5f);near(out[6],.5f);
        check(cd.musicState().serial==serial+1);check(!cd.musicState().queued);
        check(!retired.expired());cd.queueMusic({});check(retired.expired());
        near(float(cd.musicState().milliseconds),2000.f/48000);
        // Darkness/XTC masks the CD signal while its source clock keeps moving.
        cd.playMusic(clip({128,144,160,176,192,208,224,240}),1,true);
        cd.playMusicOverlay(clip({160}),1);cd.mix(out,3);
        near(out[0],.25f);near(out[4],.25f);
        near(float(cd.musicState().milliseconds),3000.f/48000);
        cd.setMusicPaused(true);cd.mix(out,1);near(out[0],0);
        near(float(cd.musicState().milliseconds),3000.f/48000);
        cd.setMusicPaused(false);cd.stopMusicOverlay();cd.mix(out,1);near(out[0],.375f);
        cd.setMusicVolume(0);cd.mix(out,1);near(out[0],0);
        cd.setMusicVolume(1);cd.mix(out,1);near(out[0],.625f);
        cd.stopAll();cd.mix(out,1);near(out[0],0);check(!cd.musicState().playing);
    }
    // Overlap must preserve stereo balance instead of clipping the louder
    // channel alone. Recovery is smooth and independent of callback chunk size.
    {
        const auto asymmetric=clip({224,176},48000,2); // .75 left, .375 right
        AudioMixer full,chunked;
        for(auto* mixer:{&full,&chunked})for(int i=0;i<2;++i)mixer->play(asymmetric,0,1,0,true);
        float first[2];full.mix(first,1);near(first[0],1);near(first[1],.5f);
        chunked.mix(out,1);near(out[0],first[0]);near(out[1],first[1]);
        std::vector<float> continuous(4096),split(4096);
        full.mix(continuous.data(),2048);
        size_t offset=0;for(size_t count:{size_t(1),size_t(31),size_t(512),size_t(1504)}) {
            chunked.mix(split.data()+2*offset,count);offset+=count;
        }
        check(continuous==split);
        // Source positions are not delayed by the limiter.
        full.stopEffects();const auto handle=full.play(asymmetric,0,1,0,true);
        std::vector<float> recovery(48000*2);full.mix(recovery.data(),48000);
        check(recovery[0]>.5f&&recovery[0]<.501f);near(recovery[1],recovery[0]*.5f);
        for(size_t i=1;i<48000;++i) {
            check(recovery[2*i]>=recovery[2*(i-1)]&&recovery[2*i]<=.75f);
            near(recovery[2*i+1],recovery[2*i]*.5f);
        }
        check(recovery.back()>.3749f);check(full.isPlaying(handle));
        full.stopAll();full.play(asymmetric);full.mix(out,1);near(out[0],.75f);near(out[1],.375f);
        // Master gain participates before peak detection: a safe quiet mix
        // retains its exact samples and does not receive unnecessary attenuation.
        AudioMixer quiet;quiet.setMasterVolume(.2f);
        for(int i=0;i<4;++i)quiet.play(asymmetric);
        quiet.mix(out,1);near(out[0],.6f);near(out[1],.3f);
    }
    // Four coherent tones exceed full scale. After attack, their shape should
    // remain a sine rather than the flattened waveform produced by hard clips.
    {
        Wave wave{48000,1,16,{}};
        constexpr double pi=3.141592653589793;
        for(int i=0;i<480;++i) {
            const int16_t value=int16_t(std::lround(std::sin(i*2*pi/48)*.45*32767));
            wave.pcm.push_back(uint8_t(value));wave.pcm.push_back(uint8_t(uint16_t(value)>>8));
        }
        std::string error;auto tone=makeAudioClip(std::move(wave),error);check(bool(tone));
        AudioMixer overlap;for(int i=0;i<4;++i)overlap.play(tone,0,1,0,true);
        std::vector<float> samples(96000);overlap.mix(samples.data(),48000);
        double ss=0,sy=0,yy=0;
        for(int i=24000;i<48000;++i) {
            const double s=std::sin(i*2*pi/48),y=samples[2*i];
            check(std::isfinite(y)&&std::abs(y)<=1);near(samples[2*i+1],float(y));
            ss+=s*s;sy+=s*y;yy+=y*y;
        }
        check(yy>1000); // No mute/silence shortcut.
        check(std::sqrt(std::max(0.,yy-sy*sy/ss)/yy)<.01);
    }
    std::string error;check(!makeAudioClip(Wave{},error));check(!error.empty());
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    { AudioDevice device;check(device.open(error));check(device.available());check(device.play(constant)!=0);
      device.setPaused(true);device.setMusicPaused(true);auto deviceClock=device.play(constant);check(device.playbackMilliseconds(deviceClock)==0);device.stop(deviceClock);check(!device.playbackMilliseconds(deviceClock));device.playMusic(constant);device.stopAll();device.close();device.close();check(!device.available()); }
    // Exercise every authored output format using SDL's callback backend.
    for (int rate : {11025,22050}) for (int bits : {8,16}) for (int channels : {1,2}) {
        AudioDevice device;const AudioOutputSettings format{rate,bits,channels};
        check(device.configure(format,error));check(device.outputSettings()==format);
        device.setPaused(true);auto retained=device.play(longClip);check(device.playbackMilliseconds(retained)==0);
        check(!device.configure({12345,16,2},error));check(device.available());
        check(device.outputSettings()==format);check(device.playbackMilliseconds(retained)==0);
        device.setEffectsVolume(.5f);device.setVoicesVolume(.25f);device.setMusicVolume(.75f);
        const AudioOutputSettings changed{rate == 11025 ? 22050 : 11025,bits == 8 ? 16 : 8,channels == 1 ? 2 : 1};
        if (device.configure(changed,error)) {
            check(device.outputSettings()==changed);check(device.playbackMilliseconds(retained)==0);
        } else {
            // Some SDL backends only allow one output device at a time. The
            // transactional operation must retain the working device on failure.
            check(!error.empty());check(device.outputSettings()==format);check(device.playbackMilliseconds(retained)==0);
        }
        device.setPaused(false);SDL_Delay(55);check(*device.playbackMilliseconds(retained)>0);
    }
    SDL_setenv("SDL_AUDIODRIVER","vyruz-invalid-audio-driver",1);
    { AudioDevice device;check(!device.open(error));check(!error.empty());check(device.play(constant)==0); }
    std::cout << "Audio mixer passed\n";
}
