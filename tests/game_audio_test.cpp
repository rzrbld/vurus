#include "audio/game_audio.h"
#include "host_temp.h"
#include "game/world.h"
#include "render/scene.h"
#include <cstdlib>
#include <iostream>
#include <set>
#include <filesystem>
#include <fstream>
#include <source_location>
using namespace vyruz;
static void check(bool ok,const std::source_location where=std::source_location::current()){
    if(!ok){std::cerr<<"Game audio check failed at line "<<where.line()<<'\n';std::exit(1);}
}
static std::vector<uint8_t> wave(size_t frames=2){
    std::vector<uint8_t> b(44+frames);
    for(auto [p,s]:{std::pair{0,"RIFF"},{8,"WAVE"},{12,"fmt "},{36,"data"}})
        for(int i=0;i<4;++i)b[p+i]=s[i];
    auto n=[&](size_t p,unsigned v){for(int i=0;i<4;++i)b[p+i]=v>>(8*i);};
    n(4,36+frames);n(16,16);b[20]=1;b[22]=1;n(24,48000);n(28,48000);b[32]=1;b[34]=8;n(40,frames);
    for(size_t i=0;i<frames;++i)b[44+i]=i%2?192:128;
    return b;
}
static TextNode value(std::string key,std::string text){TextNode n;n.kind=TextKind::Assignment;n.key=std::move(key);n.values={std::move(text)};return n;}
static TextNode block(std::string key,std::vector<TextNode> fields,std::string type={}){
    TextNode n;n.kind=TextKind::Block;n.key=std::move(key);n.children=std::move(fields);if(!type.empty())n.arguments={std::move(type)};return n;
}
static void voiceType(){
    GameAudio audio;SceneContent scene;std::string error;
    ContentReader read=[](const std::string&,std::vector<uint8_t>&out){out=wave(48000);return true;};
    check(audio.device().open(error));check(audio.load(scene,read,"ENGLISH",error));
    std::string clipError;auto clip=makeAudioClip(Wave{48000,1,8,std::vector<uint8_t>(48000,144)},clipError);
    audio.device().playMusic(clip);const auto serial=audio.device().musicState().serial;
    auto cue=audio.device().playNative(clip,4,191,65535,0,true,true,AudioBus::Cues);
    check(audio.setVoiceLanguage(read,"FRENCH",error)&&audio.voiceLanguage()=="FRENCH");
    check(audio.device().musicState().serial==serial&&audio.device().isPlaying(cue));
    ContentReader missing=[](const std::string&,std::vector<uint8_t>&){return false;};
    check(!audio.setVoiceLanguage(missing,"ENGLISH",error)&&audio.voiceLanguage()=="FRENCH");
    check(audio.device().musicState().serial==serial&&audio.device().isPlaying(cue));
}
static void transitionClips(){
    GameAudio audio;SceneContent scene;std::string error;size_t reads=0;
    ContentReader reader=[&](const std::string&,std::vector<uint8_t>& out){++reads;out=wave(48000);return true;};
    check(audio.device().open(error));check(audio.load(scene,reader,"ENGLISH",error));
    const auto loaded=reads;
    audio.speech("V-KEY.WAV",5000); // Departure must discard pending unrelated speech.
    audio.beginLiftDeparture();check(audio.device().nativeChannelPlaying(5));
    audio.cue("money.wav");audio.reset();
    check(audio.device().nativeChannelPlaying(5)&&!audio.device().nativeChannelPlaying(1));
    check(audio.load(scene,reader,"ENGLISH",error));
    check(audio.device().nativeChannelPlaying(5)&&reads==loaded);
    audio.endLiftDeparture();audio.reset();check(!audio.device().nativeChannelPlaying(5));
    audio.beginLiftDeparture(); // Native global hold deadline survives queue/reset.
    check(!audio.device().nativeChannelPlaying(5));audio.endLiftDeparture();
    audio.fade();check(audio.device().nativeChannelPlaying(1));
    check(reads==loaded);audio.reset();check(!audio.device().nativeChannelPlaying(1));
}
static void savingClip(GameAudio& audio,const ContentReader& reader,size_t& reads){
    SceneContent scene;scene.mapPath="TEST/PAID_AUDIO.3DG";scene.player=ObjectId{0};
    for(unsigned id=0;id<3;++id){
        ObjectClass cls;cls.id={id};cls.name="SaveAudio"+std::to_string(id);
        cls.behaviours={id==0?"Player":"Fixed"};
        cls.properties={value("Managed","1"),value("Bounding-box-size",id==2?"64":"22"),
            value("Saving-plot",id==1?"1":"0"),value("Saving-zone",id==2?"1":"0"),value("Reward",id==1?"-100":"0")};
        scene.classes.push_back(cls);
        ObjectPlacement object;object.id={id};object.classId={id};object.position={id==0?1100.f:1000.f,1000};
        object.properties=cls.properties;object.behaviours=cls.behaviours;scene.objects.push_back(object);
    }
    Level level;level.mapW=level.mapH=100;GameWorld world;std::string error;
    check(world.load(scene,level,error));auto carry=world.carry();carry.vitals.money=100;world.restore(carry);
    for(int i=0;i<57;++i)world.tick(.1f,{});
    check(world.saveRequest()&&world.saveRequest()->cost==100);
    check(audio.load(scene,reader,"ENGLISH",error));audio.device().setPaused(true);
    const auto loaded=reads;world.takeAudioEvents();audio.consume(world,scene,{});
    const bool channel2=audio.device().nativeChannelPlaying(2),channel4=audio.device().nativeChannelPlaying(4);
    world.finishSave(true);
    audio.consume(world,scene,{});
    check(audio.device().nativeChannelPlaying(3)); // Original explicit MONEY channel.
    check(audio.device().nativeChannelPlaying(2)==channel2&&audio.device().nativeChannelPlaying(4)==channel4);
    check(reads==loaded); // Cached PCM only, no playback-time resource reads.
    audio.device().stopNativeChannels(1u<<2);world.finishSave(true);audio.consume(world,scene,{});
    check(!audio.device().nativeChannelPlaying(3));
    audio.reset();
}
static void weaponClips(GameAudio& audio,const ContentReader& reader,size_t& reads){
    // Keep effect cursors frozen in the real device/mixer; test channel admission
    // deterministically, without sleeps or racing the dummy audio callback.
    const char* fields[]={"Fire-start-sound","Fire-sound","Fire-end-sound","No-ammo-sound"};
    for(unsigned phase=0;phase<4;++phase)for(bool available:{true,false}){
        SceneContent scene;scene.mapPath="TEST/AUDIO_CACHE.3DG";
        ObjectClass player;player.id={0};player.name="Player";player.behaviours={"Player"};
        player.properties={value("Managed","1"),value("Sub-type","Vyruz"),
            block("Energy",{value("Initial","100"),value("Max","100")}),
            block("ADD",{value("Name","CacheWeapon")},"Weapon"),
            block("ADD",{value("Name","Bullet"),value("Count","10"),value("Max","10")},"Ammo")};
        ObjectClass weapon;weapon.id={1};weapon.name="CacheWeapon";
        const auto target=std::string(available?"cache-present-":"cache-missing-")+std::to_string(phase)+".wav";
        std::vector<TextNode> settings={value("Ammo","Bullet"),value("Fire-sound-loop","1"),value(fields[phase],target)};
        if(phase>=2)settings.push_back(value("Fire-sound","cache-fire.wav"));
        weapon.properties={block("TYPE",std::move(settings),"Weapon")};
        ObjectClass ammo;ammo.id={2};ammo.name="Bullet";ammo.properties={block("TYPE",{},"Ammo")};
        scene.classes={player,weapon,ammo};scene.player=ObjectId{0};
        ObjectPlacement object;object.id={0};object.classId={0};object.properties=player.properties;
        object.behaviours=player.behaviours;object.position={200,200};scene.objects.push_back(object);
        Level level;level.mapW=32;level.mapH=32;level.hasTerrainCollision=true;
        GameWorld world;std::string error;check(world.load(scene,level,error));world.setDebugEnemyAI(false);
        check(world.player().nativeAwake&&world.player().weapons.size()==1);
        ContentReader source=[&](const std::string& path,std::vector<uint8_t>& bytes){
            return path.find("cache-missing-")==std::string::npos&&reader(path,bytes);
        };
        check(audio.load(scene,source,available?"ENGLISH":"FRENCH",error));audio.device().setPaused(true);
        const auto loaded=reads;
        auto visit=[&](bool held){
            world.tick(.014f,{});
            auto& actor=const_cast<ActorState&>(world.player());actor.nativeFireRequested=held;
            if(phase==3)for(auto& entry:actor.ammo)entry.second.count=0;
            audio.consume(world,scene,{});
        };
        visit(true);
        if(phase==2){check(audio.device().nativeChannelPlaying(2));visit(false);}
        check(audio.device().nativeChannelPlaying(2)==available);
        check(reads==loaded); // No playback-time resource reads after cache load.
        audio.reset();check(!audio.device().nativeChannelPlaying(2));
        visit(true);
        if(phase==2)visit(false);
        check(audio.device().nativeChannelPlaying(2)==available); // Reset retains usable bindings.
    }
}
int main(){try{
 SDL_setenv("SDL_AUDIODRIVER","dummy",1);voiceType();
    const auto musicDir=hostTempDirectory("vyruz-music-playlist-");
    {
    SDL_setenv("SDL_AUDIODRIVER","dummy",0);
    GameAudio audio;std::string error;
    check(!audio.device().nativeChannelPlaying(0)&&!audio.device().nativeChannelPlaying(17));
    const bool opened=audio.device().open(error);
    if(!opened)std::cerr<<"Audio device open: "<<error<<'\n';
    check(opened);
    check(!audio.device().nativeChannelPlaying(0)&&!audio.device().nativeChannelPlaying(17)&&!audio.device().nativeChannelPlaying(~0u));
    SceneContent scene;size_t reads=0;std::set<std::string> paths;
    ContentReader reader=[&](const std::string& path,std::vector<uint8_t>& out){++reads;paths.insert(path);out=wave();return true;};
    check(audio.load(scene,reader,"ENGLISH",error));check(error.empty());check(audio.diagnostics().empty());
    check(paths.contains("COMMON/DATA/SOUND/ENGLISH/V-KEY.WAV"));
    check(paths.contains("COMMON/DATA/SOUND/ENGLISH/V-HELP.WAV"));
    check(paths.contains("COMMON/DATA/SOUND/ENGLISH/V-HIGH-SPEED.WAV")&&paths.contains("COMMON/DATA/SOUND/ENGLISH/V-LOW-SPEED.WAV"));
    const size_t loaded=reads;audio.cue("V-KEY.WAV",true);audio.cue("money.wav");
    GameWorld empty;audio.consume(empty,scene,{});check(reads==loaded);
    audio.reset();check(audio.load(scene,reader,"ENGLISH",error));check(reads==loaded);
    check(audio.load(scene,reader,"FRENCH",error));check(paths.contains("COMMON/DATA/SOUND/FRENCH/V-KEY.WAV"));
    check(paths.contains("COMMON/DATA/SOUND/FRENCH/V-HELP.WAV"));
    check(paths.contains("COMMON/DATA/SOUND/FRENCH/V-HIGH-SPEED.WAV")&&paths.contains("COMMON/DATA/SOUND/FRENCH/V-LOW-SPEED.WAV"));
    paths.clear();const auto frenchReads=reads;
    check(audio.load(scene,reader,"ENGLISH",error));
    check(reads>frenchReads&&paths.contains("COMMON/DATA/SOUND/ENGLISH/V-KEY.WAV"));
    check(!paths.contains("common/data/sound/money.wav")); // common clips retained
    weaponClips(audio,reader,reads);
    savingClip(audio,reader,reads);
    audio.device().setPaused(false);
    check(audio.load(scene,reader,"ENGLISH",error));
    check(!audio.musicFile("/tmp/vyruz-no-such-track-128731.wav",error));check(!error.empty());
    // 100ms CD fixtures allow asynchronous prefetch before playback ends, even
    // with an immediate hardware callback. Two-sample clips can expire before
    // the first updateMusic visit. Still require four transitions within 1s.
    std::filesystem::create_directories(musicDir);
    for(const char* name:{"track02.wav","track03.wav"}){
        const auto bytes=wave(4800);std::ofstream output(musicDir/name,std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    }
    check(audio.musicDirectory(musicDir.string(),error));
    const auto firstSerial=audio.device().musicState().serial;
    for(int i=0;i<100 && audio.device().musicState().serial<firstSerial+4;++i){audio.updateMusic();SDL_Delay(10);}
    check(audio.device().musicState().serial>=firstSerial+4);
    check(audio.musicFile((musicDir/"track02.wav").string(),error));
    const auto fixedSerial=audio.device().musicState().serial;
    for(int i=0;i<5;++i){audio.updateMusic();SDL_Delay(10);}
    check(audio.device().musicState().serial==fixedSerial);check(audio.device().musicState().playing);
    check(!audio.musicDirectory((musicDir/"missing").string(),error));
    audio.setMusicEnabled(false);audio.updateMusic();audio.reset();
    check(!audio.device().musicState().playing);
    audio.device().close();audio.cue("V-KEY.WAV",true);audio.consume(empty,scene,{});
    check(!audio.device().nativeChannelPlaying(0)&&!audio.device().nativeChannelPlaying(17));
    } // Destroy fixed-source stream and join workers before fixture cleanup.
    transitionClips();
    std::filesystem::remove_all(musicDir);
    std::cout<<"Game audio cache and device lifecycle passed\n";
}catch(const std::exception& error){std::cerr<<"Game audio test: "<<error.what()<<'\n';return 1;}
}
