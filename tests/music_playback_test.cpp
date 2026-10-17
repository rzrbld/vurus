#include "audio/music_playback.h"
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include "host_temp.h"
using namespace vyruz;
static void check(bool okay,const char* message){if(!okay){std::cerr<<message<<'\n';std::exit(1);}}
static AudioClipPtr clip(){
    Wave wave;wave.sampleRate=48000;wave.bitsPerSample=8;wave.channels=1;
    wave.pcm.assign(48000,144);std::string error;
    auto result=makeAudioClip(std::move(wave),error);check(bool(result),"fixture clip");return result;
}
template<class Predicate>static void eventually(MusicPlayback& music,Predicate condition){
    for(int i=0;i<2000&&!condition();++i){music.update();SDL_Delay(1);}
    check(condition(),"async music operation did not finish");
}
int main(){
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);AudioDevice device;std::string error;
    check(device.open(error),"dummy device unavailable");
    const auto directory=std::filesystem::temp_directory_path()/("vyruz-music-policy-"+std::to_string(getpid()));
    std::filesystem::create_directories(directory);
    for(const auto name:{"track02.wav","track03.wav"})std::ofstream(directory/name).put('x');
    const auto sample=clip();
    auto reader=[sample](const std::string& path,std::string& error)->AudioClipPtr{
        if(path.find("missing")!=std::string::npos){error="missing fixture music";return {};}
        error.clear();return sample;
    };
    {
        MusicPlayback music(device,reader);
        check(music.musicDirectory(directory.string(),error,1),"deterministic initial selector");
        check(music.state().index==1&&music.state().count==2,"initial suffix index");
        check(device.musicState().playing,"initial CD source");
        music.setOverlay(sample);SDL_Delay(30);
        check(device.nativeChannelPlaying(1),"CD overlay reserves native WAV channel1");
        check(device.musicState().milliseconds>0,"overlay must advance underlying CD cursor");
        device.setPaused(true);const auto before=device.musicState().milliseconds;SDL_Delay(30);
        check(device.musicState().milliseconds==before,"accepted pause must freeze CD cursor");device.setPaused(false);
        music.setOptionEnabled(false);check(!device.musicState().playing&&!device.musicState().queued,"OFF must stop source and queue");
        const auto stopped=device.musicState().milliseconds;SDL_Delay(30);
        check(device.musicState().milliseconds==stopped,"OFF cursor must not advance silently");
        music.setOptionEnabled(true);eventually(music,[&]{return device.musicState().playing;});
        check(music.state().index==0,"OFF/ON must select next native track, wrapping to02");
        music.resetOverlay();music.setEnabled(false);music.setOptionEnabled(false);music.setOptionEnabled(true);
        check(!device.nativeChannelPlaying(1),"OFF clears overlay reservation as well as CD source");
        check(!music.enabled()&&!device.musicState().playing,"CLI disable must dominate saved option");
        music.setEnabled(true);eventually(music,[&]{return device.musicState().playing;});
        check(music.state().index==1,"CLI re-enable next selector");
        const auto serial=device.musicState().serial;
        check(!music.musicFile("missing",error),"missing fixed file must fail");
        check(device.musicState().serial==serial&&device.musicState().playing,"failed selection must preserve old playback");
        check(!music.musicDirectory(directory.string(),error,2),"reject out-of-range deterministic index");
        check(device.musicState().serial==serial,"invalid selector must be transactional");
        check(music.musicFile("fixed",error),"explicit override");
        music.setOptionEnabled(false);music.setOptionEnabled(true);
        check(device.musicState().playing&&music.state().count==0,"fixed override restarts same looping source");
        music.loadSampleFallback(sample);music.setOverlay(sample);music.resetOverlay();
        check(device.musicState().playing&&!music.state().cd,"WAV fallback survives overlay reset");
        check(device.nativeChannelPlaying(1),"WAV fallback occupies native channel1");
        music.setOptionEnabled(false);
    }
    {
        // OFF while prefetch is in flight must discard its result. Re-enable
        // while paused may load the next selector but cannot advance its clock.
        std::mutex mutex;std::condition_variable condition;bool entered=false,release=false;unsigned reads=0;
        auto slow=[&](const std::string&,std::string&)->AudioClipPtr{
            std::unique_lock lock(mutex);
            if(++reads==2){entered=true;condition.notify_one();condition.wait(lock,[&]{return release;});}
            return sample;
        };
        MusicPlayback music(device,slow);check(music.musicDirectory(directory.string(),error,0),"paused restart fixture");
        music.setOverlay(sample);
        {std::unique_lock lock(mutex);check(condition.wait_for(lock,std::chrono::seconds(2),[&]{return entered;}),"restart prefetch entered");}
        music.setOptionEnabled(false);const auto stoppedSerial=device.musicState().serial;
        {std::lock_guard lock(mutex);release=true;}condition.notify_one();
        for(int i=0;i<30;++i){music.update();SDL_Delay(1);}
        check(!device.musicState().playing&&!device.musicState().queued&&device.musicState().serial==stoppedSerial,"stale completion while OFF cannot enqueue or restart");
        device.setPaused(true);music.setOptionEnabled(true);
        eventually(music,[&]{return device.musicState().playing;});
        check(music.state().index==1&&device.nativeChannelPlaying(1),"restart selects next track and restores desired overlay");
        check(device.musicState().milliseconds==0,"paused async restart starts at unadvanced source cursor");
        music.setOptionEnabled(false);music.setOptionEnabled(true);music.setOptionEnabled(false);music.setOptionEnabled(true);
        eventually(music,[&]{return device.musicState().playing;});
        check(music.state().index==0,"superseding pending restart cannot advance selector twice");
        check(device.musicState().milliseconds==0,"rapid paused toggles preserve zero source position");
        device.setPaused(false);SDL_Delay(30);
        check(device.musicState().milliseconds>0,"unpause advances restored CD source under overlay");
        music.resetOverlay();check(!device.nativeChannelPlaying(1),"reset CD overlay releases native channel1");
        check(device.musicState().playing,"reset CD overlay retains advancing CD source");
        music.setOptionEnabled(false);
    }
    {
        std::mutex mutex;std::condition_variable condition;bool entered=false,release=false;unsigned reads=0;
        auto slow=[&](const std::string&,std::string&)->AudioClipPtr{
            std::unique_lock lock(mutex);
            if(++reads==2){entered=true;condition.notify_one();condition.wait(lock,[&]{return release;});}
            return sample;
        };
        MusicPlayback music(device,slow);check(music.musicDirectory(directory.string(),error,0),"slow-worker fixture");
        {std::unique_lock lock(mutex);check(condition.wait_for(lock,std::chrono::seconds(2),[&]{return entered;}),"prefetch entered");}
        const auto begin=std::chrono::steady_clock::now();
        music.setOptionEnabled(false);music.setOptionEnabled(true);music.loadSampleFallback(sample);
        check(std::chrono::steady_clock::now()-begin<std::chrono::milliseconds(200),"option/source changes must not join a blocked reader");
        const auto fallbackSerial=device.musicState().serial;
        {std::lock_guard lock(mutex);release=true;}condition.notify_one();
        for(int i=0;i<30;++i){music.update();SDL_Delay(1);}
        check(device.musicState().serial==fallbackSerial&&!music.state().cd,"stale worker result must not replace new source");
        music.setOptionEnabled(false);
    }
    {
        unsigned reads=0;std::mutex mutex;
        auto failure=[&](const std::string&,std::string& error)->AudioClipPtr{
            std::lock_guard lock(mutex);if(++reads>1){error="next track removed";return {};}
            return sample;
        };
        MusicPlayback music(device,failure);check(music.musicDirectory(directory.string(),error,0),"failing-prefetch fixture");
        eventually(music,[&]{return !music.state().loading;});
        auto diagnostics=music.takeDiagnostics();check(diagnostics.size()==1&&diagnostics.front()=="next track removed","missing prefetch diagnostic");
        check(music.takeDiagnostics().empty(),"diagnostics drain once");
        check(device.musicState().playing,"failed next track must not stop current track prematurely");
        music.setOptionEnabled(false);
    }
    device.close();std::filesystem::remove_all(directory);
    std::cout<<"Music playlist, native option restart, overlays, pause and nonblocking supersession passed\n";
}
