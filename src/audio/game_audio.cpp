#include "audio/game_audio.h"
#include "audio/native_audio.h"
#include "assets/gaf_audio.h"
#include "game/world.h"
#include "game/properties.h"
#include "render/scene.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <random>
#include <chrono>
#include <set>
namespace vyruz {
namespace {
std::string normalized(std::string path) {
    std::replace(path.begin(),path.end(),'\\','/');return folded(std::move(path));
}
std::string soundPath(const std::string& name) {
    if(name.empty())return {};
    if(name.find_first_of("/\\")!=std::string::npos)return normalized(name);
    return "common/data/sound/"+folded(name);
}
uint32_t channelMask(const std::vector<TextNode>& properties,uint32_t fallback) {
    const auto* node=contentProperty(properties,"Sample-channel-range");
    if(!node)return fallback;
    std::vector<int> range;
    for(const auto& word:node->values){char* end=nullptr;const long value=std::strtol(word.c_str(),&end,10);if(end!=word.c_str())range.push_back(int(value));}
    if(range.size()<2)return fallback;
    uint32_t mask=0;
    for(int channel=std::max(1,range[0]);channel<=std::min(16,range[1]);++channel)mask|=1u<<(channel-1);
    return mask;
}
bool weaponActor(const ObjectPlacement& object,const ActorState& actor){
    const auto subtype=objectSubtype(object);
    return !actor.weapons.empty()&&subtype!="ammo"&&subtype!="fx"&&subtype!="item"&&subtype!="lift"&&subtype!="gate"&&subtype!="thing";
}
NativeAudioPosition audioPosition(const SceneContent& scene,const GameWorld& world,size_t id){
    const auto& object=scene.objects[id];const auto& actor=world.actors()[id];
    return {object.position.x,object.position.y,actor.projectileAltitude.value_or(propertyNumber(object.properties,"Altitude"))};
}
}
AudioClipPtr GameAudio::preload(const std::string& path,const ContentReader& read) {
    if(path.empty())return {};
    const auto key=normalized(path);auto found=clips_.find(key);if(found!=clips_.end())return found->second;
    std::vector<uint8_t> bytes;Wave wave;std::string error;AudioClipPtr result;
    if(read(path,bytes)&&loadWav(bytes,wave))result=makeAudioClip(std::move(wave),error);
    if(!result)diagnostics_.push_back("Unavailable audio: "+path);
    clips_[key]=result;return result;
}
bool GameAudio::load(const SceneContent& scene,const ContentReader& read,const std::string& language,std::string& error,const NativeTileBodyCatalog* terrain) {
    reset();diagnostics_.clear();weapons_.clear();language_=folded(language)=="french"?"FRENCH":"ENGLISH";
    std::set<std::string> neededClips,neededBanks;
    auto keepClip=[&](const std::string& path,const ContentReader& reader){
        if(!path.empty())neededClips.insert(normalized(path));
        return preload(path,reader);
    };
    for(const auto* name:{"pickup-heavy.wav","weapon-switch.wav","vz-motor.wav","start-engine.wav","aaaa.wav",
                         "shield-alarm.wav","money.wav","select.wav","bip.wav","techno.wav","darken.wav","hardcore.wav","electric.wav"})keepClip(soundPath(name),read);
    for(const auto* name:{"V-CODE.WAV","V-KEY.WAV","V-BONUS.WAV","V-MALUS.WAV","V-GAME-OVER.WAV","V-GAME-SAVED.WAV",
            "V-PAUSE.WAV","V-MENU.WAV","V-MAP.WAV","V-INVENTORY.WAV","V-NEED-CASH.WAV","V-NEED-CODE.WAV","V-NEED-KEY.WAV",
            "V-LOCKED.WAV","V-GUN.WAV","V-FLAME.WAV","V-GRENADE.WAV","V-HOMING.WAV","V-LAZER.WAV","V-MINE.WAV",
            "V-PULSAR.WAV","V-ROCKETS.WAV","V-SAW.WAV","V-YEEPEE.WAV","V-SHIELD-LOW.WAV",
            "V-WARNING.WAV","V-WATCH-OUT.WAV","V-TAKE-CARE.WAV","V-LAUGH-1.WAV","V-YEAH.WAV",
            "V-HEY.WAV","V-YO.WAV","V-GIVE-ME.WAV","V-THE-BEST.WAV","V-WANT-MORE.WAV",
            "V-WELL-DONE.WAV","V-COME-ON.WAV","V-ARIBA.WAV",
            "V-WAKE-UP.WAV","V-WAITING-FOR.WAV","V-WHAT-DOING.WAV","V-FORGET-MISSION.WAV",
            "V-OOH.WAV","V-LOOSER.WAV","V-SAD-MAN.WAV"})keepClip("COMMON/DATA/SOUND/"+language_+"/"+name,read);
    std::set<std::string> paths;
    for(const auto& cls:scene.classes) {
        if(const auto* weapon=attribute(cls.properties,"weapon")) {
            WeaponSound sound;
            const auto start=soundPath(propertyText(weapon->children,"Fire-start-sound"));
            const auto fire=soundPath(propertyText(weapon->children,"Fire-sound"));
            const auto end=soundPath(propertyText(weapon->children,"Fire-end-sound"));
            const auto dry=soundPath(propertyText(weapon->children,"No-ammo-sound"));
            sound.loop=propertyBool(weapon->children,"Fire-sound-loop");
            sound.mask=channelMask(cls.properties,0xff00);
            sound.priority=int(propertyNumber(cls.properties,"Sample-channel-priority",129));
            sound.start=keepClip(start,read);sound.sound=keepClip(fire,read);
            sound.end=keepClip(end,read);sound.dry=keepClip(dry,read);
            weapons_[folded(cls.name)]=std::move(sound);
        }
        if(!cls.sprite.path.empty())paths.insert(cls.sprite.path);
        if(!cls.shadow.path.empty())paths.insert(cls.shadow.path);
    }
    for(const auto& object:scene.objects) {
        if(!object.sprite.path.empty())paths.insert(object.sprite.path);
        if(!object.shadow.path.empty())paths.insert(object.shadow.path);
    }
    //Kind4 scenery has no SceneContent actor, but its ANM uses the same
    //embedded sample banks. Cache once while loading; no playback-time I/O.
    if(terrain)visitNativeTileBodyPlacements(*terrain,[&](const auto& placement){
        const auto& path=terrain->sources[placement.source].resolvedResource;
        if(!path.empty())paths.insert(path);
        return true;
    });
    for(const auto& path:paths) {
        const auto key=normalized(path);neededBanks.insert(key);if(banks_.contains(key))continue;
        std::vector<uint8_t> bytes;std::vector<Wave> waves;std::vector<AudioClipPtr> clips;
        if(!read(path,bytes)||!decodeGafSamples(bytes,waves,error))diagnostics_.push_back("Unavailable embedded audio: "+path+" "+error);
        else for(auto& wave:waves) clips.push_back(makeAudioClip(std::move(wave),error));
        banks_[key]=std::move(clips);
    }
    if(scene.player){
        const std::string path="COMMON/DATA/PANEL/PANEL.GAF",key=normalized(path);
        neededBanks.insert(key);
        if(!banks_.contains(key)){
            std::vector<uint8_t> bytes;std::vector<Wave> waves;GafAnimation animation;
            if(read(path,bytes)&&decodeGafSamples(bytes,waves,error)&&decodeGafAnimation(bytes,animation,GafDecodeMode::Metadata)&&!animation.sequences.empty()){
                std::vector<AudioClipPtr> samples;
                for(auto& wave:waves)samples.push_back(makeAudioClip(std::move(wave),error));
                banks_[key]=std::move(samples);shieldSequence_=animation.sequences.front();
            }else diagnostics_.push_back("Unavailable HUD audio: "+path);
        }
    }
    // Preserve shared samples needed by this map, release earlier-map and
    // previous-language PCM. Re-loading the same map still needs no I/O.
    std::erase_if(clips_,[&](const auto& entry){return !neededClips.contains(entry.first);});
    std::erase_if(banks_,[&](const auto& entry){return !neededBanks.contains(entry.first);});
    if(!scene.player)shieldSequence_={};
    fullShieldAlarm_.reset(shieldSequence_);
    error.clear();return true;
}
void GameAudio::reset() {
    music_.resetOverlay();surpriseMusic_=0;device_.stopEffects();firing_.clear();
    weaponActors_.clear();weaponActorsInitialized_=false;spatialSamples_.clear();
    lastSoundWorldTime_=-1;motorBlocked_=false;shieldAlarm_={};fullShieldAlarm_.reset(shieldSequence_);speech_.clear();
}
AudioClipPtr GameAudio::clip(const std::string& name) const {
    const auto found=clips_.find(normalized(name));return found==clips_.end()?AudioClipPtr{}:found->second;
}
uint64_t GameAudio::raw(const std::string& name,uint32_t mask,int priority,bool loop,bool pause,AudioBus bus){
    return device_.playNative(clip(name),mask,priority,65535,0,loop,pause,bus);
}
void GameAudio::explicitSample(const std::string& name,unsigned channel){
    const uint32_t mask=1u<<(channel-1);device_.stopNativeChannels(mask);raw(soundPath(name),mask,191);
}
void GameAudio::briefingBip(){raw(soundPath("bip.wav"),0x40000000u,127,false,false);}
void GameAudio::cue(const std::string& filename,bool voice) {
    if(voice){speech(filename);return;}
    raw(soundPath(filename),0xffff,191,false,false);
}
void GameAudio::speech(const std::string& filename,unsigned delay,unsigned hold,unsigned tail) {
    const auto path=normalized("COMMON/DATA/SOUND/"+language_+"/"+filename);
    if(clip(path))speech_.enqueue(path,SDL_GetTicks64(),delay,hold,tail);
}
void GameAudio::updateSpeech() {
    for(const auto& path:speech_.advance(SDL_GetTicks64())) {
        if(!clip(path))continue;
        device_.stopNativeChannels(1u<<4);
        raw(path,1u<<4,191,false,false,AudioBus::Voices);
    }
}
void GameAudio::consumeWeapons(const GameWorld& world,const SceneContent& scene){
    if(!weaponActorsInitialized_){
        for(size_t i=0;i<world.actors().size()&&i<scene.objects.size();++i)
            if(weaponActor(scene.objects[i],world.actors()[i]))weaponActors_.push_back(i);
        weaponActorsInitialized_=true;
    }
    // Only the small weapon-owner set is visited/sorted, never ordinary Ammo or
    // independent FX. Awake slot order is the original43df70 traversal order.
    std::sort(weaponActors_.begin(),weaponActors_.end(),[&](size_t a,size_t b){return world.actors()[a].nativeAwakeIndex<world.actors()[b].nativeAwakeIndex;});
    for(const auto id:weaponActors_){
        if(id>=world.actors().size()||id>=scene.objects.size())continue;
        const auto& actor=world.actors()[id];
        if(!actor.alive||!actor.nativeAwake||actor.weapons.empty()||actor.selectedWeapon>=actor.weapons.size())continue;
        const auto& weapon=actor.weapons[actor.selectedWeapon];
        const auto found=weapons_.find(folded(weapon.name));if(found==weapons_.end())continue;
        const auto& sound=found->second;const bool player=id==world.playerIndex();
        // No authored weapon-instance assigned channel occurs in the shipped
        // 25-map resources: their class masks are the observed enemy route.
        const auto route=nativeWeaponAudioRoute(player,0,sound.mask,sound.priority);
        const auto stock=weaponStock(actor,weapon);
        const bool invisible=scene.objects[id].flags.invisible.value_or(false)&&!( !player&&actor.enemyRevealed );
        const bool held=nativeWeaponAudioHeld(actor.nativeFireRequested,player,invisible,
            world.surprise().active(SurpriseKind::Motor),false)&&!(player&&world.surprise().jammed);
        NativeWeaponAudioInput in;
        in.startAvailable=bool(sound.start);in.fireAvailable=bool(sound.sound);
        in.endAvailable=bool(sound.end);in.dryAvailable=bool(sound.dry);in.fireLoop=sound.loop;in.player=player;
        in.ammoPointerPresent=stock!=actor.ammo.end();in.ammoCount=in.ammoPointerPresent?stock->second.count:0;
        in.held=held;in.channelActive=device_.nativeChannelPlaying(route.channel);in.maskIsAll=route.mask==0xffff;
        const auto action=advanceNativeWeaponAudio(firing_[id],in);
        if(action.stopMask)device_.stopNativeChannels(route.mask);
        if(action.stopChannel)device_.stopNativeChannels(route.channelMask());
        if(action.playStart)device_.playNative(sound.start,route.mask,route.priority);
        if(action.playFire)device_.playNative(sound.sound,route.mask,route.priority,65535,0,action.loop);
        if(action.playEnd)device_.playNative(sound.end,route.mask,route.priority);
        if(action.playDry)device_.playNative(sound.dry,route.mask,route.priority);
    }
}
void GameAudio::consume(GameWorld& world,const SceneContent& scene,const std::vector<SceneAudioCue>& cues) {
    const auto events=world.takeAudioEvents();const auto input=world.takeAudioInput();
    if(!device_.available()||world.actors().empty()||world.playerIndex()>=scene.objects.size())return;
    if(input)if(const auto line=commentary_.advance(SDL_GetTicks(),*input))speech(line->name,line->delayMs,line->holdMs,line->tailMs);
    const bool stepped=lastSoundWorldTime_!=world.elapsedMilliseconds();
    const double delta=lastSoundWorldTime_<0?0:world.elapsedMilliseconds()-lastSoundWorldTime_;
    if(stepped){
        lastSoundWorldTime_=world.elapsedMilliseconds();
        // Original map entry changes HUD detail5 to1 until map exit.
        if(shieldAlarm_.advance(int32_t(delta),world.mapOpen()?1:5,world.player().vitals.shield,world.player().vitals.shieldMax)){
            device_.stopNativeChannels(4);raw(soundPath("shield-alarm.wav"),4,127);
        }
        const bool blocked=world.surprise().active(SurpriseKind::Motor);
        if(blocked&&!motorBlocked_)device_.stopNativeChannels(8);
        motorBlocked_=blocked;
        if(world.player().alive){
            if(blocked){if(world.audioMotorAttempt()&&!device_.nativeChannelPlaying(4))raw(soundPath("start-engine.wav"),8,191);}
            else {
                if(!device_.nativeChannelPlaying(4))raw(soundPath("vz-motor.wav"),8,191,true);
                // Native gain follows signed Thrust, not speed or position delta.
                const auto* thrust=contentProperty(scene.objects[world.playerIndex()].properties,"Thrust");
                const float maximum=thrust?propertyNumber(thrust->children,"Max"):0;
                const int volume=nativeEngineVolume(65535,world.playerNativeThrust(),maximum);
                device_.updateNativeChannel(4,unsigned(std::clamp(volume,0,65535)));
            }
        }else device_.stopNativeChannels(8);
    }
    const unsigned mode=world.surprise().active(SurpriseKind::Darkness)?2:world.surprise().active(SurpriseKind::Xtc)?3:0;
    if(mode!=surpriseMusic_){surpriseMusic_=mode;music_.setOverlay(mode?clip(soundPath(mode==2?"darken.wav":"hardcore.wav")):AudioClipPtr{});}
    for(const auto& event:events)switch(event.kind){
    case AudioEventKind::WeaponFire:{
        //444ca0 clears stage flags on accepted start-only weapon firing.
        const auto sound=weapons_.find(folded(event.name));
        if(sound!=weapons_.end()&&sound->second.start&&!sound->second.sound)firing_[event.actor]={};
        break;
    }
    case AudioEventKind::WeaponStop:case AudioEventKind::DryFire:break; //43df70 owns held audio.
    case AudioEventKind::WeaponSwitch:firing_[event.actor]={};if(event.actor==world.playerIndex())explicitSample("weapon-switch.wav",2);break;
    case AudioEventKind::MapCash:explicitSample("money.wav",3);break;
    case AudioEventKind::Voice:speech(event.name,event.delayMs,event.holdMs,event.tailMs);break;
    case AudioEventKind::Bonus:speech("V-BONUS.WAV",0,2000);break;
    case AudioEventKind::Malus:cue("V-MALUS.WAV",true);break;
    case AudioEventKind::Pickup:
        if(event.actor<scene.objects.size()){
            explicitSample("pickup-heavy.wav",3);
            const auto& object=scene.objects[event.actor];
            if(const auto* key=attribute(object.properties,"key"))cue(propertyBool(key->children,"Is-code")?"V-CODE.WAV":"V-KEY.WAV",true);
            else if(event.actor<world.actors().size()&&world.actors()[event.actor].pickupEffect.money>0)explicitSample("money.wav",3);
        }break;
    case AudioEventKind::Death:
        firing_.erase(event.actor);
        if(event.actor<world.actors().size()&&!world.actors()[event.actor].alive)std::erase(weaponActors_,event.actor);
        if(event.actor==world.playerIndex()){device_.stopNativeChannels(8);explicitSample("aaaa.wav",2);cue("V-GAME-OVER.WAV",true);}
        else if(event.actor<world.actors().size()&&world.actors()[event.actor].finalObject&&world.endingRemainingMilliseconds()>0)speech("V-YEEPEE.WAV",0,1000,10000);
        break;
    case AudioEventKind::Spawn:
        if(weaponActorsInitialized_&&event.actor<world.actors().size()&&event.actor<scene.objects.size()&&weaponActor(scene.objects[event.actor],world.actors()[event.actor])&&std::find(weaponActors_.begin(),weaponActors_.end(),event.actor)==weaponActors_.end())weaponActors_.push_back(event.actor);
        break;
    }
    if(stepped)consumeWeapons(world,scene);
    // Retire stolen/completed ownership even if its ANM died or stopped.
    // This bounds tracking by the16 actual native channels.
    std::erase_if(spatialSamples_,[&](const auto& entry){return !device_.isPlaying(entry.second.handle);});
    const auto listener=audioPosition(scene,world,world.playerIndex());
    for(const auto& cue:cues){
        if(cue.kind==SceneAudioCueKind::Forget){spatialSamples_.erase(cue.animation);continue;}
        if(cue.kind==SceneAudioCueKind::Held){
            const auto tracked=spatialSamples_.find(cue.animation);if(tracked==spatialSamples_.end())continue;
            const auto& owned=tracked->second;const auto object=owned.object;const auto handle=owned.handle;
            if(!device_.isPlaying(handle)||(!owned.source&&(object>=scene.objects.size()||object>=world.actors().size()))){spatialSamples_.erase(tracked);continue;}
            const auto source=owned.source?*owned.source:audioPosition(scene,world,object);const int volume=nativeSpatialVolume({},source,listener);
            if(volume<=0){device_.stop(handle);spatialSamples_.erase(tracked);}
            else device_.updateNative(handle,unsigned(volume),nativeSpatialPan(source[0]-listener[0],listenerHalfWidth_));
            continue;
        }
        if(!cue.sample||(!cue.source&&(cue.object>=scene.objects.size()||cue.object>=world.actors().size())))continue;
        const auto bank=banks_.find(normalized(cue.bankPath));if(bank==banks_.end()||cue.sample>bank->second.size())continue;
        const auto source=cue.source?*cue.source:audioPosition(scene,world,cue.object);
        const int volume=nativeSpatialVolume({},source,listener);if(volume<=0)continue;
        //417aa0 initializes kind4's sample mask and priority directly, without
        //class properties. Ordinary actor cues keep their existing overrides.
        const bool terrain=cue.source.has_value();
        const auto mask=terrain?0xffffu:channelMask(scene.objects[cue.object].properties,0xffff);
        const int priority=terrain?63:int(propertyNumber(scene.objects[cue.object].properties,"Sample-channel-priority",127));
        const auto handle=device_.playNative(bank->second[cue.sample-1],mask,priority,unsigned(volume),nativeSpatialPan(source[0]-listener[0],listenerHalfWidth_));
        if(handle&&mask&&cue.animation)spatialSamples_[cue.animation]={cue.object,handle,cue.source};
    }
    if(stepped){
        const auto samples=fullShieldAlarm_.advance(delta,world.mapOpen()?1:5,world.player().vitals.shield,world.player().vitals.shieldMax);
        const auto panel=banks_.find("common/data/panel/panel.gaf");
        if(panel!=banks_.end())for(const auto sample:samples)if(sample&&sample<=panel->second.size())
            device_.playNative(panel->second[sample-1],0xffff,127);
    }

}
void GameAudio::musicTrack(unsigned track){
    static const char* tracks[]={"techno.wav","darken.wav","hardcore.wav"};
    if(track>=1&&track<=3)music_.loadSampleFallback(clip(soundPath(tracks[track-1])));
}
bool GameAudio::musicFile(const std::string& path,std::string& error){return music_.musicFile(path,error);}
bool GameAudio::musicDirectory(const std::string& path,std::string& error){return music_.musicDirectory(path,error);}
void GameAudio::updateMusic(){music_.update();for(auto& message:music_.takeDiagnostics())diagnostics_.push_back(std::move(message));}
}
