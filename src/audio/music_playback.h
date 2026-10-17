#pragma once
#include "audio/mixer.h"
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace vyruz {
// Main-thread music policy. SDL's existing pause flags remain authoritative;
// Options OFF stops sources rather than muting their advancing cursors.
class MusicPlayback {
public:
    using Reader = std::function<AudioClipPtr(const std::string&,std::string&)>;
    explicit MusicPlayback(AudioDevice&,Reader reader={});
    ~MusicPlayback();
    MusicPlayback(const MusicPlayback&)=delete;
    MusicPlayback& operator=(const MusicPlayback&)=delete;
    void setEnabled(bool); // CLI admission, independent of the saved option.
    void setOptionEnabled(bool);
    bool enabled() const { return enabled_ && optionEnabled_; }
    void loadSampleFallback(AudioClipPtr);
    bool musicFile(const std::string&,std::string& error);
    bool musicDirectory(const std::string&,std::string& error,
                        std::optional<size_t> firstTrackIndex={});
    void setOverlay(AudioClipPtr); // Empty restores the underlying source.
    void resetOverlay() { setOverlay({}); }
    void update();
    std::vector<std::string> takeDiagnostics();
    struct State { bool enabled,cd,loading; size_t index,count; };
    State state() const;
private:
    struct Worker;
    enum class Source { None, Sample, Fixed, Playlist };
    enum class Request { None, Start, Next };
    void admissionChanged(bool wasEnabled);
    void retire();
    void start();
    void applyOverlay();
    void synchronize();
    void request(size_t index,Request);
    AudioDevice& device_;
    Reader reader_;
    std::unique_ptr<Worker> worker_;
    Source source_=Source::None;
    Request request_=Request::None;
    AudioClipPtr sample_,overlay_;
    std::vector<std::string> paths_,diagnostics_;
    size_t index_=0,requestedIndex_=0;
    uint64_t serial_=0,requestId_=0;
    bool enabled_=true,optionEnabled_=true,started_=false,advanceOnRestart_=false;
};
}
