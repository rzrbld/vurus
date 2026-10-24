#include "audio/mixer.h"
#include "audio/native_audio.h"
#include "audio/resample_kernel.h"
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
    ++stats_.requests;
    if (!clip || !clip->frames) {++stats_.rejected;return 0;}
    Voice* selected = nullptr;
    for (auto& voice : voices_) if (!voice.active) { selected = &voice; break; }
    if (!selected) {
        selected = &*std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) {
            return a.priority != b.priority ? a.priority < b.priority : a.handle < b.handle;
        });
        if (selected->priority > priority) {++stats_.rejected;return 0;}
    }
    if(selected->active)++stats_.stolen;retire(*selected);
    if (!nextHandle_) ++nextHandle_;
    *selected = {std::move(clip), 0, nextHandle_++, priority, gainValue(gain),
                 std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0, loop, true, pauseWithWorld, bus};
    initializeVoice(*selected);
    return selected->handle;
}
uint64_t AudioMixer::playNative(AudioClipPtr clip,uint32_t mask,int priority,unsigned volume,int pan,bool loop,bool pauseWithWorld,AudioBus bus) {
    ++stats_.requests;
    if(!clip||!clip->frames){++stats_.rejected;return 0;}
    volume=std::min(volume,65535u);pan=std::clamp(pan,-32767,32767);
    if(mixSettings_.balanced&&(bus==AudioBus::Effects||bus==AudioBus::Ambience)){
        unsigned similar=0,ambient=0;
        for(size_t i=0;i<16;++i){const auto& v=voices_[i];if(!v.active)continue;
            ambient+=v.bus==AudioBus::Ambience;
            if((mask&(1u<<i))&&v.clip==clip&&v.bus==bus&&v.priority>=priority&&
               v.position*1000/clip->wave.sampleRate<12&&std::abs(v.nativePan-pan)<4096&&v.nativeVolume>=volume)++similar;
        }
        // Prevent phase-coherent same-position bursts from becoming a wall of
        // identical PCM. Return no shared handle: animation ownership stays unique.
        if(similar>=2||(bus==AudioBus::Ambience&&priority<191&&ambient>=4)){
            ++stats_.rejected;++stats_.repetitions;return 0;
        }
    }
    std::array<NativeAudioChannel,16> candidates;
    for(size_t i=0;i<candidates.size();++i){const auto& v=voices_[i];
        double remaining=0;
        if(v.active&&v.clip){const auto position=v.loop?std::fmod(v.position,double(v.clip->frames)):v.position;
            remaining=std::max(0.0,double(v.clip->frames)-position)*1000/v.clip->wave.sampleRate;}
        candidates[i]={v.active,v.priority,static_cast<int32_t>(std::min(remaining,double(INT32_MAX)))};
        if(mixSettings_.balanced&&v.active&&v.bus==AudioBus::Voices&&bus!=AudioBus::Voices)
            candidates[i].priority=INT32_MAX;
        if(i==0&&(musicOverlay_.active||(musicNativeChannel_&&music_.active)))candidates[i]={true,255,INT32_MAX};
    }
    const auto selected=selectNativeAudioChannel(candidates,mask,priority);if(!selected){++stats_.rejected;return 0;}
    if(*selected==0){retire(musicOverlay_);if(musicNativeChannel_)retire(music_);}
    if(!nextHandle_)++nextHandle_;
    auto& v=voices_[*selected];if(v.active)++stats_.stolen;retire(v);v={std::move(clip),0,nextHandle_++,priority,1,0,loop,true,pauseWithWorld,bus};
    initializeVoice(v);v.native=true;v.nativeVolume=std::min(volume,65535u);v.nativePan=std::clamp(pan,-32767,32767);return v.handle;
}
void AudioMixer::updateNative(uint64_t handle,unsigned volume,int pan){
    for(auto& v:voices_)if(v.active&&v.handle==handle){v.nativeVolume=std::min(volume,65535u);v.nativePan=std::clamp(pan,-32767,32767);}
}
void AudioMixer::updateNativeChannel(unsigned channel,unsigned volume,int pan){if(channel>0&&channel<=16){auto& v=voices_[channel-1];if(v.active){v.nativeVolume=std::min(volume,65535u);v.nativePan=std::clamp(pan,-32767,32767);}}}
bool AudioMixer::nativeChannelPlaying(unsigned channel) const{return channel>0&&channel<=16&&(voices_[channel-1].active||(channel==1&&(musicOverlay_.active||(musicNativeChannel_&&music_.active))));}
void AudioMixer::stopNativeChannels(uint32_t mask){for(size_t i=0;i<16;++i)if(mask&(1u<<i)){retire(voices_[i]);voices_[i].handle=0;}if(mask&1){retire(musicOverlay_);if(musicNativeChannel_)retire(music_);}}
void AudioMixer::setMusicNativeChannel(bool native){musicNativeChannel_=native;if(native&&music_.active){retire(voices_[0]);voices_[0].handle=0;}}
void AudioMixer::playMusic(AudioClipPtr clip, float gain, bool loop) {
    if(musicNativeChannel_&&clip){retire(voices_[0]);voices_[0].handle=0;}
    retire(music_,.04f);
    queuedMusic_ = {};
    ++musicSerial_;
    music_ = {std::move(clip), 0, 0, 0, gainValue(gain), 0, loop, false};
    music_.bus = AudioBus::Music;
    music_.active = music_.clip && music_.clip->frames;
    initializeVoice(music_);music_.fadeSeconds=.04f;
}
void AudioMixer::queueMusic(AudioClipPtr clip, float gain) {
    queuedMusic_ = {std::move(clip), 0, 0, 0, gainValue(gain), 0, false, false};
    queuedMusic_.active = queuedMusic_.clip && queuedMusic_.clip->frames;
    initializeVoice(queuedMusic_);queuedMusic_.bus=AudioBus::Music;
}
void AudioMixer::playMusicOverlay(AudioClipPtr clip, float gain) {
    if(clip){retire(voices_[0]);voices_[0].handle=0;}
    retire(musicOverlay_,.02f);
    musicOverlay_ = {std::move(clip), 0, 0, 0, gainValue(gain), 0, true, false};
    musicOverlay_.active = musicOverlay_.clip && musicOverlay_.clip->frames;
    initializeVoice(musicOverlay_);musicOverlay_.bus=AudioBus::Music;musicOverlay_.fadeSeconds=.02f;
}
void AudioMixer::stopMusicOverlay() { retire(musicOverlay_); }
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
void AudioMixer::stopEffects(uint64_t preserveHandle) {
    for (auto& voice : voices_) if(!preserveHandle || voice.handle!=preserveHandle) {
        retire(voice);voice.handle=0;
    }
}
void AudioMixer::stop(uint64_t handle) { for (auto& voice : voices_) if (handle && voice.handle == handle) {retire(voice);voice = {};} }
void AudioMixer::stopMusic() { retire(music_,.04f);queuedMusic_.active=false;retire(musicOverlay_,.02f); }
void AudioMixer::stopAll() { stopEffects(); stopMusic();for(auto&v:tails_)v.active=false;effectsOutputGain_ = outputGain_ = 1;dialogueDuck_=cueDuck_=0;lastWorld_={}; }
void AudioMixer::setMasterVolume(float gain) { master_ = gainValue(gain); }
void AudioMixer::setEffectsVolume(float gain) { effects_ = gainValue(gain); }
void AudioMixer::setVoicesVolume(float gain) { voicesGain_ = gainValue(gain); }
void AudioMixer::setMusicVolume(float gain) { musicGain_ = gainValue(gain); }
void AudioMixer::setSampleRate(int rate) {
    if (rate >= 1000 && rate <= 384000) {
        sampleRate_ = rate;
        outputRelease_ = 1 - std::exp(-1.0f / (sampleRate_ * .080f));
        ramp_=1-std::exp(-1.f/(sampleRate_*.005f));
        duckAttack_=1-std::exp(-1.f/(sampleRate_*.008f));
        duckRelease_=1-std::exp(-1.f/(sampleRate_*.200f));
        kernels_.clear();
        for(auto&v:voices_)v.kernel=kernelFor(v.clip);
        for(auto&v:tails_)v.kernel=kernelFor(v.clip);
        for(auto*v:{&music_,&queuedMusic_,&musicOverlay_})v->kernel=kernelFor(v->clip);
    }
}
size_t AudioMixer::activeVoices() const {
    return std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.active; });
}
std::shared_ptr<const AudioResampleKernel> AudioMixer::kernelFor(const AudioClipPtr& clip) {
    if(!mixSettings_.highQuality||!clip||clip->wave.sampleRate==unsigned(sampleRate_))return {};
    if(kernels_.size()>=16&&!kernels_.contains(clip->wave.sampleRate))kernels_.erase(kernels_.begin());
    auto [it,added]=kernels_.try_emplace(clip->wave.sampleRate);
    if(added)it->second=std::make_shared<AudioResampleKernel>(clip->wave.sampleRate,unsigned(sampleRate_));
    return clip->frames>=it->second->taps?it->second:nullptr;
}
void AudioMixer::prepare(const AudioClipPtr& clip,bool force){
    if(!mixSettings_.highQuality&&!force)return;
    if(!clip||clip->wave.sampleRate==unsigned(sampleRate_))return;
    if(kernels_.size()>=16&&!kernels_.contains(clip->wave.sampleRate))kernels_.erase(kernels_.begin());
    auto [it,added]=kernels_.try_emplace(clip->wave.sampleRate);
    if(added)it->second=std::make_shared<AudioResampleKernel>(clip->wave.sampleRate,unsigned(sampleRate_));
}
void AudioMixer::initializeVoice(Voice& v){v.kernel=kernelFor(v.clip);v.left=v.right=-1;v.fade=mixSettings_.balanced?0:1;}
void AudioMixer::retire(Voice& v,float seconds){
    if(v.active&&mixSettings_.balanced){
        auto target=std::find_if(tails_.begin(),tails_.end(),[](const auto&t){return !t.active;});
        if(target!=tails_.end()){*target=v;target->retiring=true;target->fadeSeconds=seconds;}
    }
    v.active=false;
}
void AudioMixer::configureMix(AudioMixSettings settings){
    settings.ambience=gainValue(settings.ambience);settings.ducking=gainValue(settings.ducking);
    settings.headroom=std::isfinite(settings.headroom)?std::clamp(settings.headroom,.25f,1.f):.7f;
    if(!mixSettings_.balanced&&settings.balanced){
        masterSmooth_=master_;effectsSmooth_=effects_;voicesSmooth_=voicesGain_;musicSmooth_=musicGain_;
        ambienceSmooth_=settings.ambience;headroomSmooth_=settings.headroom;duckStrengthSmooth_=settings.ducking;
        for(auto&v:voices_)v.left=v.right=-1;
    }
    mixSettings_=settings;
    for(auto&v:voices_)v.kernel=kernelFor(v.clip);
    for(auto&v:tails_)v.kernel=kernelFor(v.clip);
    for(auto*v:{&music_,&queuedMusic_,&musicOverlay_})v->kernel=kernelFor(v->clip);
    if(!settings.balanced)for(auto&v:tails_)v.active=false;
}
AudioMixer::RetainedClips AudioMixer::retainedClips()const{
    RetainedClips result;size_t i=0;for(const auto&v:voices_)result[i++]=v.clip;
    for(const auto&v:tails_)result[i++]=v.clip;
    for(const auto*v:{&music_,&queuedMusic_,&musicOverlay_})result[i++]=v->clip;
    return result;
}
void AudioMixer::collectRetired(){for(auto&v:tails_)if(!v.active)v={};}
AudioStatistics AudioMixer::statistics()const{
    auto result=stats_;result.active={};
    for(const auto&v:voices_)if(v.active)++result.active[size_t(v.bus)];
    for(const auto&v:tails_)if(v.active)++result.active[size_t(v.bus)];
    if(music_.active)++result.active[6];if(musicOverlay_.active)++result.active[6];
    return result;
}
void AudioMixer::recordCallback(uint64_t ticks){++stats_.callbacks;stats_.callbackTicks+=ticks;stats_.maxCallbackTicks=std::max(stats_.maxCallbackTicks,ticks);}
#include "audio/mixer_dsp.inc"
void AudioMixer::mix(float* stereo, size_t frames) {
    if (!stereo) return;
    if(mixSettings_.balanced){mixBalanced(stereo,frames);return;}
    std::fill_n(stereo, frames * 2, 0.0f);
    for (auto& voice : voices_)
        if ((voice.bus == AudioBus::Effects||voice.bus==AudioBus::Cues||voice.bus==AudioBus::Ambience) && (!paused_ || !voice.pauseWithWorld))
            mixVoice(voice, stereo, frames);
    // Bound the combined effects before adding music, speech or movie audio.
    // Detect after the master control so quiet, safe mixes remain untouched.
    // This uses the same linked envelope as the previous effects-only output:
    // individual effects and effects-only overload retain their levels/shape.
    for (size_t i = 0; i < frames; ++i) {
        const float peak = std::max(std::abs(stereo[2*i]), std::abs(stereo[2*i+1])) * master_;
        const float ceiling = peak > 1 ? 1 / peak : 1;
        effectsOutputGain_ = std::min(ceiling, effectsOutputGain_ + outputRelease_ * (1 - effectsOutputGain_));
        stereo[2*i] *= effectsOutputGain_;
        stereo[2*i+1] *= effectsOutputGain_;
    }
    for (auto& voice : voices_)
        if (voice.bus != AudioBus::Effects&&voice.bus!=AudioBus::Cues&&voice.bus!=AudioBus::Ambience && (!paused_ || !voice.pauseWithWorld))
            mixVoice(voice, stereo, frames);
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
        stats_.inputPeak=std::max(stats_.inputPeak,peak);if(peak>1)++stats_.overloads;
        // Hard-clipping each channel flattened overlapping effects. Reduce the
        // combined signal instead, with one gain for both channels to preserve
        // their spatial balance. Recover across callback boundaries, independent
        // of buffer size, without allocating or delaying the source clocks.
        const float ceiling = peak > 1 ? 1 / peak : 1;
        outputGain_ = std::min(ceiling, outputGain_ + outputRelease_ * (1 - outputGain_));
        stereo[2 * i] = std::clamp(left * outputGain_, -1.0f, 1.0f);
        stereo[2 * i + 1] = std::clamp(right * outputGain_, -1.0f, 1.0f);
        stats_.minimumGain=std::min(stats_.minimumGain,outputGain_*effectsOutputGain_);
        stats_.outputPeak=std::max(stats_.outputPeak,std::max(std::abs(stereo[2*i]),std::abs(stereo[2*i+1])));
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
    const auto started=SDL_GetPerformanceCounter();
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
    device.mixer_.recordCallback(SDL_GetPerformanceCounter()-started);
}
uint64_t AudioDevice::play(AudioClipPtr clip, int priority, float gain, float pan, bool loop, bool pauseWithWorld, AudioBus bus) {
    if (!device_) return 0;
    mixer_.prepare(clip);const auto incoming=clip;AudioMixer::RetainedClips retired;
    DeviceLock lock(device_);retired=mixer_.retainedClips();
    return mixer_.play(std::move(clip), priority, gain, pan, loop, pauseWithWorld, bus);
}
uint64_t AudioDevice::playNative(AudioClipPtr clip,uint32_t mask,int priority,unsigned volume,int pan,bool loop,bool pauseWithWorld,AudioBus bus){
    if(!device_)return 0;mixer_.prepare(clip);const auto incoming=clip;AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();return mixer_.playNative(std::move(clip),mask,priority,volume,pan,loop,pauseWithWorld,bus);
}
void AudioDevice::updateNative(uint64_t handle,unsigned volume,int pan){DeviceLock lock(device_);mixer_.updateNative(handle,volume,pan);}
void AudioDevice::updateNativeChannel(unsigned channel,unsigned volume,int pan){DeviceLock lock(device_);mixer_.updateNativeChannel(channel,volume,pan);}
bool AudioDevice::nativeChannelPlaying(unsigned channel)const{
    // Invalid native channels have no state, including before open/after close.
    if(channel==0 || channel>16)return false;
    DeviceLock lock(device_);return mixer_.nativeChannelPlaying(channel);
}
void AudioDevice::stopNativeChannels(uint32_t mask) { AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.stopNativeChannels(mask); }
void AudioDevice::setMusicNativeChannel(bool native) { AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.setMusicNativeChannel(native); }
void AudioDevice::playMusic(AudioClipPtr clip, float gain, bool loop) { mixer_.prepare(clip);const auto incoming=clip;AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.playMusic(std::move(clip), gain, loop); }
void AudioDevice::queueMusic(AudioClipPtr clip, float gain) { mixer_.prepare(clip);const auto incoming=clip;AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.queueMusic(std::move(clip), gain); }
void AudioDevice::playMusicOverlay(AudioClipPtr clip, float gain) { mixer_.prepare(clip);const auto incoming=clip;AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.playMusicOverlay(std::move(clip), gain); }
void AudioDevice::stopMusicOverlay() { AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.stopMusicOverlay(); }
AudioMixer::MusicState AudioDevice::musicState() const { DeviceLock lock(device_); return mixer_.musicState(); }
bool AudioDevice::isPlaying(uint64_t handle) const { DeviceLock lock(device_); return mixer_.isPlaying(handle); }
std::optional<double> AudioDevice::playbackMilliseconds(uint64_t handle) const { DeviceLock lock(device_); return mixer_.playbackMilliseconds(handle); }
void AudioDevice::update(uint64_t handle, float gain, float pan) { DeviceLock lock(device_); mixer_.update(handle, gain, pan); }
void AudioDevice::stopEffects(uint64_t preserveHandle) { AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.stopEffects(preserveHandle); }
void AudioDevice::stop(uint64_t handle) { AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.stop(handle); }
void AudioDevice::stopMusic() { AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.stopMusic(); }
void AudioDevice::stopAll() { AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.stopAll(); }
void AudioDevice::setPaused(bool paused) { DeviceLock lock(device_); mixer_.setPaused(paused); }
void AudioDevice::setMusicPaused(bool paused) { DeviceLock lock(device_); mixer_.setMusicPaused(paused); }
void AudioDevice::setMasterVolume(float gain) { DeviceLock lock(device_); mixer_.setMasterVolume(gain); }
void AudioDevice::setEffectsVolume(float gain) { DeviceLock lock(device_); mixer_.setEffectsVolume(gain); }
void AudioDevice::setVoicesVolume(float gain) { DeviceLock lock(device_); mixer_.setVoicesVolume(gain); }
void AudioDevice::setMusicVolume(float gain) { DeviceLock lock(device_); mixer_.setMusicVolume(gain); }
void AudioDevice::collectRetired(){AudioMixer::RetainedClips retired;DeviceLock lock(device_);retired=mixer_.retainedClips();mixer_.collectRetired();}
void AudioDevice::configureMix(AudioMixSettings settings){
    AudioMixer::RetainedClips clips;{DeviceLock lock(device_);clips=mixer_.retainedClips();}
    if(settings.highQuality)for(const auto&clip:clips)mixer_.prepare(clip,true);
    DeviceLock lock(device_);mixer_.configureMix(settings);
}
AudioStatistics AudioDevice::statistics()const{DeviceLock lock(device_);return mixer_.statistics();}
} // namespace vyruz
