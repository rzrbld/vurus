#include "assets/gaf_audio.h"
#include "assets/lzss.h"
#include <array>
#include <cstring>
#include <span>

namespace vyruz {
namespace {
using Bytes=std::span<const uint8_t>;
uint32_t u32(Bytes b,size_t p) {
    return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);
}
bool child(Bytes parent,const char* name,Bytes& out) {
    if(parent.size()<16 || u32(parent,4)!=parent.size()-16)return false;
    size_t p=16;const auto count=u32(parent,12);
    if(count>(parent.size()-16)/16)return false;
    for(uint32_t i=0;i<count;++i) {
        if(parent.size()-p<16)return false;
        const size_t bytes=u32(parent,p+4);
        if(bytes>parent.size()-p-16)return false;
        if(std::memcmp(parent.data()+p,name,4)==0) {
            if(!out.empty())return false;
            out=parent.subspan(p,bytes+16);
        }
        p+=bytes+16;
    }
    return p==parent.size();
}
bool streams(Bytes b,std::array<std::vector<uint8_t>,4>& out) {
    if(b.size()<96)return false;
    size_t pos=96,total=0;
    for(size_t i=0;i<4;++i) {
        const auto compression=u32(b,i*24),unpacked=u32(b,i*24+16);
        const size_t stored=compression?u32(b,i*24+12):unpacked;
        if(compression!=0 && compression!=0x100)return false;
        if(unpacked>64*1024*1024-total || stored>b.size()-pos)return false;
        total+=unpacked;
        const size_t aligned=(stored+3)&~size_t(3);
        if(aligned>b.size()-pos)return false;
        if(compression)out[i]=lzssDecompress(b.data()+pos,stored,unpacked);
        else out[i].assign(b.begin()+pos,b.begin()+pos+stored);
        if(out[i].size()!=unpacked)return false;
        pos+=aligned;
    }
    return pos==b.size();
}
}
bool decodeGafSamples(const std::vector<uint8_t>& data,std::vector<Wave>& out,std::string& error) {
    auto fail=[&](const char* why){error=why;return false;};
    Bytes head,body,hs,ds;
    if(!child(data,"HEAD",head)||!child(data,"DATA",body)||head.empty()||body.empty()||
       !child(head,"SMP_",hs)||!child(body,"SMP_",ds))return fail("invalid GAF sample containers");
    if(hs.empty()&&ds.empty()){out.clear();error.clear();return true;}
    if(hs.size()<20||ds.size()<16||u32(hs,12)||u32(ds,12))return fail("incomplete GAF sample bank");
    const size_t count=u32(hs,16);
    if(count>65536)return fail("too many GAF samples");
    std::array<std::vector<uint8_t>,4> components;
    if(!streams(ds.subspan(16),components)||components[0].size()!=count*76)return fail("invalid GAF sample streams");
    Bytes pcm=components[1];size_t pos=0;std::vector<Wave> decoded;decoded.reserve(count);
    for(size_t i=0;i<count;++i) {
        if(pcm.size()-pos<36||std::memcmp(pcm.data()+pos,"SND_",4)||u32(pcm,pos+12)!=24)
            return fail("invalid embedded SND header");
        const size_t stored=u32(pcm,pos+4);const auto rate=u32(pcm,pos+20),bits=u32(pcm,pos+24),channels=u32(pcm,pos+28);
        const uint64_t frames=u32(pcm,pos+32);
        if(!rate||rate>384000||(bits!=8&&bits!=16)||(channels!=1&&channels!=2))return fail("unsupported embedded PCM format");
        const uint64_t bytes=frames*channels*(bits/8);
        if(!bytes||bytes>stored||stored-bytes>3||stored>pcm.size()-pos-36||(stored&3))return fail("invalid embedded PCM length");
        Wave wave;wave.sampleRate=rate;wave.channels=channels;wave.bitsPerSample=bits;
        wave.pcm.assign(pcm.begin()+pos+36,pcm.begin()+pos+36+static_cast<size_t>(bytes));
        decoded.push_back(std::move(wave));pos+=36+stored;
    }
    if(pos!=pcm.size())return fail("trailing embedded PCM data");
    out=std::move(decoded);error.clear();return true;
}
}
