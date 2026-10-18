// Windows host adapter. Keep directory handles open without DELETE sharing:
// ancestors cannot be renamed/replaced while a catalog transaction is active.
#include "game/save_catalog.h"
#include "platform/files.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <memory>
namespace vyruz { namespace {
namespace fs=std::filesystem;
std::string profileComponent(const std::string& profile){constexpr char hex[]="0123456789abcdef";std::string result="p-";for(unsigned char c:profile){result+=hex[c>>4];result+=hex[c&15];}return result;}
std::string fileName(uint64_t n){auto d=std::to_string(n);return "s"+std::string(d.size()<3?3-d.size():0,'0')+d+".vzs";}
std::optional<uint64_t> fileNumber(const std::string& name){if(name.size()<8||name.front()!='s'||!name.ends_with(".vzs"))return {};uint64_t n=0;auto r=std::from_chars(name.data()+1,name.data()+name.size()-4,n);if(r.ec!=std::errc{}||r.ptr!=name.data()+name.size()-4||fileName(n)!=name)return {};return n;}
struct DirectoryGuard {
    std::vector<std::unique_ptr<platform::Handle>> held;
    bool open(const fs::path& target){
        auto absolute=fs::absolute(target).lexically_normal();fs::path path=absolute.root_path();
        for(const auto& component:absolute.relative_path()){
            path/=component;auto h=std::make_unique<platform::Handle>(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            BY_HANDLE_FILE_INFORMATION info{};if(!*h||!GetFileInformationByHandle(h->value,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
            held.push_back(std::move(h));
        }return true;
    }
    bool history(const fs::path& base,const std::string& profile,bool create){
        auto parent=base.parent_path();if(parent.empty())parent=".";if(!open(parent))return false;
        for(const auto& name:{std::string("saves"),profileComponent(profile)}){parent/=name;if(create&&!CreateDirectoryW(parent.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;if(!open(parent))return false;}return true;
    }
};
bool regularPath(const fs::path& path){const DWORD a=GetFileAttributesW(path.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));}
bool readRegular(const fs::path& path,SaveGame& save,std::string& error){
    platform::Handle h(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    LARGE_INTEGER size{};if(!h||!platform::regular(h.value)||!GetFileSizeEx(h.value,&size)||size.QuadPart<0||size.QuadPart>256*1024*1024){error="Cannot open regular save file";return false;}
    std::vector<uint8_t> bytes(size_t(size.QuadPart));size_t offset=0;while(offset<bytes.size()){DWORD count=0;if(!ReadFile(h.value,bytes.data()+offset,DWORD(std::min<size_t>(bytes.size()-offset,1024*1024)),&count,nullptr)||!count){error="Cannot read complete save file";return false;}offset+=count;}return decodeSave(bytes,save,error);
}
struct Numbered{fs::path path;std::string name;uint64_t number;bool regular;};
std::vector<Numbered> numbered(const fs::path& folder){std::vector<Numbered> out;std::error_code ec;for(fs::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec)){auto name=it->path().filename().string();auto n=fileNumber(name);if(n)out.push_back({it->path(),name,*n,regularPath(it->path())});}return out;}
bool removeRegular(const fs::path& path){platform::Handle h(CreateFileW(path.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));if(h&&platform::regular(h.value)){FILE_DISPOSITION_INFO disposition{TRUE};return SetFileInformationByHandle(h.value,FileDispositionInfo,&disposition,sizeof(disposition))!=0;}return false;}
SaveCatalogEntry metadata(const fs::path& path,uint64_t n,bool legacy){SaveCatalogEntry entry;entry.path=path;entry.number=n;entry.legacy=legacy;entry.label=legacy?"Previous save":fileName(n);SaveGame save;if(readRegular(path,save,entry.error)){const auto& world=save.levels.at(save.currentLevel).world;if(world.player>=world.actors.size())entry.error="Save has no player";else{const auto& p=world.actors[world.player];entry.map=save.currentLevel;entry.difficulty=world.difficulty;entry.elapsedMs=world.elapsedMs;entry.energy=p.vitals.energy;entry.shield=p.vitals.shield;entry.money=p.vitals.money;}}return entry;}
}
fs::path saveHistoryDirectory(const fs::path& base,const std::string& profile){return base.parent_path()/"saves"/profileComponent(profile);}
fs::path defaultRetryPath(const fs::path& base,const std::string& profile){return saveHistoryDirectory(base,profile)/"default.vzs";}
bool writeDefaultRetry(const fs::path& base,const std::string& profile,SaveGame save,std::string& error){DirectoryGuard guard;if(!guard.history(base,profile,true)){error="Cannot open retry directory";return false;}auto path=defaultRetryPath(base,profile);const auto attrs=GetFileAttributesW(path.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&!regularPath(path)){error="Retry destination is not a regular file";return false;}std::vector<uint8_t> bytes;return encodeStoredSave(std::move(save),bytes,error)&&platform::atomicWrite(path,bytes,error);}
bool readDefaultRetry(const fs::path& base,const std::string& profile,SaveGame& save,std::string& error){DirectoryGuard guard;if(!guard.history(base,profile,false)){error="No retry session";return false;}return readRegular(defaultRetryPath(base,profile),save,error);}
std::vector<SaveCatalogEntry> listSaveCatalog(const fs::path& base,const std::string& profile){std::vector<SaveCatalogEntry> out;DirectoryGuard guard;if(guard.history(base,profile,false)){auto entries=numbered(saveHistoryDirectory(base,profile));std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.name>b.name;});for(const auto& e:entries)if(e.regular&&out.size()<7)out.push_back(metadata(e.path,e.number,false));}if(regularPath(base))out.push_back(metadata(base,0,true));return out;}
static std::optional<fs::path> publishHistorySave(const fs::path& base,const std::string& profile,const std::vector<uint8_t>& bytes,std::string& error){
DirectoryGuard guard;if(!guard.history(base,profile,true)){error="Cannot open save history";return {};}
    auto folder=saveHistoryDirectory(base,profile);uint64_t next=0;for(const auto& e:numbered(folder)){if(e.number==UINT64_MAX){error="Save history number exhausted";return {};}next=std::max(next,e.number+1);}
    // Each writer owns a different staging name. MoveFileEx without REPLACE
    // publishes a complete file exclusively; raced names are never overwritten.
    static std::atomic<uint64_t> serial{0};const auto temp=folder/(".pending-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial));
    if(!platform::atomicWrite(temp,bytes,error))return {};
    fs::path result;for(;;){result=folder/fileName(next);if(MoveFileExW(temp.c_str(),result.c_str(),MOVEFILE_WRITE_THROUGH))break;const auto code=GetLastError();if((code!=ERROR_ALREADY_EXISTS&&code!=ERROR_FILE_EXISTS)||next==UINT64_MAX){error=platform::systemError();DeleteFileW(temp.c_str());return {};}++next;}
    if(next>=7)for(const auto& e:numbered(folder))if(e.regular&&e.number<=next-7){if(removeRegular(e.path)){auto preview=e.path;preview.replace_extension(".tga");removeRegular(preview);}}
    error.clear();return result;
}
std::optional<fs::path> writeHistorySave(const fs::path& base,const std::string& profile,const SaveGame& save,std::string& error){
    std::vector<uint8_t> bytes;if(!encodeStoredSave(save,bytes,error))return {};
    return publishHistorySave(base,profile,bytes,error);
}
std::optional<fs::path> writeHistorySave(const fs::path& base,const std::string& profile,SaveGame&& save,std::string& error){
    std::vector<uint8_t> bytes;if(!encodeStoredSave(std::move(save),bytes,error))return {};
    return publishHistorySave(base,profile,bytes,error);
}

}
