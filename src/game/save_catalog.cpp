#ifdef _WIN32
#include "game/save_catalog_windows.inc"
#else
#include "game/save_catalog.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#if defined(__linux__) && !defined(VYRUZ_PORTABLE_POSIX_FILES)
#include <sys/syscall.h>
#include <linux/fs.h>
#endif
#include <unistd.h>
namespace vyruz { namespace {
struct Fd { int value=-1;explicit Fd(int n):value(n){} ~Fd(){if(value>=0)::close(value);} };
std::string profileComponent(const std::string& profile){
    constexpr char hex[]="0123456789abcdef";std::string result="p-";
    for(unsigned char c:profile){result+=hex[c>>4];result+=hex[c&15];}return result;
}
std::string fileName(uint64_t n){auto digits=std::to_string(n);return "s"+std::string(digits.size()<3?3-digits.size():0,'0')+digits+".vzs";}
std::optional<uint64_t> fileNumber(const std::string& name){
    if(name.size()<8 || name.front()!='s' || !name.ends_with(".vzs"))return {};
    uint64_t value=0;const auto first=name.data()+1,last=name.data()+name.size()-4;
    auto parsed=std::from_chars(first,last,value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=last || fileName(value)!=name)return {};
    return value;
}
int historyFd(const std::filesystem::path& base,const std::string& profile,bool create){
    auto parent=base.parent_path();if(parent.empty())parent=".";
    Fd parentFd(::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC));if(parentFd.value<0)return -1;
    if(create && ::mkdirat(parentFd.value,"saves",0700)<0 && errno!=EEXIST)return -1;
    Fd saves(::openat(parentFd.value,"saves",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));if(saves.value<0)return -1;
    const auto component=profileComponent(profile);
    if(create && ::mkdirat(saves.value,component.c_str(),0700)<0 && errno!=EEXIST)return -1;
    return ::openat(saves.value,component.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
}
struct Numbered {std::string name;uint64_t number;bool regular;};
std::vector<Numbered> numbered(int fd){
    std::vector<Numbered> result;
    // A separate open description avoids sharing a directory cursor across scans.
    int scan=::openat(fd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(scan<0)return result;
    DIR* directory=::fdopendir(scan);if(!directory){::close(scan);return result;}
    while(auto* entry=::readdir(directory)){
        std::string name=entry->d_name;const auto number=fileNumber(name);if(!number)continue;
        struct stat status{};
        if(::fstatat(fd,name.c_str(),&status,AT_SYMLINK_NOFOLLOW)==0)
            result.push_back({name,*number,S_ISREG(status.st_mode)});
    }
    ::closedir(directory);return result;
}
int publishNoReplace(int directory,const std::string& temporary,const std::string& name,const std::vector<uint8_t>& bytes){
    #if defined(__linux__) && !defined(VYRUZ_PORTABLE_POSIX_FILES)
    if(::syscall(SYS_renameat2,directory,temporary.c_str(),directory,name.c_str(),RENAME_NOREPLACE)==0)return 0;
    if(errno!=ENOSYS && errno!=EINVAL && errno!=EOPNOTSUPP)return -1;
    #else
    if(::linkat(directory,temporary.c_str(),directory,name.c_str(),0)==0)return 0;
    if(errno==EEXIST)return -1;
    #endif
    // Older kernels/filesystems may lack no-replace rename. An exclusive file
    // keeps collision safety; failed writes remove only the file just created.
    int fd=::openat(directory,name.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return -1;
    bool ok=true;size_t offset=0;
    while(offset<bytes.size()){
        auto count=::write(fd,bytes.data()+offset,bytes.size()-offset);
        if(count<0 && errno==EINTR)continue;
        if(count<=0){ok=false;break;}offset+=size_t(count);
    }
    if(ok && ::fsync(fd)<0)ok=false;
    int failure=errno;if(::close(fd)<0 && ok){ok=false;failure=errno;}
    if(!ok){::unlinkat(directory,name.c_str(),0);errno=failure;return -1;}
    return 0;
}
SaveCatalogEntry metadata(const std::filesystem::path& path,int fd,uint64_t number,bool legacy){
    SaveCatalogEntry entry;entry.path=path;entry.number=number;entry.legacy=legacy;
    entry.label=legacy?"Previous save":fileName(number);
    struct stat status{};SaveGame save;
    if(fd<0 || ::fstat(fd,&status)<0 || !S_ISREG(status.st_mode))entry.error="Cannot open save file";
    else if(status.st_size<0 || uint64_t(status.st_size)>256*1024*1024)entry.error="Invalid save file size";
    else {
        std::vector<uint8_t> bytes(static_cast<size_t>(status.st_size));size_t offset=0;
        while(offset<bytes.size()){
            auto count=::read(fd,bytes.data()+offset,bytes.size()-offset);
            if(count<0 && errno==EINTR)continue;
            if(count<=0){entry.error="Cannot read complete save file";break;}offset+=size_t(count);
        }
        if(entry.error.empty() && decodeSave(bytes,save,entry.error)){
            const auto& world=save.levels.at(save.currentLevel).world;
            if(world.player>=world.actors.size())entry.error="Save has no player";
            else {const auto& player=world.actors[world.player];entry.map=save.currentLevel;
                entry.difficulty=world.difficulty;entry.elapsedMs=world.elapsedMs;
                entry.energy=player.vitals.energy;entry.shield=player.vitals.shield;entry.money=player.vitals.money;}
        }
    }
    return entry;
}
}
std::filesystem::path saveHistoryDirectory(const std::filesystem::path& base,const std::string& profile){return base.parent_path()/"saves"/profileComponent(profile);}
std::filesystem::path defaultRetryPath(const std::filesystem::path& base,const std::string& profile){return saveHistoryDirectory(base,profile)/"default.vzs";}
bool writeDefaultRetry(const std::filesystem::path& base,const std::string& profile,const SaveGame& save,std::string& error){
    Fd directory(historyFd(base,profile,true));
    if(directory.value<0){error="Cannot open retry directory: "+std::string(std::strerror(errno));return false;}
    struct stat status{};
    if(::fstatat(directory.value,"default.vzs",&status,AT_SYMLINK_NOFOLLOW)==0 && !S_ISREG(status.st_mode)){error="Retry destination is not a regular file";return false;}
    //The held directory FD prevents parent symlink replacement from redirecting
    //atomic temporary creation/rename. The generic writer never follows the old file.
#if defined(__linux__) && !defined(VYRUZ_PORTABLE_POSIX_FILES)
    return writeSaveFile("/proc/self/fd/"+std::to_string(directory.value)+"/default.vzs",save,error);
#else
    std::vector<uint8_t> bytes;if(!encodeStoredSave(save,bytes,error))return false;
    static std::atomic<uint64_t> serial{0};std::string temporary;int fd=-1;
    for(int i=0;i<100;++i){temporary=".retry-"+std::to_string(::getpid())+"-"+std::to_string(++serial);fd=::openat(directory.value,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd>=0||errno!=EEXIST)break;}
    if(fd<0){error=std::strerror(errno);return false;}
    bool ok=true;size_t offset=0;while(offset<bytes.size()){auto n=::write(fd,bytes.data()+offset,bytes.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}offset+=size_t(n);}
    if(ok&&::fsync(fd)<0)ok=false;int failure=errno;if(::close(fd)<0&&ok){ok=false;failure=errno;}
    if(ok&&::renameat(directory.value,temporary.c_str(),directory.value,"default.vzs")<0){ok=false;failure=errno;}
    if(!ok){::unlinkat(directory.value,temporary.c_str(),0);error=std::strerror(failure);return false;}
    ::fsync(directory.value);error.clear();return true;
#endif
}
bool readDefaultRetry(const std::filesystem::path& base,const std::string& profile,SaveGame& save,std::string& error){
    Fd directory(historyFd(base,profile,false));
    if(directory.value<0){error="No retry session";return false;}
    Fd file(::openat(directory.value,"default.vzs",O_RDONLY|O_NOFOLLOW|O_CLOEXEC));struct stat status{};
    if(file.value<0 || ::fstat(file.value,&status)<0 || !S_ISREG(status.st_mode)){error="No regular retry session";return false;}
    if(status.st_size<0||uint64_t(status.st_size)>256*1024*1024){error="Invalid retry size";return false;}
    std::vector<uint8_t> bytes(static_cast<size_t>(status.st_size));size_t offset=0;
    while(offset<bytes.size()){auto n=::read(file.value,bytes.data()+offset,bytes.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0){error="Cannot read retry";return false;}offset+=size_t(n);}
    return decodeSave(bytes,save,error);
}
std::vector<SaveCatalogEntry> listSaveCatalog(const std::filesystem::path& base,const std::string& profile){
    std::vector<SaveCatalogEntry> result;Fd directory(historyFd(base,profile,false));
    if(directory.value>=0){
        auto entries=numbered(directory.value);std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.name>b.name;});
        for(const auto& entry:entries)if(entry.regular && result.size()<7){
            Fd file(::openat(directory.value,entry.name.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC));
            result.push_back(metadata(saveHistoryDirectory(base,profile)/entry.name,file.value,entry.number,false));
        }
    }
    struct stat status{};
    if(::lstat(base.c_str(),&status)==0 && S_ISREG(status.st_mode)){
        Fd file(::open(base.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC));result.push_back(metadata(base,file.value,0,true));
    }
    return result;
}
static std::optional<std::filesystem::path> publishHistorySave(const std::filesystem::path& base,const std::string& profile,const std::vector<uint8_t>& bytes,std::string& error){

    Fd directory(historyFd(base,profile,true));
    auto fail=[&](const char* action){error=std::string(action)+": "+std::strerror(errno);return std::optional<std::filesystem::path>{};};
    if(directory.value<0)return fail("Cannot open save history");
    uint64_t maximum=0;bool any=false;for(const auto& entry:numbered(directory.value)){maximum=std::max(maximum,entry.number);any=true;}
    if(maximum==std::numeric_limits<uint64_t>::max()){error="Save history number exhausted";return {};}
    static std::atomic<uint64_t> nonce{0};std::string temporary;int descriptor=-1;
    for(int attempt=0;attempt<100;++attempt){
        temporary=".pending-"+std::to_string(::getpid())+"-"+std::to_string(++nonce);
        descriptor=::openat(directory.value,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(descriptor>=0 || errno!=EEXIST)break;
    }
    if(descriptor<0)return fail("Cannot create history temporary file");
    bool ok=true;size_t offset=0;
    while(offset<bytes.size()){
        auto count=::write(descriptor,bytes.data()+offset,bytes.size()-offset);
        if(count<0 && errno==EINTR)continue;
        if(count<=0){ok=false;break;}offset+=size_t(count);
    }
    if(ok && ::fsync(descriptor)<0)ok=false;
    int failure=errno;if(::close(descriptor)<0 && ok){ok=false;failure=errno;}
    if(!ok){::unlinkat(directory.value,temporary.c_str(),0);errno=failure;return fail("Cannot write history save");}
    uint64_t next=any?maximum+1:0;std::string name;
    for(;;){
        name=fileName(next);
        // Prefer atomic no-replace rename; every fallback also reserves a new
        // path exclusively, so a raced symlink/directory survives.
        if(publishNoReplace(directory.value,temporary,name,bytes)==0)break;
        if(errno!=EEXIST || next==std::numeric_limits<uint64_t>::max()){
            failure=errno;::unlinkat(directory.value,temporary.c_str(),0);errno=failure;return fail("Cannot publish history save");
        }
        ++next;
    }
    ::unlinkat(directory.value,temporary.c_str(),0);::fsync(directory.value);
    // Native deletes session <= previous maximum-6 after the new save exists.
    if(next>=7)for(const auto& entry:numbered(directory.value))if(entry.regular && entry.number<=next-7){
        struct stat status{};
        if(::fstatat(directory.value,entry.name.c_str(),&status,AT_SYMLINK_NOFOLLOW)==0 && S_ISREG(status.st_mode))
            if(::unlinkat(directory.value,entry.name.c_str(),0)==0){
                const auto preview=entry.name.substr(0,entry.name.size()-4)+".tga";
                if(::fstatat(directory.value,preview.c_str(),&status,AT_SYMLINK_NOFOLLOW)==0 && S_ISREG(status.st_mode))
                    ::unlinkat(directory.value,preview.c_str(),0);
            }
    }
    ::fsync(directory.value);error.clear();return saveHistoryDirectory(base,profile)/name;
}
std::optional<std::filesystem::path> writeHistorySave(const std::filesystem::path& base,const std::string& profile,const SaveGame& save,std::string& error){
    std::vector<uint8_t> bytes;if(!encodeStoredSave(save,bytes,error))return {};
    return publishHistorySave(base,profile,bytes,error);
}
std::optional<std::filesystem::path> writeHistorySave(const std::filesystem::path& base,const std::string& profile,SaveGame&& save,std::string& error){
    std::vector<uint8_t> bytes;if(!encodeStoredSave(std::move(save),bytes,error))return {};
    return publishHistorySave(base,profile,bytes,error);
}

}

#endif
