#include "audio/mixer.h"
#include "audio/native_audio.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace vyruz {
namespace {
float gainValue(float value) { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; }
float sample(const AudioClip& clip, size_t frame, unsigned channel) {
    const auto& wave = clip.wave;
    const size_t index = (frame * wave.channels + (wave.channels == 1 ? 0 : channel)) * (wave.bitsPerSample / 8);
    if (wave.bitsPerSample == 8) return (int(wave.pcm[index]) - 128) / 128.0f;
    const unsigned bits = unsigned(wave.pcm[index]) | (unsigned(wave.pcm[index + 1]) << 8);
    const int value = bits >= 32768 ? int(bits) - 65536 : int(bits);
    return value / 32768.0f;
}
struct DeviceLock {
    SDL_AudioDeviceID device;
    explicit DeviceLock(SDL_AudioDeviceID id) : device(id) { if (device) SDL_LockAudioDevice(device); }
    ~DeviceLock() { if (device) SDL_UnlockAudioDevice(device); }
};
}
AudioClipPtr makeAudioClip(Wave wave, std::string& error) {
    if ((wave.channels != 1 && wave.channels != 2) ||
        (wave.bitsPerSample != 8 && wave.bitsPerSample != 16) ||
        wave.sampleRate < 1000 || wave.sampleRate > 384000 || wave.pcm.empty() ||
        wave.pcm.size() % (wave.channels * (wave.bitsPerSample / 8)) != 0) {
        error = "Unsupported or malformed PCM audio";
        return {};
    }
    auto clip = std::make_shared<AudioClip>();
    clip->frames = wave.pcm.size() / (wave.channels * (wave.bitsPerSample / 8));
    clip->wave = std::move(wave);
    error.clear();
    return clip;
}
uint64_t AudioMixer::play(AudioClipPtr clip, int priority, float gain, float pan, bool loop, bool pauseWithWorld, AudioBus bus) {
    if (!clip || !clip->frames) return 0;
    Voice* selected = nullptr;
    for (auto& voice : voices_) if (!voice.active) { selected = &voice; break; }
    if (!selected) {
        selected = &*std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
            return a.priority != b.priority ? a.priority < b.priority : a.handle < b.handle;
        });
        if (selected->priority > priority) return 0;
    }
    if (!nextHandle_) ++nextHandle_;
    *selected = {std::move(clip), 0, nextHandle_++, priority, gainValue(gain),
                 std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0, loop, true, pauseWithWorld, bus};
    return selected->handle;
}
uint64_t AudioMixer::playNative(AudioClipPtr clip,uint32_t mask,int priority,unsigned volume,int pan,bool loop,bool pauseWithWorld,AudioBus bus) {
    if(!clip||!clip->frames)return 0;
    std::array<NativeAudioChannel,16> candidates;
    for(size_t i=0;i<candidates.size();++i){const auto& v=voices_[i];
        double remaining=0;
        if(v.active&&v.clip){const auto position=v.loop?std::fmod(v.position,double(v.clip->frames)):v.position;
            remaining=std::max(0.0,double(v.clip->frames)-position)*1000/v.clip->wave.sampleRate;}
        candidates[i]={v.active,v.priority,static_cast<int32_t>(std::min(remaining,double(INT32_MAX)))};
        if(i==0&&(musicOverlay_.active||(musicNativeChannel_&&music_.active)))candidates[i]={true,255,INT32_MAX};
    }
    const auto selected=selectNativeAudioChannel(candidates,mask,priority);if(!selected)return 0;
    if(*selected==0){musicOverlay_.active=false;if(musicNativeChannel_)music_.active=false;}
    if(!nextHandle_)++nextHandle_;
    auto& v=voices_[*selected];v={std::move(clip),0,nextHandle_++,priority,1,0,loop,true,pauseWithWorld,bus};
    v.native=true;v.nativeVolume=std::min(volume,65535u);v.nativePan=std::clamp(pan,-32767,32767);return v.handle;
}
void AudioMixer::updateNative(uint64_t handle,unsigned volume,int pan){
    for(auto& v:voices_)if(v.active&&v.handle==handle){v.nativeVolume=std::min(volume,65535u);v.nativePan=std::clamp(pan,-32767,32767);}
}
void AudioMixer::updateNativeChannel(unsigned channel,unsigned volume,int pan){if(channel>0&&channel<=16){auto& v=voices_[channel-1];if(v.active){v.nativeVolume=std::min(volume,65535u);v.nativePan=std::clamp(pan,-32767,32767);}}}
bool AudioMixer::nativeChannelPlaying(unsigned channel) const{return channel>0&&channel<=16&&(voices_[channel-1].active||(channel==1&&(musicOverlay_.active||(musicNativeChannel_&&music_.active))));}
void AudioMixer::stopNativeChannels(uint32_t mask){for(size_t i=0;i<16;++i)if(mask&(1u<<i)){voices_[i].active=false;voices_[i].handle=0;}if(mask&1){musicOverlay_.active=false;if(musicNativeChannel_)music_.active=false;}}
void AudioMixer::setMusicNativeChannel(bool native){musicNativeChannel_=native;if(native&&music_.active){voices_[0].active=false;voices_[0].handle=0;}}
void AudioMixer::playMusic(AudioClipPtr clip, float gain, bool loop) {
    if(musicNativeChannel_&&clip){voices_[0].active=false;voices_[0].handle=0;}
    queuedMusic_ = {};
    ++musicSerial_;
    music_ = {std::move(clip), 0, 0, 0, gainValue(gain), 0, loop, false};
    music_.bus = AudioBus::Presentation;
    music_.active = music_.clip && music_.clip->frames;
}
void AudioMixer::queueMusic(AudioClipPtr clip, float gain) {
    queuedMusic_ = {std::move(clip), 0, 0, 0, gainValue(gain), 0, false, false};
    queuedMusic_.active = queuedMusic_.clip && queuedMusic_.clip->frames;
}
void AudioMixer::playMusicOverlay(AudioClipPtr clip, float gain) {
    if(clip){voices_[0].active=false;voices_[0].handle=0;}
    musicOverlay_ = {std::move(clip), 0, 0, 0, gainValue(gain), 0, true, false};
    musicOverlay_.active = musicOverlay_.clip && musicOverlay_.clip->frames;
}
void AudioMixer::stopMusicOverlay() { musicOverlay_.active = false; }
AudioMixer::MusicState AudioMixer::musicState() const {
    return {musicSerial_, music_.active, queuedMusic_.active,
            music_.clip ? music_.position * 1000.0 / music_.clip->wave.sampleRate : 0.0};
}
bool AudioMixer::isPlaying(uint64_t handle) const {
    return handle && std::any_of(voices_.begin(), voices_.end(), [handle](const Voice& voice) { return voice.active && voice.handle == handle; });
}
std::optional<double> AudioMixer::playbackMilliseconds(uint64_t handle) const {
    if (!handle) return std::nullopt;
    for (const auto& voice : voices_) if (voice.handle == handle && voice.clip) {
        const double position = voice.loop ? std::fmod(voice.position, double(voice.clip->frames))
                                          : std::min(voice.position, double(voice.clip->frames));
        return position * 1000.0 / voice.clip->wave.sampleRate;
    }
    return std::nullopt;
}
void AudioMixer::update(uint64_t handle, float gain, float pan) {
    for (auto& voice : voices_) if (voice.handle == handle && voice.active) {
        voice.gain = gainValue(gain);
        voice.pan = std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0;
    }
}
void AudioMixer::stopEffects() { for (auto& voice : voices_) { voice.active = false; voice.handle = 0; } }
void AudioMixer::stop(uint64_t handle) { for (auto& voice : voices_) if (voice.handle == handle) { voice.active = false; voice.handle = 0; } }
void AudioMixer::stopMusic() { music_.active = queuedMusic_.active = musicOverlay_.active = false; }
void AudioMixer::stopAll() { stopEffects(); stopMusic(); outputGain_ = 1; }
void AudioMixer::setMasterVolume(float gain) { master_ = gainValue(gain); }
void AudioMixer::setEffectsVolume(float gain) { effects_ = gainValue(gain); }
void AudioMixer::setVoicesVolume(float gain) { voicesGain_ = gainValue(gain); }
void AudioMixer::setMusicVolume(float gain) { musicGain_ = gainValue(gain); }
void AudioMixer::setSampleRate(int rate) {
    if (rate >= 1000 && rate <= 384000) {
        sampleRate_ = rate;
        outputRelease_ = 1 - std::exp(-1.0f / (sampleRate_ * .080f));
    }
}
size_t AudioMixer::activeVoices() const {
    return std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.active; });
}
size_t AudioMixer::mixVoice(Voice& voice, float* stereo, size_t frames) {
    if (!voice.active) return 0;
    const auto& clip = *voice.clip;
    const double step = double(clip.wave.sampleRate) / sampleRate_;
    const float busGain = &voice == &music_ ? (musicOverlay_.active ? 0.0f : musicGain_) : &voice == &musicOverlay_ ? musicGain_ : voice.bus == AudioBus::Effects ? effects_ : voice.bus == AudioBus::Voices ? voicesGain_ : 1.0f;
    float left,right;
    if(voice.native){
        const auto pan=nativeAudioPanGains(voice.nativePan);
        const float gain=busGain>0?nativeAudioVolumeGain(static_cast<uint16_t>(std::clamp(voice.nativeVolume*busGain,0.f,65535.f))):0;
        left=gain*pan.first;right=gain*pan.second;
    }else{
        left=voice.gain*busGain*(voice.pan>0?1-voice.pan:1);
        right=voice.gain*busGain*(voice.pan<0?1+voice.pan:1);
    }
    size_t i = 0;
    for (; i < frames; ++i) {
        if (voice.position >= clip.frames) {
            if (voice.loop) voice.position = std::fmod(voice.position, double(clip.frames));
            else { voice.active = false; break; }
        }
        const size_t first = size_t(voice.position);
        const size_t second = first + 1 < clip.frames ? first + 1 : voice.loop ? 0 : first;
        const float fraction = float(voice.position - first);
        for (unsigned channel = 0; channel < 2; ++channel) {
            const float a = sample(clip, first, channel), b = sample(clip, second, channel);
            stereo[2 * i + channel] += (a + (b - a) * fraction) * (channel ? right : left);
        }
        voice.position += step;
    }
    if (!voice.loop && voice.position >= clip.frames) voice.active = false;
    return i;
    // Keep clip alive after completion; its memory is freed only by a main-thread
    // play/replacement or device destruction, never by this callback.
}
void AudioMixer::mix(float* stereo, size_t frames) {
    if (!stereo) return;
    std::fill_n(stereo, frames * 2, 0.0f);
    for (auto& voice : voices_) if (!paused_ || !voice.pauseWithWorld) mixVoice(voice, stereo, frames);
    if (!paused_ && !musicPaused_) {
        size_t count = mixVoice(music_, stereo, frames);
        if (!music_.active && queuedMusic_.active) {
            // Swapping retains the completed clip until the main thread queues
            // another track; no destruction or allocation on the audio callback.
            std::swap(music_, queuedMusic_);
            ++musicSerial_;
            mixVoice(music_, stereo + count * 2, frames - count);
        }
        mixVoice(musicOverlay_, stereo, frames);
    }
    for (size_t i = 0; i < frames; ++i) {
        const float left = stereo[2 * i] * master_, right = stereo[2 * i + 1] * master_;
        const float peak = std::max(std::abs(left), std::abs(right));
        // Hard-clipping each channel flattened overlapping effects. Reduce the
        // combined signal instead, with one gain for both channels to preserve
        // their spatial balance. Recover across callback boundaries, independent
        // of buffer size, without allocating or delaying the source clocks.
        const float ceiling = peak > 1 ? 1 / peak : 1;
        outputGain_ = std::min(ceiling, outputGain_ + outputRelease_ * (1 - outputGain_));
        stereo[2 * i] = std::clamp(left * outputGain_, -1.0f, 1.0f);
        stereo[2 * i + 1] = std::clamp(right * outputGain_, -1.0f, 1.0f);
    }
}
AudioDevice::~AudioDevice() { close(); }
bool AudioDevice::open(std::string& error) {
    SDL_SetMainReady();
    if (device_) { error.clear(); return true; }
    return configure(output_, error);
}
bool AudioDevice::configure(AudioOutputSettings settings, std::string& error) {
    if ((settings.sampleRate != 11025 && settings.sampleRate != 22050 && settings.sampleRate != 48000) ||
        (settings.bitsPerSample != 8 && settings.bitsPerSample != 16 && settings.bitsPerSample != 32) ||
        (settings.channels != 1 && settings.channels != 2)) {
        error = "Unsupported audio output format";
        return false;
    }
    if (device_ && settings == output_) { error.clear(); return true; }
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { error = SDL_GetError(); return false; }
        ownsAudio_ = true;
    }
    SDL_AudioSpec desired{}, obtained{};
    desired.freq = settings.sampleRate;
    desired.format = settings.bitsPerSample == 8 ? AUDIO_U8 : settings.bitsPerSample == 16 ? AUDIO_S16SYS : AUDIO_F32SYS;
    desired.channels = Uint8(settings.channels);
    desired.samples = 512;
    desired.callback = callback;
    desired.userdata = this;
    const auto replacement = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (!replacement) {
        error = SDL_GetError();
        if (!device_ && ownsAudio_) { SDL_QuitSubSystem(SDL_INIT_AUDIO); ownsAudio_ = false; }
        return false;
    }
    // SDL starts a new callback device paused. Closing the old one waits for its
    // callback before changing the mixer rate or output conversion format.
    if (device_) SDL_CloseAudioDevice(device_);
    device_ = replacement;
    output_ = settings;
    mixer_.setSampleRate(settings.sampleRate);
    error.clear();
    SDL_PauseAudioDevice(device_, 0);
    return true;
}
void AudioDevice::close() {
    if (device_) { SDL_CloseAudioDevice(device_); device_ = 0; }
    mixer_ = AudioMixer{};
    if (ownsAudio_) { SDL_QuitSubSystem(SDL_INIT_AUDIO); ownsAudio_ = false; }
}
void AudioDevice::callback(void* userdata, Uint8* stream, int length) {
    if (length <= 0) return;
    auto& device = *static_cast<AudioDevice*>(userdata);
    const auto format = device.output_;
    std::memset(stream, format.bitsPerSample == 8 ? 128 : 0, size_t(length));
    const size_t bytesPerFrame = size_t(format.channels * format.bitsPerSample / 8);
    const size_t frames = size_t(length) / bytesPerFrame;
    // Fixed scratch storage keeps allocation out of the real-time callback.
    std::array<float, 1024> scratch{};
    for (size_t offset = 0; offset < frames;) {
        const size_t count = std::min(frames - offset, scratch.size() / 2);
        device.mixer_.mix(scratch.data(), count);
        for (size_t i = 0; i < count; ++i) for (int channel = 0; channel < format.channels; ++channel) {
            const float value = format.channels == 1 ? (scratch[i * 2] + scratch[i * 2 + 1]) * .5f : scratch[i * 2 + channel];
            const size_t sampleIndex = (offset + i) * size_t(format.channels) + size_t(channel);
            if (format.bitsPerSample == 8) stream[sampleIndex] = Uint8(std::clamp(int(std::lround(value * 128 + 128)), 0, 255));
            else if (format.bitsPerSample == 16) {
                const int16_t sampleValue = int16_t(std::clamp(int(std::lround(value * 32768)), -32768, 32767));
                std::memcpy(stream + sampleIndex * sizeof(sampleValue), &sampleValue, sizeof(sampleValue));
            } else std::memcpy(stream + sampleIndex * sizeof(value), &value, sizeof(value));
        }
        offset += count;
    }
}
uint64_t AudioDevice::play(AudioClipPtr clip, int priority, float gain, float pan, bool loop, bool pauseWithWorld, AudioBus bus) {
    if (!device_) return 0;
    DeviceLock lock(device_);
    return mixer_.play(std::move(clip), priority, gain, pan, loop, pauseWithWorld, bus);
}
uint64_t AudioDevice::playNative(AudioClipPtr clip,uint32_t mask,int priority,unsigned volume,int pan,bool loop,bool pauseWithWorld,AudioBus bus){
    if(!device_)return 0;DeviceLock lock(device_);return mixer_.playNative(std::move(clip),mask,priority,volume,pan,loop,pauseWithWorld,bus);
}
void AudioDevice::updateNative(uint64_t handle,unsigned volume,int pan){DeviceLock lock(device_);mixer_.updateNative(handle,volume,pan);}
void AudioDevice::updateNativeChannel(unsigned channel,unsigned volume,int pan){DeviceLock lock(device_);mixer_.updateNativeChannel(channel,volume,pan);}
bool AudioDevice::nativeChannelPlaying(unsigned channel)const{DeviceLock lock(device_);return mixer_.nativeChannelPlaying(channel);}
void AudioDevice::stopNativeChannels(uint32_t mask){DeviceLock lock(device_);mixer_.stopNativeChannels(mask);}
void AudioDevice::setMusicNativeChannel(bool native) { DeviceLock lock(device_);mixer_.setMusicNativeChannel(native); }
void AudioDevice::playMusic(AudioClipPtr clip, float gain, bool loop) { DeviceLock lock(device_); mixer_.playMusic(std::move(clip), gain, loop); }
void AudioDevice::queueMusic(AudioClipPtr clip, float gain) { DeviceLock lock(device_); mixer_.queueMusic(std::move(clip), gain); }
void AudioDevice::playMusicOverlay(AudioClipPtr clip, float gain) { DeviceLock lock(device_); mixer_.playMusicOverlay(std::move(clip), gain); }
void AudioDevice::stopMusicOverlay() { DeviceLock lock(device_); mixer_.stopMusicOverlay(); }
AudioMixer::MusicState AudioDevice::musicState() const { DeviceLock lock(device_); return mixer_.musicState(); }
bool AudioDevice::isPlaying(uint64_t handle) const { DeviceLock lock(device_); return mixer_.isPlaying(handle); }
std::optional<double> AudioDevice::playbackMilliseconds(uint64_t handle) const { DeviceLock lock(device_); return mixer_.playbackMilliseconds(handle); }
void AudioDevice::update(uint64_t handle, float gain, float pan) { DeviceLock lock(device_); mixer_.update(handle, gain, pan); }
void AudioDevice::stopEffects() { DeviceLock lock(device_); mixer_.stopEffects(); }
void AudioDevice::stop(uint64_t handle) { DeviceLock lock(device_); mixer_.stop(handle); }
void AudioDevice::stopMusic() { DeviceLock lock(device_); mixer_.stopMusic(); }
void AudioDevice::stopAll() { DeviceLock lock(device_); mixer_.stopAll(); }
void AudioDevice::setPaused(bool paused) { DeviceLock lock(device_); mixer_.setPaused(paused); }
void AudioDevice::setMusicPaused(bool paused) { DeviceLock lock(device_); mixer_.setMusicPaused(paused); }
void AudioDevice::setMasterVolume(float gain) { DeviceLock lock(device_); mixer_.setMasterVolume(gain); }
void AudioDevice::setEffectsVolume(float gain) { DeviceLock lock(device_); mixer_.setEffectsVolume(gain); }
void AudioDevice::setVoicesVolume(float gain) { DeviceLock lock(device_); mixer_.setVoicesVolume(gain); }
void AudioDevice::setMusicVolume(float gain) { DeviceLock lock(device_); mixer_.setMusicVolume(gain); }
} // namespace vyruz
