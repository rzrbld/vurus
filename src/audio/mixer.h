#pragma once
#include "assets/wav.h"
#include "audio/wav_stream.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <SDL2/SDL.h>

namespace vyruz {
// Effects keep PCM; disk music uses a bounded worker-fed buffer.
struct AudioClip { Wave wave; size_t frames = 0; std::shared_ptr<WavStream> stream; };
using AudioClipPtr = std::shared_ptr<const AudioClip>;
AudioClipPtr makeAudioClip(Wave wave, std::string& error);

// Separate native FX/voice controls without stopping playback or its source clock.
enum class AudioBus { Effects, Voices, Presentation, Cues, Ambience, Ui, Music };

struct AudioMixSettings {
    bool balanced=false, highQuality=false;
    float ambience=.5f, ducking=.5f, headroom=.7f;
};
struct AudioStatistics {
    std::array<unsigned,7> active{}; // buses above, then music
    uint64_t requests=0,rejected=0,stolen=0,underruns=0,overloads=0,clipped=0,repetitions=0;
    float inputPeak=0,outputPeak=0,minimumGain=1;
    uint64_t callbacks=0,callbackTicks=0,maxCallbackTicks=0;
};
struct AudioResampleKernel;

class AudioMixer {
public:
    static constexpr int SampleRate = 48000;
    static constexpr size_t MaxVoices = 32;
    uint64_t play(AudioClipPtr clip, int priority = 0, float gain = 1, float pan = 0, bool loop = false, bool pauseWithWorld = true, AudioBus bus = AudioBus::Effects);
    // Native caller masks select among the first16 channels. Generic presentation
    // playback retains the existing32-voice adapter API.
    uint64_t playNative(AudioClipPtr clip,uint32_t mask,int priority,unsigned volume=65535,int pan=0,bool loop=false,bool pauseWithWorld=true,AudioBus bus=AudioBus::Effects);
    void updateNative(uint64_t handle,unsigned volume,int pan=0);
    void updateNativeChannel(unsigned channel,unsigned volume,int pan=0);
    bool nativeChannelPlaying(unsigned channel) const;
    void stopNativeChannels(uint32_t mask);
    void setMusicNativeChannel(bool native); // WAV music occupies native channel1; CD does not.
    void playMusic(AudioClipPtr clip, float gain = 1, bool loop = true);
    void queueMusic(AudioClipPtr clip, float gain = 1);
    void playMusicOverlay(AudioClipPtr clip, float gain = 1);
    void stopMusicOverlay();
    struct MusicState { uint64_t serial; bool playing, queued; double milliseconds; };
    MusicState musicState() const;
    bool isPlaying(uint64_t handle) const;
    // Rendered source position, retained at completion until stopped or reused.
    std::optional<double> playbackMilliseconds(uint64_t handle) const;
    void update(uint64_t handle, float gain, float pan);
    void stop(uint64_t handle);
    void stopEffects(uint64_t preserveHandle=0);
    void stopMusic();
    void stopAll();
    void setPaused(bool paused) { paused_ = paused; }
    void setMusicPaused(bool paused) { musicPaused_ = paused; }
    void setMasterVolume(float gain);
    void setEffectsVolume(float gain);
    void setVoicesVolume(float gain);
    void setMusicVolume(float gain);
    void setSampleRate(int rate);
    void mix(float* stereo, size_t frames);
    size_t activeVoices() const;
    void configureMix(AudioMixSettings);
    void prepare(const AudioClipPtr&,bool force=false); // main thread only; builds cached filters
    AudioStatistics statistics() const;
    void recordCallback(uint64_t ticks);
    void collectRetired(); // main thread; caller retains ownership until unlock
    using RetainedClips=std::array<AudioClipPtr,MaxVoices+8+3>;
    RetainedClips retainedClips() const;
private:
    struct Voice {
        AudioClipPtr clip;
        double position = 0;
        uint64_t handle = 0;
        int priority = 0;
        float gain = 1, pan = 0;
        bool loop = false, active = false, pauseWithWorld = true;
        AudioBus bus = AudioBus::Effects;
        bool native=false;
        unsigned nativeVolume=65535;
        int nativePan=0;
        uint64_t streamOffset=0;
        std::shared_ptr<const AudioResampleKernel> kernel;
        float left=0,right=0,fade=1;
        bool retiring=false;
        float fadeSeconds=.001f;
    };
    void retire(Voice&,float seconds=.003f);
    void initializeVoice(Voice&);
    std::shared_ptr<const AudioResampleKernel> kernelFor(const AudioClipPtr&);
    void mixBalanced(float*,size_t);
    size_t mixVoice(Voice& voice, float* stereo, size_t frames);
    std::array<Voice, MaxVoices> voices_{};
    std::array<Voice,8> tails_{};
    Voice music_, queuedMusic_, musicOverlay_;
    uint64_t musicSerial_ = 0;
    uint64_t nextHandle_ = 1;
    float master_ = 1, effects_ = 1, voicesGain_ = 1, musicGain_ = 1;
    // Effects have their own peak control before music/speech are added. A loud
    // effect must not impose its full gain reduction on the other buses.
    float effectsOutputGain_ = 1;
    // Stereo-linked output peak control: immediate attack, 80ms recovery.
    // No look-ahead/delay; source clocks and ordinary unity-gain mixes stay intact.
    float outputGain_ = 1;
    float outputRelease_ = 1 - std::exp(-1.0f / (SampleRate * .080f));
    int sampleRate_ = SampleRate;
    bool paused_ = false, musicPaused_ = false, musicNativeChannel_ = false;
    AudioMixSettings mixSettings_;
    AudioStatistics stats_;
    std::map<unsigned,std::shared_ptr<const AudioResampleKernel>> kernels_;
    float ramp_=1-std::exp(-1.f/(SampleRate*.005f));
    float duckAttack_=1-std::exp(-1.f/(SampleRate*.008f));
    float duckRelease_=1-std::exp(-1.f/(SampleRate*.200f));
    float dialogueDuck_=0,cueDuck_=0;
    float pauseEnvelope_=1,musicEnvelope_=1;
    std::array<std::array<float,2>,7> lastWorld_{};
    float ambienceSmooth_=.5f,headroomSmooth_=.7f,duckStrengthSmooth_=.5f;
    float masterSmooth_=1,effectsSmooth_=1,voicesSmooth_=1,musicSmooth_=1;
};

// Main-thread operations lock the callback. AudioMixer itself is deliberately
// independent of devices and locks, to permit deterministic offline rendering.
struct AudioOutputSettings {
    int sampleRate = 48000;
    int bitsPerSample = 32;
    int channels = 2;
    bool operator==(const AudioOutputSettings&) const = default;
};

class AudioDevice {
public:
    AudioDevice() = default;
    ~AudioDevice();
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;
    bool open(std::string& error);
    void close();
    // Open the replacement paused before releasing the current device; a failure
    // leaves current playback untouched. Active clips retain their source clocks.
    bool configure(AudioOutputSettings settings, std::string& error);
    AudioOutputSettings outputSettings() const { return output_; }
    bool available() const { return device_ != 0; }
    uint64_t play(AudioClipPtr clip, int priority = 0, float gain = 1, float pan = 0, bool loop = false, bool pauseWithWorld = true, AudioBus bus = AudioBus::Effects);
    // Native caller masks select among the first16 channels. Generic presentation
    // playback retains the existing32-voice adapter API.
    uint64_t playNative(AudioClipPtr clip,uint32_t mask,int priority,unsigned volume=65535,int pan=0,bool loop=false,bool pauseWithWorld=true,AudioBus bus=AudioBus::Effects);
    void updateNative(uint64_t handle,unsigned volume,int pan=0);
    void updateNativeChannel(unsigned channel,unsigned volume,int pan=0);
    bool nativeChannelPlaying(unsigned channel) const;
    void stopNativeChannels(uint32_t mask);
    void setMusicNativeChannel(bool native); // WAV music occupies native channel1; CD does not.
    void playMusic(AudioClipPtr clip, float gain = 1, bool loop = true);
    void queueMusic(AudioClipPtr clip, float gain = 1);
    void playMusicOverlay(AudioClipPtr clip, float gain = 1);
    void stopMusicOverlay();
    AudioMixer::MusicState musicState() const;
    bool isPlaying(uint64_t handle) const;
    // Rendered source position, retained at completion until stopped or reused.
    std::optional<double> playbackMilliseconds(uint64_t handle) const;
    void update(uint64_t handle, float gain, float pan);
    void stop(uint64_t handle);
    void stopEffects(uint64_t preserveHandle=0);
    void stopMusic();
    void stopAll();
    void setPaused(bool paused);
    void setMusicPaused(bool paused);
    void setMasterVolume(float gain);
    void setEffectsVolume(float gain);
    void setVoicesVolume(float gain);
    void setMusicVolume(float gain);
    void configureMix(AudioMixSettings);
    AudioStatistics statistics() const;
    void collectRetired();
private:
    friend struct AudioPipelineAuditAccess; // opt-in deterministic resource captures
    static void callback(void* userdata, Uint8* stream, int length);
    SDL_AudioDeviceID device_ = 0;
    bool ownsAudio_ = false;
    AudioMixer mixer_;
    AudioOutputSettings output_;
};
} // namespace vyruz
