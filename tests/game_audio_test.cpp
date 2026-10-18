#include "audio/game_audio.h"
#include "game/world.h"
#include "render/scene.h"
#include <cstdlib>
#include <iostream>
#include <set>
#include <filesystem>
#include <fstream>
using namespace vyruz;
static void check(bool ok){if(!ok){std::cerr<<"Game audio check failed\n";std::exit(1);}}
static std::vector<uint8_t> wave(){
    std::vector<uint8_t> b(46);
    for(auto [p,s]:{std::pair{0,"RIFF"},{8,"WAVE"},{12,"fmt "},{36,"data"}})
        for(int i=0;i<4;++i)b[p+i]=s[i];
    auto n=[&](size_t p,unsigned v){for(int i=0;i<4;++i)b[p+i]=v>>(8*i);};
    n(4,38);n(16,16);b[20]=1;b[22]=1;n(24,48000);n(28,48000);b[32]=1;b[34]=8;n(40,2);b[44]=128;b[45]=192;return b;
}
int main(){
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    GameAudio audio;std::string error;check(audio.device().open(error));
    SceneContent scene;size_t reads=0;std::set<std::string> paths;
    ContentReader reader=[&](const std::string& path,std::vector<uint8_t>& out){++reads;paths.insert(path);out=wave();return true;};
    check(audio.load(scene,reader,"ENGLISH",error));check(error.empty());check(audio.diagnostics().empty());
    check(paths.contains("COMMON/DATA/SOUND/ENGLISH/V-KEY.WAV"));
    const size_t loaded=reads;audio.cue("V-KEY.WAV",true);audio.cue("money.wav");
    GameWorld empty;audio.consume(empty,scene,{});check(reads==loaded);
    audio.reset();check(audio.load(scene,reader,"ENGLISH",error));check(reads==loaded);
    check(audio.load(scene,reader,"FRENCH",error));check(paths.contains("COMMON/DATA/SOUND/FRENCH/V-KEY.WAV"));
    paths.clear();const auto frenchReads=reads;
    check(audio.load(scene,reader,"ENGLISH",error));
    check(reads>frenchReads&&paths.contains("COMMON/DATA/SOUND/ENGLISH/V-KEY.WAV"));
    check(!paths.contains("common/data/sound/money.wav")); // common clips retained
    check(!audio.musicFile("/tmp/vyruz-no-such-track-128731.wav",error));check(!error.empty());
    // Tiny CD fixtures exercise asynchronous queue handoff and repeat, without
    // loading disc-sized PCM or depending on a particular randomized first track.
    const auto musicDir=std::filesystem::temp_directory_path()/"vyruz-music-playlist-test";
    std::filesystem::create_directories(musicDir);
    for(const char* name:{"track02.wav","track03.wav"}){
        const auto bytes=wave();std::ofstream output(musicDir/name,std::ios::binary);
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
    std::filesystem::remove_all(musicDir);
    audio.device().close();audio.cue("V-KEY.WAV",true);audio.consume(empty,scene,{});
    std::cout<<"Game audio cache and device lifecycle passed\n";
}
