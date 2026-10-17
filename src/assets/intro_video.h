#pragma once
#include "assets/tga.h"
#include "assets/wav.h"
#include <cstddef>
#include <string>
#include <utility>

namespace vyruz {
// Bounded AVI reader for the original all-intra IV50 + PCM intro. Missing or
// unsupported movies are optional at the presentation layer, never game data.
class IntroVideo {
public:
    bool load(const std::vector<uint8_t>& bytes, std::string& error);
    bool frame(size_t index, Image& image, std::string& error) const;
    int width() const { return width_; }
    int height() const { return height_; }
    size_t frameCount() const { return packets_.size(); }
    double frameRate() const { return fps_; }
    double duration() const { return fps_>0?frameCount()/fps_:0; }
    double durationMilliseconds() const { return duration()*1000.0; }
    const Wave& audio() const { return audio_; }
private:
    int width_=0,height_=0;
    double fps_=0;
    Wave audio_;
    std::vector<uint8_t> bytes_;
    std::vector<std::pair<size_t,size_t>> packets_;
};
}
