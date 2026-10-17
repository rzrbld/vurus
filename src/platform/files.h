#pragma once
// Host filesystem operations only; save encoding remains platform independent.
#include <filesystem>
#include <algorithm>
#include <span>
#include <string>
#include <vector>
#include <atomic>
#include <cstdint>
#include <cerrno>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace vyruz::platform {
#ifdef _WIN32
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    explicit operator bool()const{return value!=INVALID_HANDLE_VALUE;}
};
inline std::string systemError(){return std::system_category().message(GetLastError());}
inline bool regular(HANDLE h){BY_HANDLE_FILE_INFORMATION i{};return GetFileType(h)==FILE_TYPE_DISK&&GetFileInformationByHandle(h,&i)&&!(i.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));}
#endif
inline bool replaceFile(const std::filesystem::path& from,const std::filesystem::path& to,std::string& error){
#ifdef _WIN32
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){error=systemError();return false;}
#else
    if(::rename(from.c_str(),to.c_str())<0){error=std::strerror(errno);return false;}
#endif
    error.clear();return true;
}
inline bool atomicWrite(const std::filesystem::path& path,std::span<const uint8_t> bytes,std::string& error){
#ifdef _WIN32
    static std::atomic<uint64_t> serial{0};std::filesystem::path temp;HANDLE raw=INVALID_HANDLE_VALUE;
    for(int attempt=0;attempt<100;++attempt){temp=path;temp+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++serial);
        raw=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(raw!=INVALID_HANDLE_VALUE||GetLastError()!=ERROR_FILE_EXISTS)break;}
    if(raw==INVALID_HANDLE_VALUE){error=systemError();return false;}
    bool ok=true;size_t offset=0;
    while(offset<bytes.size()){DWORD count=0;const auto chunk=DWORD(std::min<size_t>(bytes.size()-offset,1024*1024));
        if(!WriteFile(raw,bytes.data()+offset,chunk,&count,nullptr)||!count){ok=false;error=systemError();break;}offset+=count;}
    if(ok&&!FlushFileBuffers(raw)){ok=false;error=systemError();}CloseHandle(raw);
    if(ok)ok=replaceFile(temp,path,error);if(!ok)DeleteFileW(temp.c_str());return ok;
#else
    std::string temp=path.string()+".tmp.XXXXXX";std::vector<char> name(temp.begin(),temp.end());name.push_back(0);
    const int fd=::mkstemp(name.data());if(fd<0){error=std::strerror(errno);return false;}
    bool ok=true;size_t offset=0;while(offset<bytes.size()){const auto n=::write(fd,bytes.data()+offset,bytes.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}offset+=size_t(n);}
    if(ok&&::fsync(fd)<0)ok=false;
    int failure=errno;if(::close(fd)<0&&ok){ok=false;failure=errno;}
    if(ok&&::rename(name.data(),path.c_str())<0){ok=false;failure=errno;}
    if(!ok){::unlink(name.data());error=std::strerror(failure);return false;}
    auto parent=path.parent_path();if(parent.empty())parent=".";int dir=::open(parent.c_str(),O_RDONLY|O_DIRECTORY);if(dir>=0){::fsync(dir);::close(dir);}error.clear();return true;
#endif
}
}
