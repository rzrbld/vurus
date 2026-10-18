#pragma once
#include "assets/tga.h"
#include "assets/wav.h"
#include <cstddef>
#include <string>
#include <utility>
#include <fstream>
#include <functional>
#include <memory>

namespace vyruz {
// Bounded AVI reader for the original all-intra IV50 + PCM intro. Missing or
// unsupported movies are optional at the presentation layer, never game data.
class IntroReadAhead;
class IntroVideo {
public:
    bool loadFile(const std::string& path,std::string& error);
    bool load(const std::vector<uint8_t>& bytes, std::string& error);
    bool frame(size_t index, Image& image, std::string& error) const;
    int width() const { return width_; }
    int height() const { return height_; }
    size_t frameCount() const { return packets_.size(); }
    double frameRate() const { return fps_; }
    double duration() const { return fps_>0?frameCount()/fps_:0; }
    double durationMilliseconds() const { return duration()*1000.0; }
    const Wave& audio() const { return audio_; }
    const std::vector<PcmChunk>& audioChunks()const{return audioChunks_;}
    const std::string& path()const{return path_;}
    size_t indexBytes()const{return packets_.capacity()*sizeof(packets_[0])+audioChunks_.capacity()*sizeof(PcmChunk);}
    size_t videoReadAheadBytes() const;
    size_t retainedMediaBytes()const{return bytes_.capacity()+audio_.pcm.capacity();}
private:
    std::shared_ptr<IntroReadAhead> readAhead_;
    using Reader=std::function<std::vector<uint8_t>(size_t,size_t)>;
    bool parse(size_t size,const Reader&,bool residentAudio,std::string& error);
    std::vector<uint8_t> read(size_t offset,size_t size)const;
    std::shared_ptr<std::ifstream> file_;
    std::string path_;
    std::vector<PcmChunk> audioChunks_;
    int width_=0,height_=0;
    double fps_=0;
    Wave audio_;
    std::vector<uint8_t> bytes_;
    std::vector<std::pair<size_t,size_t>> packets_;
};
}
