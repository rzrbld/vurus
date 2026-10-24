#include "audio/wav_stream.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace vyruz {
namespace {
uint16_t u16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t u32(const uint8_t* p){return uint32_t(u16(p))|(uint32_t(u16(p+2))<<16);}
void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
}
std::shared_ptr<WavStream> WavStream::open(const std::string& path,std::string& error){
    try{
        auto stream=std::shared_ptr<WavStream>(new WavStream);
        auto& in=stream->input_;in.open(path,std::ios::binary|std::ios::ate);
        require(bool(in),"Music file unavailable");const auto fileSize=in.tellg();
        require(fileSize>=12,"Truncated music WAV");in.seekg(0);
        std::array<uint8_t,16> header{};in.read(reinterpret_cast<char*>(header.data()),12);
        require(in&&std::memcmp(header.data(),"RIFF",4)==0&&std::memcmp(header.data()+8,"WAVE",4)==0,"Expected RIFF WAVE music");
        const uint64_t end=uint64_t(u32(header.data()+4))+8;
        require(end>=12&&end<=uint64_t(fileSize),"Truncated music RIFF");
        bool format=false,data=false;
        for(uint64_t pos=12;pos<end;){
            require(end-pos>=8,"Truncated music chunk");in.seekg(std::streamoff(pos));in.read(reinterpret_cast<char*>(header.data()),8);
            require(bool(in),"Cannot read music chunk");const auto size=u32(header.data()+4);pos+=8;
            require(size<=end-pos,"Music chunk exceeds RIFF");
            if(std::memcmp(header.data(),"fmt ",4)==0){
                require(!format&&size>=16,"Invalid music format");
                in.read(reinterpret_cast<char*>(header.data()),16);require(bool(in),"Truncated music format");
                stream->channels_=u16(header.data()+2);stream->rate_=u32(header.data()+4);stream->bits_=u16(header.data()+14);stream->align_=u16(header.data()+12);
                require(u16(header.data())==1&&(stream->channels_==1||stream->channels_==2)&&
                    (stream->bits_==8||stream->bits_==16)&&stream->rate_>=1000&&stream->rate_<=384000,
                    "Unsupported PCM music format");
                require(stream->align_==stream->channels_*(stream->bits_/8)&&u32(header.data()+8)==stream->rate_*stream->align_,"Invalid music PCM alignment");format=true;
            }else if(std::memcmp(header.data(),"data",4)==0){
                require(!data&&size>0,"Invalid music data chunk");stream->data_=pos;stream->frames_=size;data=true;
            }
            pos+=size+(size&1u);require(pos<=end,"Missing music chunk padding");
        }
        require(format&&data&&stream->frames_%stream->align_==0,"Missing or partial music PCM");
        stream->frames_/=stream->align_;
        stream->chunks_.push_back({stream->data_,stream->frames_*stream->align_});stream->starts_.push_back(0);
        require(stream->start(),"Cannot prime music buffer");
        error.clear();return stream;
    }catch(const std::exception& e){error=std::string(e.what())+": "+path;return {};}
}
std::shared_ptr<WavStream> WavStream::openPcm(const std::string& path,unsigned rate,unsigned channels,unsigned bits,
    const std::vector<PcmChunk>& chunks,std::string& error){
    try{
        auto stream=std::shared_ptr<WavStream>(new WavStream);
        require((channels==1||channels==2)&&(bits==8||bits==16)&&rate>=1000&&rate<=384000,"Unsupported PCM stream format");
        require(!chunks.empty()&&chunks.size()<=100000,"Invalid PCM chunk index");
        stream->input_.open(path,std::ios::binary|std::ios::ate);require(bool(stream->input_),"PCM file unavailable");
        const auto size=stream->input_.tellg();require(size>=0,"Invalid PCM file size");
        stream->rate_=rate;stream->channels_=channels;stream->bits_=bits;stream->align_=channels*(bits/8);
        for(const auto& chunk:chunks){
            require(chunk.offset<=uint64_t(size)&&chunk.bytes<=uint64_t(size)-chunk.offset&&chunk.bytes%stream->align_==0,"Invalid PCM chunk bounds");
            if(!chunk.bytes)continue;
            stream->chunks_.push_back(chunk);stream->starts_.push_back(stream->frames_);stream->frames_+=chunk.bytes/stream->align_;
        }
        require(stream->frames_>0&&stream->start(),"Cannot prime PCM stream");
        error.clear();return stream;
    }catch(const std::exception& e){error=std::string(e.what())+": "+path;return {};}
}
bool WavStream::start(){
    buffer_.resize(BufferFrames*align_);
    if(!fill())return false;
    worker_=std::thread([this]{
        while(!stop_.load(std::memory_order_acquire)){
            if(!fill())break;
            std::unique_lock lock(wakeMutex_);
            wake_.wait_for(lock,std::chrono::milliseconds(10),[this]{return stop_.load(std::memory_order_acquire);});
        }
    });return true;
}
WavStream::~WavStream(){
    {std::lock_guard lock(wakeMutex_);stop_.store(true,std::memory_order_release);}
    wake_.notify_one();if(worker_.joinable())worker_.join();
}
bool WavStream::fill(){
    auto write=produced_.load(std::memory_order_relaxed);
    const auto read=consumed_.load(std::memory_order_acquire);
    while(write-read<BufferFrames&&!stop_.load(std::memory_order_relaxed)){
        const auto slot=write%BufferFrames,source=write%frames_;
        const size_t index=size_t(std::upper_bound(starts_.begin(),starts_.end(),source)-starts_.begin()-1);
        const auto chunkFrame=source-starts_[index];const auto& chunk=chunks_[index];
        const auto count=std::min({BufferFrames-(write-read),BufferFrames-slot,chunk.bytes/align_-chunkFrame});
        input_.seekg(std::streamoff(chunk.offset+chunkFrame*align_));
        input_.read(reinterpret_cast<char*>(buffer_.data()+slot*align_),std::streamsize(count*align_));
        if(!input_){failed_.store(true,std::memory_order_release);return false;}
        write+=count;produced_.store(write,std::memory_order_release);
    }
    return true;
}
bool WavStream::frame(uint64_t absolute,float (&stereo)[2])const{
    if(absolute>=produced_.load(std::memory_order_acquire))return false;
    const auto* frame=buffer_.data()+(absolute%BufferFrames)*align_;
    for(unsigned c=0;c<2;++c){const auto* p=frame+(channels_==1?0:c)*(bits_/8);
        if(bits_==8)stereo[c]=(int(*p)-128)/128.f;
        else{const unsigned v=u16(p);stereo[c]=(v>=32768?int(v)-65536:int(v))/32768.f;}
    }
    return true;
}
}
