#pragma once
#include <cstdint>
#include <fstream>
#include <istream>
#include <optional>
#include <string>

namespace vyruz {
// Linux resident categories, all in KiB. File-backed pages may be shared;
// these are not exclusive game-subsystem ownership or GPU-memory figures.
struct ProcessMemory {
    std::optional<uint64_t> rss, anonymous, file, shmem, pss, privatePages, sharedPages;
};
inline void readMemoryStatus(std::istream& in,ProcessMemory& result) {
    std::string line;
    while(std::getline(in,line)) {
        const auto colon=line.find(':');if(colon==std::string::npos)continue;
        const auto key=line.substr(0,colon);
        std::optional<uint64_t>* field=nullptr;
        if(key=="VmRSS")field=&result.rss;
        else if(key=="RssAnon")field=&result.anonymous;
        else if(key=="RssFile")field=&result.file;
        else if(key=="RssShmem")field=&result.shmem;
        if(!field)continue;
        try{*field=std::stoull(line.substr(colon+1));}catch(...){/* unavailable */}
    }
}
inline void readMemoryRollup(std::istream& in,ProcessMemory& result) {
    std::string key,unit;uint64_t value;
    std::optional<uint64_t> privateClean,privateDirty,sharedClean,sharedDirty;
    std::string line;
    while(std::getline(in,line)) {
        const auto colon=line.find(':');if(colon==std::string::npos)continue;
        key=line.substr(0,colon);
        try{value=std::stoull(line.substr(colon+1));}catch(...){continue;}
        if(key=="Pss")result.pss=value;
        else if(key=="Private_Clean")privateClean=value;
        else if(key=="Private_Dirty")privateDirty=value;
        else if(key=="Shared_Clean")sharedClean=value;
        else if(key=="Shared_Dirty")sharedDirty=value;
    }
    if(privateClean&&privateDirty)result.privatePages=*privateClean+*privateDirty;
    if(sharedClean&&sharedDirty)result.sharedPages=*sharedClean+*sharedDirty;
}
inline ProcessMemory processMemory(bool detailed=true) {
    ProcessMemory result;
#if defined(__linux__)
    std::ifstream status("/proc/self/status");readMemoryStatus(status,result);
    if(detailed){std::ifstream rollup("/proc/self/smaps_rollup");readMemoryRollup(rollup,result);}
#endif
    return result;
}
}
