#include "assets/intro_video.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

extern "C" int vyruz_indeo_decode(const uint8_t*,size_t,uint32_t,uint32_t,uint8_t*,size_t,char*,size_t);
namespace vyruz {
namespace {
uint32_t le32(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
uint16_t le16(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool tag(const uint8_t* p,const char* name){return std::memcmp(p,name,4)==0;}
struct Chunk {size_t data=0,size=0;std::array<uint8_t,4> id{};};
using Reader=std::function<std::vector<uint8_t>(size_t,size_t)>;
void chunks(const Reader& read,size_t begin,size_t end,const std::function<void(Chunk)>& visit){
    for(size_t at=begin;at<end;){
        require(end-at>=8,"Truncated AVI chunk header");const auto header=read(at,8);const size_t size=le32(header.data()+4);
        require(size<=end-at-8,"AVI chunk exceeds container");Chunk c{at+8,size,{}};std::copy_n(header.data(),4,c.id.data());visit(c);
        at+=8+size;
        if(size&1){require(at<end,"Missing AVI alignment byte");++at;}
    }
}
}
// Compressed video only: audio has its own handle and bounded streaming ring.
// The budget includes the worker's in-flight read. Decoding stays on the caller.
class IntroReadAhead {
    static constexpr size_t budget=512*1024;
    struct Packet {size_t index;std::vector<uint8_t> bytes;};
    std::ifstream file;
    const std::vector<std::pair<size_t,size_t>> packets;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Packet> queue;
    size_t bytes=0,next=0,inFlight=size_t(-1),generation=0;
    bool stopping=false;
    std::string failure;
    std::thread worker;
    void run(){
        std::unique_lock lock(mutex);
        while(!stopping){
            changed.wait(lock,[&]{return stopping||(!failure.size()&&next<packets.size()&&
                packets[next].second<=budget-bytes);});
            if(stopping)break;
            const size_t index=next++,epoch=generation;
            const auto [offset,size]=packets[index];
            inFlight=index;bytes+=size;
            lock.unlock();
            std::vector<uint8_t> data;
            std::string error;
            try{
                data.resize(size);file.clear();file.seekg(std::streamoff(offset));
                if(!file.read(reinterpret_cast<char*>(data.data()),std::streamsize(size)))
                    error="Truncated AVI read-ahead";
            }catch(const std::exception& e){error=e.what();}
            lock.lock();inFlight=size_t(-1);bytes-=size;
            if(epoch==generation){
                if(error.empty()){bytes+=size;queue.push_back({index,std::move(data)});}
                else failure=std::move(error);
            }
            changed.notify_all();
        }
    }
public:
    IntroReadAhead(const std::string& path,const std::vector<std::pair<size_t,size_t>>& index)
        :file(path,std::ios::binary),packets(index){
        require(bool(file),"Intro AVI unavailable for read-ahead");
        worker=std::thread([this]{run();});
    }
    ~IntroReadAhead(){
        {std::lock_guard lock(mutex);stopping=true;}
        changed.notify_all();if(worker.joinable())worker.join();
    }
    std::vector<uint8_t> get(size_t index){
        std::unique_lock lock(mutex);
        // Drop skipped frames, but keep useful future packets after normal advance.
        while(!queue.empty()&&queue.front().index<index){bytes-=queue.front().bytes.size();queue.pop_front();}
        if((queue.empty()||queue.front().index!=index)&&inFlight!=index){
            for(const auto& packet:queue)bytes-=packet.bytes.size();
            queue.clear();++generation;next=index;failure.clear();
        }
        changed.notify_all();
        changed.wait(lock,[&]{return !failure.empty()||(!queue.empty()&&queue.front().index==index);});
        require(failure.empty(),failure.c_str());
        auto result=std::move(queue.front().bytes);queue.pop_front();bytes-=result.size();
        changed.notify_all();return result;
    }
    size_t bufferedBytes(){std::lock_guard lock(mutex);return bytes;}
    static bool fits(size_t size){return size<=budget;}
};
size_t IntroVideo::videoReadAheadBytes()const{return readAhead_?readAhead_->bufferedBytes():0;}
std::vector<uint8_t> IntroVideo::read(size_t offset,size_t size)const{
    std::vector<uint8_t> result(size);
    if(file_){file_->clear();file_->seekg(std::streamoff(offset));require(bool(file_->read(reinterpret_cast<char*>(result.data()),std::streamsize(size))),"Truncated AVI read");}
    else {require(offset<=bytes_.size()&&size<=bytes_.size()-offset,"Truncated AVI read");std::copy_n(bytes_.data()+offset,size,result.data());}
    return result;
}
bool IntroVideo::loadFile(const std::string& path,std::string& error){
    *this={};error.clear();
    try{
        file_=std::make_shared<std::ifstream>(path,std::ios::binary|std::ios::ate);
        require(bool(*file_),"Intro AVI unavailable");const auto size=file_->tellg();
        require(size>=12&&size<=256*1024*1024,"Invalid AVI size");path_=path;
        if(!parse(size_t(size),[this](size_t p,size_t n){return read(p,n);},false,error))return false;
        readAhead_=std::make_shared<IntroReadAhead>(path_,packets_);
        return true;
    }catch(const std::exception& e){error=e.what();*this={};return false;}
}
bool IntroVideo::load(const std::vector<uint8_t>& bytes,std::string& error){
    *this={};bytes_=bytes;
    return parse(bytes_.size(),[this](size_t p,size_t n){return read(p,n);},true,error);
}
bool IntroVideo::parse(size_t size,const Reader& read,bool residentAudio,std::string& error){
    error.clear();
    try {
        require(size>=12&&size<=256*1024*1024,"Invalid AVI size");const auto riff=read(0,12);
        require(tag(riff.data(),"RIFF")&&tag(riff.data()+8,"AVI "),"Expected RIFF AVI movie");
        const size_t end=size_t(le32(riff.data()+4))+8;
        require(end>=12&&end<=size,"Truncated AVI RIFF");
        int videoStream=-1,audioStream=-1,stream=0;uint32_t declaredFrames=0;
        std::vector<Chunk> movi;
        chunks(read,12,end,[&](Chunk top){
            if(!tag(top.id.data(),"LIST"))return;
            require(top.size>=4,"Invalid AVI LIST");const auto listBytes=read(top.data,4);const auto* list=listBytes.data();
            if(tag(list,"movi")){movi.push_back(top);return;}
            if(!tag(list,"hdrl"))return;
            chunks(read,top.data+4,top.data+top.size,[&](Chunk item){
                if(!tag(item.id.data(),"LIST"))return;
                require(item.size>=4,"Invalid AVI header list");if(!tag(read(item.data,4).data(),"strl"))return;
                require(stream<100,"Too many AVI streams");
                Chunk header{},format{};
                chunks(read,item.data+4,item.data+item.size,[&](Chunk c){if(tag(c.id.data(),"strh"))header=c;if(tag(c.id.data(),"strf"))format=c;});
                require(header.size>=48,"Missing AVI stream header");const auto headerBytes=read(header.data,48);const auto* h=headerBytes.data();
                if(tag(h,"vids")){
                    require(videoStream<0&&format.size>=40,"Invalid AVI video format");const auto formatBytes=read(format.data,40);const auto* f=formatBytes.data();
                    require(tag(f+16,"IV50")||tag(f+16,"iv50"),"Intro requires Indeo5 IV50 video");
                    const auto w=le32(f+4),height=le32(f+8);
                    require(w>0&&height>0&&w<=2048&&height<=2048,"Unsupported AVI dimensions");
                    width_=int(w);height_=int(height);const auto scale=le32(h+20),rate=le32(h+24);
                    require(scale>0&&rate>0,"Invalid AVI frame rate");fps_=double(rate)/scale;
                    require(fps_>=1&&fps_<=240,"Unsupported AVI frame rate");declaredFrames=le32(h+32);videoStream=stream;
                }else if(tag(h,"auds")){
                    require(audioStream<0&&format.size>=16,"Invalid AVI audio format");const auto formatBytes=read(format.data,16);const auto* f=formatBytes.data();
                    audio_.channels=le16(f+2);audio_.sampleRate=le32(f+4);audio_.bitsPerSample=le16(f+14);
                    require(le16(f)==1&&audio_.channels>=1&&audio_.channels<=2&&audio_.bitsPerSample==16&&audio_.sampleRate>=8000&&audio_.sampleRate<=96000,"Intro requires 16-bit PCM audio");
                    require(le16(f+12)==audio_.channels*2&&le32(f+8)==audio_.sampleRate*audio_.channels*2,"Invalid AVI PCM alignment");audioStream=stream;
                }
                ++stream;
            });
        });
        require(videoStream>=0&&!movi.empty(),"AVI lacks video or movie data");
        std::function<void(size_t,size_t,int)> readMovie;
        readMovie=[&](size_t begin,size_t finish,int depth){
            require(depth<=8,"AVI nesting exceeds limit");
            chunks(read,begin,finish,[&](Chunk c){
                if(tag(c.id.data(),"LIST")){require(c.size>=4,"Invalid AVI movie list");readMovie(c.data+4,c.data+c.size,depth+1);return;}
                if(c.id[0]<'0'||c.id[0]>'9'||c.id[1]<'0'||c.id[1]>'9')return;
                const int id=(c.id[0]-'0')*10+c.id[1]-'0';
                if(id==videoStream&&c.id[2]=='d'&&(c.id[3]=='b'||c.id[3]=='c')){
                    require(c.size>0&&c.size<=16*1024*1024&&read(c.data,1)[0]==0x1f,"Intro supports only independent Indeo5 INTRA frames");
                    require(packets_.size()<100000,"Too many AVI video frames");packets_.push_back({c.data,c.size});
                }else if(id==audioStream&&c.id[2]=='w'&&c.id[3]=='b'){
                    require(c.size%(audio_.channels*2)==0,"Partial AVI PCM sample");require(audioChunks_.size()<100000,"Too many AVI audio packets");audioChunks_.push_back({c.data,c.size});
                    if(residentAudio){const auto pcm=read(c.data,c.size);audio_.pcm.insert(audio_.pcm.end(),pcm.begin(),pcm.end());}
                }
            });
        };
        for(auto c:movi)readMovie(c.data+4,c.data+c.size,0);
        require(!packets_.empty()&&packets_.size()==declaredFrames,"AVI frame count mismatch");
        return true;
    }catch(const std::exception& e){error=e.what();*this={};return false;}
}
bool IntroVideo::frame(size_t index,Image& image,std::string& error) const {
    error.clear();image={};if(index>=packets_.size()){error="Intro frame index out of range";return false;}
    try{
    image.width=width_;image.height=height_;image.rgba.resize(size_t(width_)*height_*4);
    std::array<char,512> detail{};const auto [offset,size]=packets_[index];
    const auto packet=readAhead_&&IntroReadAhead::fits(size)?readAhead_->get(index):read(offset,size);
    if(!vyruz_indeo_decode(packet.data(),size,width_,height_,image.rgba.data(),image.rgba.size(),detail.data(),detail.size())){
        error=detail.data();image={};return false;
    }
    return true;
    }catch(const std::exception& e){error=e.what();image={};return false;}
}
}
