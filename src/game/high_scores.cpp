#include "game/high_scores.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include "platform/files.h"
namespace vyruz {
HighScores defaultHighScores(uint32_t seed) {
    HighScores result;std::mt19937 random(seed);
    const auto between=[&](uint32_t lo,uint32_t hi){return std::uniform_int_distribution<uint32_t>(lo,hi)(random);};
    constexpr const char* names[]={"Graben Djroved","De Froboze","Strangula","Menadal","Jack Kelkin","Baxter","AMAJKADAM","Jesse James","Xigh","Beka"};
    // 401520/401af0. RNG distribution is a host adapter; formulas/names native.
    for(size_t i=0;i<result.size();++i){auto& e=result[i];const uint32_t strength=20-uint32_t(i);
        const auto tower=std::max(1u,strength*3/20);e.values[2]=tower;
        const auto levels=tower==1?5u:tower==2?8u:tower==3?11u:3u;
        e.values[3]=std::max(1u,levels*strength/20);e.values[6]=std::max(1u,strength*60/20);
        e.values[8]=between(0,15);e.values[9]=between(0,59);e.values[10]=between(0,59);
        const auto hours=std::max(1u,e.values[8]);e.values[7]=between(500,1000)*hours;
        e.values[4]=between(45,90)*hours;e.values[5]=strength*hours*1000;
        e.name=i==0?"Guillaume Portes":names[between(0,9)];
    }
    return result;
}
unsigned insertHighScore(HighScores& scores,const HighScoreEntry& entry) {
    size_t at=scores.size();
    while(at>0){bool before=true;for(size_t field=2;field<=7;++field){if(entry.values[field]<scores[at-1].values[field]){before=false;break;}if(entry.values[field]>scores[at-1].values[field])break;}
        if(!before)break;
        --at;
    }
    if(at==scores.size())return 0;
    std::move_backward(scores.begin()+at,scores.end()-1,scores.end());scores[at]=entry;scores[at].name.resize(std::min(size_t(19),entry.name.size()));return unsigned(at+1);
}
bool decodeHighScores(std::span<const uint8_t> bytes,HighScores& out,std::string& error) {
    if(bytes.size()!=1440){error="Invalid original score file size";return false;}
    HighScores result;
    for(size_t i=0;i<20;++i){const auto row=bytes.subspan(i*72,72);for(size_t j=0;j<13;++j)for(unsigned k=0;k<4;++k)result[i].values[j]|=uint32_t(row[j*4+k])<<(8*k);
        const auto name=row.subspan(52,20);const auto end=std::find(name.begin(),name.end(),uint8_t(0));result[i].name.assign(name.begin(),end);}
    out=std::move(result);error.clear();return true;
}
std::vector<uint8_t> encodeHighScores(const HighScores& scores) {
    std::vector<uint8_t> bytes(1440);for(size_t i=0;i<20;++i){for(size_t j=0;j<13;++j)for(unsigned k=0;k<4;++k)bytes[i*72+j*4+k]=uint8_t(scores[i].values[j]>>(8*k));
        std::copy_n(scores[i].name.begin(),std::min(size_t(19),scores[i].name.size()),bytes.begin()+i*72+52);}
    return bytes;
}
bool readHighScores(const std::string& path,HighScores& scores,std::string& error) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file){error="Cannot open score file";return false;}
    if(file.tellg()!=1440){error="Invalid original score file size";return false;}
    file.seekg(0);std::vector<uint8_t> data(1440);if(!file.read(reinterpret_cast<char*>(data.data()),1440)){error="Cannot read score file";return false;}return decodeHighScores(data,scores,error);
}
bool writeHighScores(const std::string& path,const HighScores& scores,std::string& error) {
    const auto bytes=encodeHighScores(scores);return platform::atomicWrite(path,bytes,error);
}
HighScoreEntry makeHighScore(std::string name,uint32_t tower,uint32_t level,uint32_t kills,uint32_t money,uint64_t milliseconds,uint32_t hits,uint32_t misses) {
    HighScoreEntry result;result.name=name.substr(0,19);result.values[2]=tower;result.values[3]=level;result.values[4]=kills;result.values[5]=money;
    result.values[8]=(milliseconds/3600000)%60;result.values[9]=(milliseconds/60000)%60;result.values[10]=(milliseconds/1000)%60;
    // 43e548/43e550 retain these across zero-fill (Ghidra reorders stores).
    const uint64_t total=uint64_t(hits)+misses;result.values[7]=uint32_t(std::min<uint64_t>(total,UINT32_MAX));result.values[6]=total?uint32_t(uint64_t(hits)*100/total):0;
    return result;
}
}
