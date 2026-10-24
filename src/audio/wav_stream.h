#pragma once
#include <atomic>
#include "assets/wav.h"
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>
#include <mutex>

namespace vyruz {
// Single producer (disk worker), single consumer (audio callback). No I/O,
// allocation, mutex or worker join on the callback. Absolute frame positions
// preserve interpolation across both ring wrap and a looping track boundary.
class WavStream {
public:
    static constexpr uint64_t BufferFrames=65536;
    static std::shared_ptr<WavStream> open(const std::string&,std::string& error);
    // Indexed PCM chunks in an AVI, read as one continuous audio stream.
    static std::shared_ptr<WavStream> openPcm(const std::string&,unsigned rate,unsigned channels,unsigned bits,
        const std::vector<PcmChunk>&,std::string& error);
    ~WavStream();
    WavStream(const WavStream&)=delete;
    WavStream& operator=(const WavStream&)=delete;
    bool frame(uint64_t absolute,float (&stereo)[2]) const;
    void consume(uint64_t absolute){
        const auto produced=produced_.load(std::memory_order_acquire);
        consumed_.store(absolute<produced?absolute:produced,std::memory_order_release);
    }
    bool failed()const{return failed_.load(std::memory_order_acquire);}
    uint64_t frames()const{return frames_;}
    unsigned rate()const{return rate_;}
    unsigned channels()const{return channels_;}
    unsigned bits()const{return bits_;}
    size_t bufferBytes()const{return buffer_.size();}
private:
    WavStream()=default;
    bool fill();
    bool start();
    std::ifstream input_;
    uint64_t data_=0,frames_=0;
    unsigned rate_=0,channels_=0,bits_=0,align_=0;
    std::vector<uint8_t> buffer_;
    std::vector<PcmChunk> chunks_;
    std::vector<uint64_t> starts_;
    std::atomic<uint64_t> produced_{0},consumed_{0};
    std::atomic<bool> stop_{false},failed_{false};
    std::thread worker_;
    // Worker/shutdown only. The callback never touches this mutex or signals.
    std::mutex wakeMutex_;
    std::condition_variable wake_;
};
}
