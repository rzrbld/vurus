#pragma once
#include "audio/mixer.h"
#include "audio/voice_schedule.h"
#include "audio/music_playback.h"
#include "audio/input_commentary_native.h"
#include "audio/weapon_audio_native.h"
#include "audio/shield_alarm_native.h"
#include "audio/full_hud_shield_alarm_native.h"
#include "game/content.h"
#include <map>
#include <unordered_map>
#include <vector>
#include <array>
#include <optional>
namespace vyruz {
class GameWorld;
struct SceneAudioCue;
struct NativeTileBodyCatalog;
class GameAudio {
public:
    AudioDevice& device() { return device_; }
    bool load(const SceneContent&, const ContentReader&, const std::string& language, std::string& error,const NativeTileBodyCatalog* terrain=nullptr);
    void reset();
    void consume(GameWorld&, const SceneContent&, const std::vector<SceneAudioCue>&);
    void cue(const std::string& filename, bool voice=false);
    void briefingBip(); // Original428690 mask/priority, including its silent invalid channel.
    void speech(const std::string& filename,unsigned delay=0,unsigned hold=1000,unsigned tail=0);
    void updateSpeech(); // Also called in menus, where consume() is not run.
    void setMusicEnabled(bool enabled) { music_.setEnabled(enabled); }
    void setMusicOption(bool enabled) { music_.setOptionEnabled(enabled); }
    void setListenerHalfWidth(unsigned width) { listenerHalfWidth_=width; }
    void musicTrack(unsigned track);
    bool musicDirectory(const std::string& path, std::string& error);
    void updateMusic();
    bool musicFile(const std::string& path, std::string& error);
    const std::vector<std::string>& diagnostics() const { return diagnostics_; }
private:
    struct WeaponSound {
        std::string start,sound,end,dry;
        bool loop=false;
        uint32_t mask=0xff00;
        int priority=129;
    };
    struct TrackedSample { size_t object;uint64_t handle;std::optional<std::array<float,3>> source; };
    AudioClipPtr preload(const std::string&,const ContentReader&);
    AudioClipPtr clip(const std::string&) const;
    uint64_t raw(const std::string&,uint32_t mask,int priority,bool loop=false,bool pause=true,AudioBus bus=AudioBus::Effects);
    void explicitSample(const std::string&,unsigned channel);
    void consumeWeapons(const GameWorld&,const SceneContent&);
    AudioDevice device_;
    MusicPlayback music_{device_};
    NativeShieldAlarm shieldAlarm_;
    NativeFullHudShieldAlarm fullShieldAlarm_;
    GafSequence shieldSequence_;
    NativeInputCommentary commentary_; // Native process globals survive world replacement.
    std::string language_="ENGLISH";
    std::map<std::string,AudioClipPtr> clips_;
    std::map<std::string,std::vector<AudioClipPtr>> banks_;
    std::map<std::string,WeaponSound> weapons_;
    std::map<size_t,NativeWeaponAudioState> firing_;
    std::vector<size_t> weaponActors_;
    std::unordered_map<uint64_t,TrackedSample> spatialSamples_;
    std::vector<std::string> diagnostics_;
    VoiceSchedule speech_;
    unsigned surpriseMusic_=0,listenerHalfWidth_=320;
    bool weaponActorsInitialized_=false,motorBlocked_=false;
    double lastSoundWorldTime_=-1;
};
}
