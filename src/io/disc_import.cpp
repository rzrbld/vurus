#include "io/disc_import.h"
#include "io/rsc.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <memory>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "platform/files.h"
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/statvfs.h>
#endif

namespace vyruz {
namespace {
namespace fs=std::filesystem;
std::string upper(std::string s){for(auto& c:s)if(c>='a'&&c<='z')c-=32;return s;}
std::string hash(const std::string& s){uint64_t h=14695981039346656037ULL;for(unsigned char c:s){h^=c;h*=1099511628211ULL;}std::ostringstream o;o<<std::hex<<h;return o.str();}
void require(bool good,const std::string& message){if(!good)throw std::runtime_error(message);}
void syncPath(const fs::path& path,bool directory=false){
#ifdef _WIN32
    // Windows does not expose POSIX directory fsync. Files are flushed before
    // write-through publication; never pretend a directory flush succeeded.
    if(directory)return;
    platform::Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    require(bool(file)&&FlushFileBuffers(file.value),"Cannot synchronize imported output: "+path.string()+". "+platform::systemError());
#else
    const int fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC|(directory?O_DIRECTORY:0));
    require(fd>=0,"Cannot open imported output for synchronization: "+path.string());
    const bool synced=::fsync(fd)==0;const int syncError=errno;::close(fd);
    require(synced,"Cannot synchronize imported output: "+path.string()+". "+std::strerror(syncError)+". Check free space and write permissions, then Retry.");
#endif
}
struct ImportCancelled:std::runtime_error {using std::runtime_error::runtime_error;};
void report(const ImportProgressCallback& callback,ImportStage stage,const std::string& item={},uint64_t done=0,uint64_t total=0){
    if(callback && !callback({stage,item,done,total}))throw ImportCancelled("Import cancelled. Your original disc image was not changed.");
}
// Lock the cache directory itself: no lock-file publication race, and another
// launcher can never retire the first launcher's live staging directory.
#ifdef _WIN32
struct CacheLock {
    platform::Handle file;
    explicit CacheLock(const fs::path& cache):file(CreateFileW((cache/".import.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr)){
        require(bool(file),"Game files are being prepared by another launcher, or the import folder is not writable. Close it, then Retry.");
        require(platform::regular(file.value),"Import lock is not a regular file");
    }
};
#else
struct CacheLock {
    int fd=-1;
    explicit CacheLock(const fs::path& cache){
        fd=::open(cache.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        require(fd>=0,"Cannot access import folder: "+cache.string());
        if(::flock(fd,LOCK_EX|LOCK_NB)!=0){
            const int code=errno;::close(fd);fd=-1;
            if(code==EWOULDBLOCK||code==EAGAIN)throw std::runtime_error("Game files are being prepared by another launcher. Close it, then Retry.");
            throw std::runtime_error("Cannot lock import folder: "+cache.string()+". "+std::strerror(code)+". Retry in a local writable folder.");
        }
    }
    CacheLock(const CacheLock&)=delete;
    CacheLock& operator=(const CacheLock&)=delete;
    ~CacheLock(){if(fd>=0)::close(fd);}
};
#endif

bool keyName(const std::string& s){return !s.empty()&&s.size()<=16&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool generatedName(const std::string& s,const std::string& prefix){
    if(!s.starts_with(prefix))return false;
    const auto split=s.find('-',prefix.size());if(split==std::string::npos)return false;
    const auto serial=s.substr(split+1);
    return keyName(s.substr(prefix.size(),split-prefix.size()))&&!serial.empty()&&serial.find_first_not_of("0123456789")==std::string::npos;
}
const char* StageMarker=".vurus-staging";
const char* Manifest=".vurus-files-v1";
void recoverStaging(const fs::path& cache,const ImportProgressCallback& progress){
    for(const auto& e:fs::directory_iterator(cache)){
        const auto name=e.path().filename().string();
        if(e.is_symlink())continue;
        if(generatedName(name,".import-")&&e.is_directory()){
            const auto marker=e.path()/StageMarker;std::ifstream in(marker);std::string magic;std::getline(in,magic);
            // Empty pre-marker directories are harmless; never delete an
            // unmarked nonempty directory, even if its name resembles ours.
            const bool emptyMarker=fs::is_regular_file(fs::symlink_status(marker))&&fs::file_size(marker)==0&&std::distance(fs::directory_iterator(e.path()),fs::directory_iterator{})==1;
            if((fs::is_regular_file(fs::symlink_status(marker))&&magic=="Vurus staging v1")||fs::is_empty(e.path())||emptyMarker){
                report(progress,ImportStage::Checking,"Recovering interrupted import");std::error_code ignored;fs::remove_all(e.path(),ignored);
            }
        }else if((generatedName(name,".receipt-")||generatedName(name,".selected-"))&&e.is_regular_file()&&e.file_size()<=17){
            std::ifstream in(e.path());std::string key;std::getline(in,key);
            const auto prefix=name.starts_with(".receipt-")?std::string(".receipt-"):std::string(".selected-");
            if((keyName(key)&&name.starts_with(prefix+key+"-"))||e.file_size()==0){std::error_code ignored;fs::remove(e.path(),ignored);}
        }else if(keyName(name)&&e.is_directory()){
            // Reserved legacy optional-movie temp files, never directories or links.
            for(const auto& file:fs::directory_iterator(e.path())){
                const auto n=file.path().filename().string();const auto serial=n.starts_with(".intro-")?n.substr(7):std::string{};
                if(!serial.empty()&&serial.find_first_not_of("0123456789")==std::string::npos&&!file.is_symlink()&&file.is_regular_file()){std::error_code ignored;fs::remove(file.path(),ignored);}
            }
        }
    }
}
void checkSpace(const fs::path& cache,uint64_t bytes){
#ifdef _WIN32
    std::error_code spaceError;const auto info=fs::space(cache,spaceError);
    require(!spaceError,"Cannot check free space in "+cache.string());
    const auto available=info.available;
#else
    struct statvfs info{};
    require(::statvfs(cache.c_str(),&info)==0,"Cannot check free space in "+cache.string());
    const auto unit=uint64_t(info.f_frsize?info.f_frsize:info.f_bsize);
    const auto available=unit&&uint64_t(info.f_bavail)>UINT64_MAX/unit?UINT64_MAX:uint64_t(info.f_bavail)*unit;
#endif
    constexpr uint64_t reserve=1024*1024;
    require(bytes<=available&&reserve<=available-bytes,"Not enough free space for game files. Need "+std::to_string((bytes+reserve+1048575)/1048576)+" MiB in "+cache.string()+". Free space, then Retry.");
}
std::string outputError(const std::string& action,const fs::path& path){
    const auto code=errno;
    return action+": "+path.string()+". "+(code?std::string(std::strerror(code)):std::string("Check free space and write permissions"))+". Check the destination and Retry.";
}
uint64_t fileHash(const fs::path& path,const ImportProgressCallback& progress){
    std::ifstream in(path,std::ios::binary);require(bool(in),"Cannot verify imported file: "+path.string());
    uint64_t value=14695981039346656037ULL,done=0;const auto total=fs::file_size(path);std::array<char,65536> buffer{};
    while(in){in.read(buffer.data(),buffer.size());const auto n=in.gcount();for(std::streamsize i=0;i<n;++i){value^=static_cast<unsigned char>(buffer[i]);value*=1099511628211ULL;}done+=n;report(progress,ImportStage::Finishing,"Verifying "+path.filename().string(),done,total);}
    require(in.eof()&&done==total,"Cannot verify complete imported file: "+path.string());return value;
}
bool outputName(const std::string& name){
    if(name=="vz-data.rsc"||name=="Vz-slide.rsc"||name=="Intro.avi")return true;
    return name.size()==17&&name.starts_with("music/track")&&name.ends_with(".wav")&&name[11]>='0'&&name[11]<='9'&&name[12]>='0'&&name[12]<='9';
}
void writeManifest(const fs::path& directory,const ImportProgressCallback& progress){
    std::ofstream out(directory/Manifest);require(bool(out),outputError("Cannot create verification record",directory/Manifest));out<<"Vurus files v1\n";
    for(const auto& e:fs::recursive_directory_iterator(directory))if(e.is_regular_file()){
        const auto name=e.path().lexically_relative(directory).generic_string();if(!outputName(name))continue;
        out<<name<<' '<<e.file_size()<<' '<<fileHash(e.path(),progress)<<'\n';
    }
    out.close();require(bool(out),outputError("Cannot save verification record",directory/Manifest));
}
bool manifestValid(const fs::path& directory,bool full,const ImportProgressCallback& progress={}){
    const auto path=directory/Manifest;if(!fs::exists(path))return true; // Legacy imports remain supported.
    if(fs::is_symlink(path)||!fs::is_regular_file(path)||fs::file_size(path)>4096)return false;
    std::ifstream in(path);std::string line;std::getline(in,line);if(line!="Vurus files v1")return false;
    std::set<std::string> names;
    while(std::getline(in,line)){
        std::istringstream row(line);std::string name,extra;uint64_t size=0,digest=0;
        if(!(row>>name>>size>>digest)||(row>>extra)||!outputName(name)||!names.insert(name).second)return false;
        const auto file=directory/name;
        if(fs::is_symlink(file)||!fs::is_regular_file(file)||fs::file_size(file)!=size)return false;
        if(full&&fileHash(file,progress)!=digest)return false;
    }
    return in.eof()&&names.contains("vz-data.rsc")&&names.contains("Vz-slide.rsc");
}
uint32_t le32(const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
fs::path matching(const fs::path& directory,const std::string& filename){
    fs::path result;
    for(const auto& e:fs::directory_iterator(directory))if(upper(e.path().filename().string())==upper(filename)){
        require(result.empty(),"Ambiguous filename: "+filename);result=e.path();
    }
    return result;
}
struct Track {int number=0;bool audio=false;uint64_t start=0;bool indexed=false;};
struct Disc {fs::path image;std::string cue;std::vector<Track> tracks;};
Disc cueDisc(const fs::path& path){
    require(fs::file_size(path)<=1024*1024,"CUE file is too large");
    std::ifstream in(path);require(bool(in),"Cannot read CUE: "+path.string());
    Disc disc;std::ostringstream all;all<<in.rdbuf();disc.cue=all.str();
    std::istringstream lines(disc.cue);std::string line;bool fileSeen=false;
    while(std::getline(lines,line)){
        std::istringstream tokens(line);std::string command;tokens>>command;command=upper(command);
        if(command=="FILE"){
            require(!fileSeen,"Multi-file CUE images are not supported; use a single-BIN image or extracted resources");
            std::string filename,kind;tokens>>std::quoted(filename)>>kind;
            require(!filename.empty()&&upper(kind)=="BINARY","CUE requires a BINARY file");
            require(fs::path(filename).filename()==fs::path(filename),"CUE image must be in the same folder as its CUE");
            disc.image=matching(path.parent_path(),filename);
            require(!disc.image.empty()&&fs::is_regular_file(disc.image),"CUE references a missing BIN: "+filename);fileSeen=true;
        }else if(command=="TRACK"){
            int number=0;std::string mode;tokens>>number>>mode;mode=upper(mode);
            require(fileSeen&&number>=1&&number<=99,"Invalid CUE track");
            require(mode=="AUDIO"||mode=="MODE1/2352"||mode=="MODE2/2352","Unsupported CUE track mode: "+mode);
            require(disc.tracks.empty()||number>disc.tracks.back().number,"CUE tracks are out of order");
            disc.tracks.push_back({number,mode=="AUDIO"});
        }else if(command=="INDEX"){
            int index=-1,m=-1,s=-1,f=-1;char colon1=0,colon2=0;tokens>>index>>m>>colon1>>s>>colon2>>f;
            require(!disc.tracks.empty()&&tokens&&colon1==':'&&colon2==':'&&m>=0&&m<1000&&s>=0&&s<60&&f>=0&&f<75,"Invalid CUE INDEX");
            if(index==1){auto& t=disc.tracks.back();require(!t.indexed,"Duplicate CUE INDEX 01");t.start=(uint64_t(m)*60+s)*75+f;t.indexed=true;}
        }
    }
    require(!disc.tracks.empty()&&!disc.tracks.front().audio,"CUE must begin with a data track");
    uint64_t previous=0;
    for(size_t i=0;i<disc.tracks.size();++i){const auto& t=disc.tracks[i];require(t.indexed&&(i==0||t.start>previous)&&(i==0||t.audio),"Missing or unordered CUE track indexes");previous=t.start;}
    return disc;
}
class Iso {
    std::ifstream file_;uint64_t bytes_=0,base_=0;uint32_t stride_=0,offset_=0,blocks_=0;
public:
    struct Entry {uint32_t block=0,size=0;bool directory=false;};
    Entry root;
    explicit Iso(const Disc& disc):file_(disc.image,std::ios::binary){
        require(bool(file_),"Cannot read disc image: "+disc.image.string());bytes_=fs::file_size(disc.image);
        const uint64_t start=disc.tracks.empty()?0:disc.tracks.front().start;
        for(auto layout:{std::pair{2048u,0u},std::pair{2352u,16u},std::pair{2352u,24u}}){
            if(!disc.tracks.empty()&&layout.first!=2352)continue;
            stride_=layout.first;offset_=layout.second;base_=start*stride_;
            std::array<unsigned char,2048> p{};
            if(!sector(16,p.data()))continue;
            if(p[0]!=1||std::string(reinterpret_cast<char*>(p.data()+1),5)!="CD001"||p[6]!=1)continue;
            require(p[128]==0&&p[129]==8,"ISO must use 2048-byte logical blocks");
            blocks_=le32(p.data()+80);require(blocks_>16&&uint64_t(blocks_)*stride_<=bytes_-base_,"Truncated ISO data volume");
            require(p[156]>=34,"Invalid ISO root directory");root=entry(p.data()+156);require(root.directory,"Invalid ISO root");return;
        }
        throw std::runtime_error("Not a supported ISO9660 image (expected ISO2048 or MODE1/MODE2 BIN2352)");
    }
    bool sector(uint64_t n,unsigned char* out){
        const uint64_t pos=base_+n*stride_+offset_;
        if(pos>bytes_||2048>bytes_-pos)return false;
        file_.clear();file_.seekg(pos);return bool(file_.read(reinterpret_cast<char*>(out),2048));
    }
    Entry entry(const unsigned char* p){
        require(p[1]==0&&!(p[25]&0x80),"Extended or multi-extent ISO records are unsupported");
        Entry e{le32(p+2),le32(p+10),bool(p[25]&2)};
        require(e.block<blocks_&&uint64_t(e.size)<=uint64_t(blocks_-e.block)*2048,"ISO extent exceeds data volume");return e;
    }
    std::map<std::string,Entry> directory(Entry dir){
        require(dir.directory&&dir.size<=16*1024*1024,"Invalid ISO directory size");
        std::map<std::string,Entry> out;std::array<unsigned char,2048> data{};
        for(uint64_t base=0;base<dir.size;base+=2048){
            require(sector(dir.block+base/2048,data.data()),"Truncated ISO directory");
            const size_t limit=std::min<uint64_t>(2048,dir.size-base);
            for(size_t i=0;i<limit;){
                const size_t length=data[i];if(!length)break;
                require(length>=34&&length<=limit-i,"Invalid ISO directory record");
                const auto* p=data.data()+i;require(p[32]<=length-33,"Invalid ISO filename length");
                if(p[32]>1||p[33]>1){
                    std::string name(reinterpret_cast<const char*>(p+33),p[32]);name=name.substr(0,name.find(';'));
                    require(out.emplace(upper(name),entry(p)).second,"Duplicate ISO resource name");
                }
                i+=length;
            }
        }
        return out;
    }
    void extract(Entry e,const fs::path& path,const ImportProgressCallback& progress={},const std::string& displayName={}){
        require(!e.directory&&e.size<=256*1024*1024,"Invalid game resource extent");
        std::ofstream out(path,std::ios::binary);require(bool(out),"Cannot create imported resource: "+path.string());
        // Keep the original64KiB progress/cancellation cadence, but read a
        // contiguous run of sectors instead of seeking for every logical block.
        // Raw BIN headers/trailers are stripped in-place before the single write.
        constexpr size_t chunkSectors=32;
        std::array<unsigned char,chunkSectors*2352> data;
        const auto label=displayName.empty()?path.filename().string():displayName;
        report(progress,ImportStage::Resources,label,0,e.size);
        const uint64_t pos=base_+uint64_t(e.block)*stride_;
        const uint64_t sectors=(uint64_t(e.size)+2047)/2048;
        require(pos<=bytes_&&sectors*stride_<=bytes_-pos,"Truncated disc resource");
        file_.clear();file_.seekg(static_cast<std::streamoff>(pos));
        require(bool(file_),"Cannot seek disc resource");
        for(uint64_t done=0;done<e.size;){
            const auto count=std::min<uint64_t>(chunkSectors,(e.size-done+2047)/2048);
            require(bool(file_.read(reinterpret_cast<char*>(data.data()),count*stride_)),"Truncated disc resource");
            if(stride_!=2048 || offset_)for(size_t sector=0;sector<count;++sector)
                std::memmove(data.data()+sector*2048,data.data()+sector*stride_+offset_,2048);
            const auto size=std::min<uint64_t>(count*2048,e.size-done);
            out.write(reinterpret_cast<char*>(data.data()),size);
            require(bool(out),outputError("Cannot write imported resource",path));
            done+=size;
            if(count==chunkSectors)report(progress,ImportStage::Resources,label,done,e.size);
        }
        report(progress,ImportStage::Resources,label,e.size,e.size);
        out.close();require(bool(out),outputError("Cannot finish imported resource",path));
    }
};
void put32(std::ostream& out,uint32_t n){for(int i=0;i<4;++i)out.put(char(n>>(8*i)));}
void music(const Disc& disc,const fs::path& directory,const ImportProgressCallback& progress){
    if(disc.tracks.size()<2)return;
    fs::create_directories(directory);std::ifstream in(disc.image,std::ios::binary);const auto bytes=fs::file_size(disc.image);
    require(bytes%2352==0,"BIN ends in a partial audio sector");
    std::array<char,64*1024> buffer{};
    for(size_t i=1;i<disc.tracks.size();++i){
        const auto& t=disc.tracks[i];const uint64_t start=t.start*2352,end=i+1<disc.tracks.size()?disc.tracks[i+1].start*2352:bytes;
        require(start<end&&end<=bytes&&end-start<=UINT32_MAX-36,"Invalid CUE audio extent");
        std::ostringstream name;name<<"track"<<std::setw(2)<<std::setfill('0')<<t.number<<".wav";
        std::ofstream out(directory/name.str(),std::ios::binary);require(bool(out),"Cannot create music track");
        out.write("RIFF",4);put32(out,uint32_t(end-start)+36);out.write("WAVEfmt ",8);put32(out,16);
        out.put(1);out.put(0);out.put(2);out.put(0);put32(out,44100);put32(out,176400);out.put(4);out.put(0);out.put(16);out.put(0);out.write("data",4);put32(out,uint32_t(end-start));
        const auto label="CD track "+std::to_string(t.number)+" ("+std::to_string(i)+" of "+std::to_string(disc.tracks.size()-1)+")";
        report(progress,ImportStage::Music,label,0,end-start);
        in.clear();in.seekg(start);
        for(uint64_t left=end-start;left;){const auto size=std::min<uint64_t>(left,buffer.size());require(bool(in.read(buffer.data(),size)),"Truncated CD audio");out.write(buffer.data(),size);require(bool(out),outputError("Cannot write music track",directory/name.str()));left-=size;report(progress,ImportStage::Music,label,end-start-left,end-start);}
        out.close();require(bool(out),outputError("Cannot finish music track",directory/name.str()));
    }
}
std::string discKey(const Disc& disc){
    auto stamp=fs::last_write_time(disc.image).time_since_epoch().count();
    // libc++ can use a128-bit filesystem clock rep. Preserve every decimal
    // digit (and existing import identities) without narrowing to long long.
    const bool negative=stamp<0;
    std::string timestamp;
    do {
        const auto digit=stamp%10;
        timestamp.push_back(char('0'+(digit<0?-digit:digit)));
        stamp/=10;
    } while(stamp!=0);
    if(negative)timestamp.push_back('-');
    std::reverse(timestamp.begin(),timestamp.end());
    return hash("import-v1:"+disc.image.string()+":"+std::to_string(fs::file_size(disc.image))+":"+timestamp+":"+disc.cue);
}
std::map<std::string,Iso::Entry> gameEntries(Iso& iso){
    auto entries=iso.directory(iso.root);
    if(const auto sub=entries.find("VYRUZ");sub!=entries.end())entries=iso.directory(sub->second);
    return entries;
}
// Old imports are already playable: optional recovery must never invalidate
// them. Publish only a complete movie and keep the original receipt/archives.
bool backfillIntro(const Disc& disc,const fs::path& destination,const ImportProgressCallback& progress){
    fs::path staging;
    try {
        if(!matching(destination,"Intro.avi").empty())return false;
        Iso iso(disc);const auto entries=gameEntries(iso);
        const auto movie=entries.find("INTRO.AVI");if(movie==entries.end())return false;
        checkSpace(destination,movie->second.size);
        staging=destination/(".intro-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        iso.extract(movie->second,staging,progress,"Intro.avi");syncPath(staging);
        report(progress,ImportStage::Finishing,"Saving intro movie");
        fs::rename(staging,destination/"Intro.avi");staging.clear();syncPath(destination,true);return true;
    }catch(const ImportCancelled&){if(!staging.empty()){std::error_code ignored;fs::remove(staging,ignored);}throw;}
    catch(const std::exception&){if(!staging.empty()){std::error_code ignored;fs::remove(staging,ignored);}return false;}
}
bool recoverIntro(const fs::path& folder,const fs::path& destination,const std::string& savedKey,const ImportProgressCallback& progress){
    if(!matching(destination,"Intro.avi").empty())return false;
    // Do not let a newly dropped, unrelated image augment an older receipt.
    // Try each source independently: a leftover CUE with no BIN is harmless.
    for(const auto& e:fs::directory_iterator(folder)){
        if(!e.is_regular_file())continue;
        try {
            Disc disc;const auto ext=upper(e.path().extension().string());
            if(ext==".CUE")disc=cueDisc(e.path());
            else if(ext==".ISO"||ext==".BIN"){
                const auto cue=ext==".BIN"?matching(folder,e.path().stem().string()+".cue"):fs::path{};
                if(!cue.empty())disc=cueDisc(cue);else disc.image=e.path();
            }else continue;
            if(discKey(disc)==savedKey)return backfillIntro(disc,destination,progress);
        }catch(const ImportCancelled&){throw;}catch(const std::exception&){} // A valid existing import still wins.
    }
    return false;
}
bool validCache(const fs::path& path){
    if(!fs::is_directory(path))return false;
    RscArchive data,slides;const auto d=matching(path,"vz-data.rsc"),s=matching(path,"Vz-slide.rsc");
    return !d.empty()&&!s.empty()&&data.open(d.string())&&slides.open(s.string())&&manifestValid(path,false);
}
}
bool verifyImportedData(const std::string& directory,std::string& error,const ImportProgressCallback& progress){
    try {require(manifestValid(directory,true,progress),"Imported game files are missing or damaged in "+directory+". Restore the import from your original disc image.");error.clear();return true;}
    catch(const std::exception& e){error=e.what();return false;}
}
std::string defaultImportRoot(const std::string& executableDirectory){
    return (fs::path(executableDirectory)/"data").string();
}
bool prepareData(const std::string& input,const std::string& cacheRoot,DataLocation& result,std::string& error,const ImportProgressCallback& progress){
    result={};error.clear();fs::path temporary;std::vector<fs::path> receiptTemps;std::unique_ptr<CacheLock> lock;
    try {
        report(progress,ImportStage::Checking,"Looking for game resources");
        const auto source=fs::absolute(input).lexically_normal();
        const bool directory=fs::is_directory(source);
        const auto folder=directory?source:source.parent_path();
        const fs::path cache=fs::absolute(cacheRoot).lexically_normal();
        const bool localFolder=folder==cache.parent_path();
        const auto receipt=cache/(localFolder?"current.txt":"location-"+hash(folder.string())+".txt");
        Disc disc;
        if(directory){
            if(const auto archive=matching(source,"vz-data.rsc");!archive.empty()) {
                RscArchive data,slides;const auto slide=matching(source,"Vz-slide.rsc");
                require(data.open(archive.string())&&(slide.empty()||slides.open(slide.string()))&&manifestValid(source,false),"Invalid or incomplete game archives in "+source.string()+". Restore them from your original disc image.");
                result.directory=source.string();return true;
            }
            if(!matching(source,"COMMON").empty()) {result.directory=source.string();return true;}
            if(fs::exists(cache)){lock=std::make_unique<CacheLock>(cache);recoverStaging(cache,progress);}
            // A successful receipt wins for ordinary folder launches. This
            // also handles deleting only the large BIN and leaving its CUE.
            std::ifstream saved(receipt);std::string savedKey;std::getline(saved,savedKey);
            if(!savedKey.empty() && savedKey.find_first_not_of("0123456789abcdef")==std::string::npos && validCache(cache/savedKey)){
                result.directory=(cache/savedKey).string();
                result.imported=recoverIntro(source,cache/savedKey,savedKey,progress);return true;
            }
            // A manually relocated legacy import may place archives directly
            // in data/. Explicit external data folders keep their own precedence.
            if(localFolder && validCache(cache)){result.directory=cache.string();return true;}
            std::vector<fs::path> candidates;std::set<fs::path> cueImages;
            for(const auto& e:fs::directory_iterator(source))if(e.is_regular_file()&&upper(e.path().extension().string())==".CUE"){
                const auto candidate=cueDisc(e.path());cueImages.insert(candidate.image);candidates.push_back(e.path());
            }
            for(const auto& e:fs::directory_iterator(source))if(e.is_regular_file()){
                const auto ext=upper(e.path().extension().string());
                if(ext==".ISO"||(ext==".BIN"&&!cueImages.contains(e.path())))candidates.push_back(e.path());
            }
            require(candidates.size()<=1,"Multiple disc images found in "+source.string()+". Select one with --data /path/to/image.iso (or .cue).");
            if(!candidates.empty()){if(upper(candidates.front().extension().string())==".CUE")disc=cueDisc(candidates.front());else disc.image=candidates.front();}
        }else if(fs::is_regular_file(source)){
            const auto ext=upper(source.extension().string());
            require(ext==".ISO"||ext==".BIN"||ext==".CUE","--data expects a resource directory, ISO, BIN or CUE");
            if(ext==".CUE")disc=cueDisc(source);
            else if(ext==".BIN"){
                const auto cue=matching(source.parent_path(),source.stem().string()+".cue");
                if(!cue.empty()){
                    disc=cueDisc(cue);require(disc.image==source,"Adjacent CUE references a different BIN; select the intended CUE explicitly");
                }else disc.image=source;
            }else disc.image=source;
        }
        if(disc.image.empty()){
            std::ifstream saved(receipt);std::string key;std::getline(saved,key);
            if(!key.empty()&&key.find_first_not_of("0123456789abcdef")==std::string::npos && validCache(cache/key)) {result.directory=(cache/key).string();return true;}
            throw std::runtime_error("No game data found in "+source.string()+". Put your own Vyruz ISO or BIN/CUE beside the game executable, or use --data with a resource directory or disc image.");
        }
        if(!lock){fs::create_directories(cache);lock=std::make_unique<CacheLock>(cache);recoverStaging(cache,progress);}
        const auto key=discKey(disc);
        const auto destination=cache/key;
        if(!validCache(destination)){
            require(!fs::exists(destination),"Imported data is damaged. Remove this generated cache directory and retry with the original image: "+destination.string());
            fs::create_directories(cache);
            Iso iso(disc);auto entries=gameEntries(iso);
            uint64_t required=0;
            for(const auto& name:{"VZ-DATA.RSC","VZ-SLIDE.RSC","INTRO.AVI"}){
                const auto it=entries.find(name);if(it==entries.end()){require(std::string(name)=="INTRO.AVI","Disc does not contain Vyruz/"+std::string(name));continue;}
                require(!it->second.directory&&it->second.size<=256*1024*1024,"Invalid game resource extent: "+std::string(name));required+=it->second.size;
            }
            if(disc.tracks.size()>1){const auto bytes=fs::file_size(disc.image);require(bytes%2352==0,"BIN ends in a partial audio sector");
                for(size_t i=1;i<disc.tracks.size();++i){const auto start=disc.tracks[i].start*2352,end=i+1<disc.tracks.size()?disc.tracks[i+1].start*2352:bytes;require(start<end&&end<=bytes&&end-start<=UINT32_MAX-36,"Invalid CUE audio extent");required+=end-start+44;}}
            checkSpace(cache,required);
            temporary=cache/(".import-"+key+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            require(fs::create_directory(temporary),"Cannot create import staging directory");
            {std::ofstream marker(temporary/StageMarker);marker<<"Vurus staging v1\n";marker.close();require(bool(marker),outputError("Cannot mark import staging",temporary));}
            syncPath(temporary/StageMarker);syncPath(temporary,true);syncPath(cache,true);
            for(auto name:{"vz-data.rsc","Vz-slide.rsc"}){
                const auto entry=entries.find(upper(name));require(entry!=entries.end(),"Disc does not contain Vyruz/"+std::string(name));iso.extract(entry->second,temporary/name,progress);
            }
            require(validCache(temporary),"Disc contains invalid or truncated game archives");
            if(const auto movie=entries.find("INTRO.AVI");movie!=entries.end())iso.extract(movie->second,temporary/"Intro.avi",progress);
            music(disc,temporary/"music",progress);
            writeManifest(temporary,progress);
            report(progress,ImportStage::Finishing,"Verifying and saving imported files");
            for(const auto& e:fs::recursive_directory_iterator(temporary))if(e.is_regular_file())syncPath(e.path());
            if(fs::is_directory(temporary/"music"))syncPath(temporary/"music",true);
            syncPath(temporary,true);
            report(progress,ImportStage::Finishing,"Publishing completed import");
            if(fs::exists(destination)){require(validCache(destination),"Another import produced invalid data");fs::remove_all(temporary);}else fs::rename(temporary,destination);
            temporary.clear();syncPath(cache,true);result.imported=true;
        }
        else result.imported=backfillIntro(disc,destination,progress);
        // Publish only after the entire import succeeds. The receipt contains
        // a cache key, never an arbitrary destination path.
        report(progress,ImportStage::Finishing,"Selecting game resources");
        const auto receiptTemp=cache/(".receipt-"+key+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        receiptTemps.push_back(receiptTemp);
        {std::ofstream out(receiptTemp);out<<key<<'\n';out.close();require(bool(out),"Cannot save imported-data location");}
        syncPath(receiptTemp);{std::string e;require(platform::replaceFile(receiptTemp,receipt,e),"Cannot publish import receipt: "+e);}syncPath(cache,true);
        // The selected import belongs to this executable's data folder. A
        // relative key survives moving that entire folder or deleting the source.
        if(receipt!=cache/"current.txt") {
            const auto selectedTemp=cache/(".selected-"+key+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            receiptTemps.push_back(selectedTemp);
            {std::ofstream out(selectedTemp);out<<key<<'\n';out.close();require(bool(out),"Cannot save selected game data");}
            syncPath(selectedTemp);{std::string e;require(platform::replaceFile(selectedTemp,cache/"current.txt",e),"Cannot publish selected import: "+e);}syncPath(cache,true);
        }
        result.directory=destination.string();return true;
    }catch(const std::exception& e){result={};error=e.what();for(const auto& p:receiptTemps){std::error_code ignored;fs::remove(p,ignored);}if(!temporary.empty()){std::error_code ignored;fs::remove_all(temporary,ignored);}return false;}
}
}
