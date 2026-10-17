#include "assets/intro_video.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <stdexcept>

extern "C" int vyruz_indeo_decode(const uint8_t*,size_t,uint32_t,uint32_t,uint8_t*,size_t,char*,size_t);
namespace vyruz {
namespace {
uint32_t le32(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
uint16_t le16(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool tag(const uint8_t* p,const char* name){return std::memcmp(p,name,4)==0;}
struct Chunk {size_t data,size;const uint8_t* id;};
void chunks(const std::vector<uint8_t>& b,size_t begin,size_t end,const std::function<void(Chunk)>& visit){
    for(size_t at=begin;at<end;){
        require(end-at>=8,"Truncated AVI chunk header");const size_t size=le32(b.data()+at+4);
        require(size<=end-at-8,"AVI chunk exceeds container");visit({at+8,size,b.data()+at});
        at+=8+size;
        if(size&1){require(at<end,"Missing AVI alignment byte");++at;}
    }
}
}
bool IntroVideo::load(const std::vector<uint8_t>& bytes,std::string& error){
    *this={};error.clear();
    try {
        require(bytes.size()>=12&&bytes.size()<=256*1024*1024,"Invalid AVI size");
        require(tag(bytes.data(),"RIFF")&&tag(bytes.data()+8,"AVI "),"Expected RIFF AVI movie");
        const size_t end=size_t(le32(bytes.data()+4))+8;
        require(end>=12&&end<=bytes.size(),"Truncated AVI RIFF");
        int videoStream=-1,audioStream=-1,stream=0;uint32_t declaredFrames=0;
        std::vector<Chunk> movi;
        chunks(bytes,12,end,[&](Chunk top){
            if(!tag(top.id,"LIST"))return;
            require(top.size>=4,"Invalid AVI LIST");const auto* list=bytes.data()+top.data;
            if(tag(list,"movi")){movi.push_back(top);return;}
            if(!tag(list,"hdrl"))return;
            chunks(bytes,top.data+4,top.data+top.size,[&](Chunk item){
                if(!tag(item.id,"LIST"))return;
                require(item.size>=4,"Invalid AVI header list");if(!tag(bytes.data()+item.data,"strl"))return;
                require(stream<100,"Too many AVI streams");
                Chunk header{},format{};
                chunks(bytes,item.data+4,item.data+item.size,[&](Chunk c){if(tag(c.id,"strh"))header=c;if(tag(c.id,"strf"))format=c;});
                require(header.size>=48,"Missing AVI stream header");const auto* h=bytes.data()+header.data;
                if(tag(h,"vids")){
                    require(videoStream<0&&format.size>=40,"Invalid AVI video format");const auto* f=bytes.data()+format.data;
                    require(tag(f+16,"IV50")||tag(f+16,"iv50"),"Intro requires Indeo5 IV50 video");
                    const auto w=le32(f+4),height=le32(f+8);
                    require(w>0&&height>0&&w<=2048&&height<=2048,"Unsupported AVI dimensions");
                    width_=int(w);height_=int(height);const auto scale=le32(h+20),rate=le32(h+24);
                    require(scale>0&&rate>0,"Invalid AVI frame rate");fps_=double(rate)/scale;
                    require(fps_>=1&&fps_<=240,"Unsupported AVI frame rate");declaredFrames=le32(h+32);videoStream=stream;
                }else if(tag(h,"auds")){
                    require(audioStream<0&&format.size>=16,"Invalid AVI audio format");const auto* f=bytes.data()+format.data;
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
            chunks(bytes,begin,finish,[&](Chunk c){
                if(tag(c.id,"LIST")){require(c.size>=4,"Invalid AVI movie list");readMovie(c.data+4,c.data+c.size,depth+1);return;}
                if(c.id[0]<'0'||c.id[0]>'9'||c.id[1]<'0'||c.id[1]>'9')return;
                const int id=(c.id[0]-'0')*10+c.id[1]-'0';
                if(id==videoStream&&c.id[2]=='d'&&(c.id[3]=='b'||c.id[3]=='c')){
                    require(c.size>0&&bytes[c.data]==0x1f,"Intro supports only independent Indeo5 INTRA frames");
                    require(packets_.size()<100000,"Too many AVI video frames");packets_.push_back({c.data,c.size});
                }else if(id==audioStream&&c.id[2]=='w'&&c.id[3]=='b'){
                    require(c.size%(audio_.channels*2)==0,"Partial AVI PCM sample");audio_.pcm.insert(audio_.pcm.end(),bytes.begin()+c.data,bytes.begin()+c.data+c.size);
                }
            });
        };
        for(auto c:movi)readMovie(c.data+4,c.data+c.size,0);
        require(!packets_.empty()&&packets_.size()==declaredFrames,"AVI frame count mismatch");
        bytes_=bytes;return true;
    }catch(const std::exception& e){error=e.what();*this={};return false;}
}
bool IntroVideo::frame(size_t index,Image& image,std::string& error) const {
    error.clear();image={};if(index>=packets_.size()){error="Intro frame index out of range";return false;}
    image.width=width_;image.height=height_;image.rgba.resize(size_t(width_)*height_*4);
    std::array<char,512> detail{};const auto [offset,size]=packets_[index];
    if(!vyruz_indeo_decode(bytes_.data()+offset,size,width_,height_,image.rgba.data(),image.rgba.size(),detail.data(),detail.size())){
        error=detail.data();image={};return false;
    }
    return true;
}
}
