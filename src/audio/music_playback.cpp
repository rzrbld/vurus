#include "audio/music_playback.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>

namespace vyruz {
namespace {
constexpr float MusicGain=.35f; // Existing accepted port bus gain.
AudioClipPtr readMusic(const std::string& path,std::string& error) {
    auto stream=WavStream::open(path,error);if(!stream)return {};
    auto clip=std::make_shared<AudioClip>();clip->frames=stream->frames();
    clip->wave.sampleRate=stream->rate();clip->wave.channels=stream->channels();clip->wave.bitsPerSample=stream->bits();
    clip->stream=std::move(stream);return clip;
}
}
// One worker, at most one pending job and one completed clip. Replacing a job
// never waits for disk I/O. A stale in-flight result is discarded by the worker,
// without touching the audio device or publishing it into a new session.
struct MusicPlayback::Worker {
    struct Job { uint64_t id;std::string path; };
    struct Result { uint64_t id;AudioClipPtr clip;std::string error; };
    Reader read;
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Job> pending;
    std::optional<Result> result;
    uint64_t generation=0;
    bool stopping=false;
    std::thread thread;
    explicit Worker(Reader reader):read(std::move(reader)),thread([this]{run();}){}
    ~Worker(){
        {std::lock_guard lock(mutex);stopping=true;pending.reset();result.reset();}
        wake.notify_one();thread.join(); // Shutdown only, never an option toggle.
    }
    uint64_t submit(const std::string& path){
        std::lock_guard lock(mutex);const auto id=++generation;
        pending=Job{id,path};result.reset();wake.notify_one();return id;
    }
    void cancel(){std::lock_guard lock(mutex);++generation;pending.reset();result.reset();}
    std::optional<Result> poll(){
        std::lock_guard lock(mutex);auto ready=std::move(result);result.reset();return ready;
    }
    void run(){
        for(;;){
            Job job;
            {std::unique_lock lock(mutex);wake.wait(lock,[&]{return stopping||pending.has_value();});
             if(stopping)return;job=std::move(*pending);pending.reset();}
            Result completed{job.id,{},{}};
            try {completed.clip=read(job.path,completed.error);}
            catch(const std::exception& error){completed.error=error.what();}
            catch(...){completed.error="Unknown music read failure";}
            {std::lock_guard lock(mutex);
             if(!stopping&&job.id==generation)result=std::move(completed);}
        }
    }
};

MusicPlayback::MusicPlayback(AudioDevice& device,Reader reader):device_(device),reader_(reader?std::move(reader):Reader(readMusic)){}
MusicPlayback::~MusicPlayback()=default;
void MusicPlayback::setEnabled(bool enabled){const bool was=this->enabled();enabled_=enabled;admissionChanged(was);}
void MusicPlayback::setOptionEnabled(bool enabled){const bool was=this->enabled();optionEnabled_=enabled;admissionChanged(was);}
void MusicPlayback::admissionChanged(bool was){
    if(was==enabled())return;
    if(!enabled()){
        synchronize();advanceOnRestart_=started_&&source_==Source::Playlist;
        retire();
    } else start();
}
void MusicPlayback::retire(){
    if(worker_)worker_->cancel();request_=Request::None;
    device_.stopMusic();
    // Release retired/queued PCM on the main thread, never in the callback.
    device_.playMusic({});device_.playMusicOverlay({});
    serial_=device_.musicState().serial;
}
void MusicPlayback::loadSampleFallback(AudioClipPtr clip){
    retire();device_.setMusicNativeChannel(true);source_=clip?Source::Sample:Source::None;sample_=std::move(clip);
    paths_.clear();started_=advanceOnRestart_=false;if(enabled())start();
}
bool MusicPlayback::musicFile(const std::string& path,std::string& error){
    auto clip=reader_(path,error);if(!clip)return false;
    retire();device_.setMusicNativeChannel(false);source_=Source::Fixed;sample_=std::move(clip);paths_={path};
    started_=advanceOnRestart_=false;
    if(enabled()){device_.playMusic(sample_,MusicGain,true);started_=true;serial_=device_.musicState().serial;applyOverlay();}
    error.clear();return true;
}
bool MusicPlayback::musicDirectory(const std::string& directory,std::string& error,std::optional<size_t> firstTrackIndex){
    std::vector<std::string> paths;
    for(unsigned track=2;track<=8;++track){
        char name[32];std::snprintf(name,sizeof(name),"track%02u.wav",track);
        auto file=std::filesystem::path(directory)/name;std::error_code ec;
        if(!std::filesystem::is_regular_file(file,ec))break;paths.push_back(file.string());
    }
    if(paths.empty()){error="CD music tracks unavailable: "+directory;return false;}
    size_t initial;
    if(firstTrackIndex){
        if(*firstTrackIndex>=paths.size()){error="Initial CD track index is outside playlist";return false;}
        initial=*firstTrackIndex;
    } else {
        std::mt19937 generator(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
        initial=std::uniform_int_distribution<size_t>(0,paths.size()-1)(generator);
    }
    // Initial setup already runs during loading; later OFF/ON reads are async.
    auto clip=reader_(paths[initial],error);if(!clip)return false;
    retire();device_.setMusicNativeChannel(false);source_=Source::Playlist;paths_=std::move(paths);sample_.reset();index_=initial;
    started_=advanceOnRestart_=false;
    if(enabled()){
        device_.playMusic(std::move(clip),MusicGain,false);started_=true;
        serial_=device_.musicState().serial;applyOverlay();request((index_+1)%paths_.size(),Request::Next);
    }
    error.clear();return true;
}
void MusicPlayback::start(){
    if(source_==Source::Playlist){
        if(advanceOnRestart_)index_=(index_+1)%paths_.size();
        advanceOnRestart_=false;started_=false;request(index_,Request::Start);
    } else if(source_==Source::Fixed && sample_ && sample_->stream){
        started_=false;request(0,Request::Start);
    } else if(sample_){
        device_.playMusic(sample_,MusicGain,true);started_=true;
        serial_=device_.musicState().serial;applyOverlay();
    } else if(overlay_)applyOverlay();
}
void MusicPlayback::request(size_t index,Request kind){
    try {
        if(!worker_)worker_=std::make_unique<Worker>(reader_);
        requestedIndex_=index;request_=kind;requestId_=worker_->submit(paths_[index]);
    } catch(const std::exception& error){
        request_=Request::None;
        diagnostics_.push_back(std::string("Cannot prefetch CD music: ")+error.what());
    }
}
void MusicPlayback::setOverlay(AudioClipPtr clip){
    if(overlay_==clip)return;overlay_=std::move(clip);
    if(enabled())applyOverlay();
}
void MusicPlayback::applyOverlay(){
    if(source_==Source::Playlist||source_==Source::Fixed){
        if(overlay_)device_.playMusicOverlay(overlay_,MusicGain);else device_.stopMusicOverlay();
    } else {
        device_.playMusic(overlay_?overlay_:sample_,MusicGain,true);
        serial_=device_.musicState().serial;
    }
}
void MusicPlayback::synchronize(){
    if(source_!=Source::Playlist||!started_)return;
    const auto state=device_.musicState();
    if(state.serial==serial_)return;
    index_=(index_+size_t(state.serial-serial_))%paths_.size();serial_=state.serial;
    device_.queueMusic({});
    if(enabled())request((index_+1)%paths_.size(),Request::Next);
}
void MusicPlayback::update(){
    if(!enabled())return;synchronize();
    if(!worker_||request_==Request::None)return;
    auto result=worker_->poll();if(!result||result->id!=requestId_)return;
    const auto kind=request_;request_=Request::None;
    if(!result->clip){
        diagnostics_.push_back(result->error.empty()?"Cannot read next CD music track":result->error);
        // Keep the selected source, so a subsequent explicit OFF/ON can retry.
        return;
    }
    if(kind==Request::Start){
        index_=requestedIndex_;
        if(source_==Source::Fixed)sample_=result->clip;
        device_.playMusic(std::move(result->clip),MusicGain,source_==Source::Fixed);
        serial_=device_.musicState().serial;started_=true;applyOverlay();
        if(source_==Source::Playlist)request((index_+1)%paths_.size(),Request::Next);
    } else device_.queueMusic(std::move(result->clip),MusicGain);
}
std::vector<std::string> MusicPlayback::takeDiagnostics(){auto result=std::move(diagnostics_);diagnostics_.clear();return result;}
MusicPlayback::State MusicPlayback::state()const{return{enabled(),source_==Source::Fixed||source_==Source::Playlist,request_!=Request::None,index_,source_==Source::Playlist?paths_.size():0};}
}
