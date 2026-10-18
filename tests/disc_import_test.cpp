#include "io/disc_import.h"
#include "io/rsc.h"
#include "io/vfs.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <array>
#include <iostream>
namespace fs=std::filesystem;
using Bytes=std::vector<unsigned char>;
void put(Bytes& b,size_t off,uint32_t n,int size=4){for(int i=0;i<size;++i)b[off+i]=n>>(i*8);}
void write(const fs::path& path,const Bytes& bytes){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(f.good());}
Bytes rsc(){
 const std::string name="UNDERGROUND\\T1U1.3DG";const int data=44+name.size()+1;Bytes b(data+4);put(b,0,1,2);put(b,2,0x102,2);put(b,4,32);put(b,8,12);put(b,12,44);put(b,16,name.size()+1);put(b,20,data);put(b,24,4);put(b,28,0x5f584b5f);put(b,40,4);
 for(size_t i=0;i<name.size();++i)b[44+i]=name[i];
 for(auto range:{std::pair{32,12},std::pair{44,int(name.size()+1)}})for(int i=0;i<range.second;++i){unsigned char x=~b[range.first+i];const int n=i%8;b[range.first+i]=n?((x<<n)|(x>>(8-n))):x;}
 for(int i=0;i<4;++i)b[data+i]=(i+1)^255;return b;
}
// Three entries larger than a stream buffer expose shared seek-position races.
Bytes threadedRsc(){
 constexpr size_t count=3,span=32771,names=32+count*12,payload=names+6;
 Bytes b(payload+count*span);put(b,0,count,2);put(b,2,0x102,2);
 put(b,4,32);put(b,8,count*12);put(b,12,names);put(b,16,6);
 put(b,20,payload);put(b,24,count*span);put(b,28,0x5f584b5f);
 for(size_t i=0;i<count;++i){put(b,32+i*12,i*2,2);put(b,36+i*12,i*span);put(b,40+i*12,span);b[names+i*2]='a'+i;
  for(size_t j=0;j<span;++j)b[payload+i*span+j]=uint8_t((j+i*37)^255);}
 for(auto [off,size]:{std::pair{size_t(32),count*12},std::pair{names,size_t(6)}})
  for(size_t i=0;i<size;++i){uint8_t x=~b[off+i];const auto n=i%8;b[off+i]=n?uint8_t((x<<n)|(x>>(8-n))):x;}
 return b;
}
void archiveHandleChecks(const fs::path& root){
 const auto path=root/"threaded.rsc";write(path,threadedRsc());
 vyruz::RscArchive a;assert(a.open(path.string()));auto copy=a;auto moved=std::move(a);
 std::array<std::thread,4> readers;
 for(size_t t=0;t<readers.size();++t)readers[t]=std::thread([&,t]{
  for(size_t n=0;n<150;++n){const auto id=(n+t)%3;auto bytes=(t%2?copy:moved).read(id);
   assert(bytes.size()==32771);for(size_t j=0;j<bytes.size();++j)assert(bytes[j]==uint8_t(j+id*37));}
 });
 for(auto& reader:readers)reader.join();
 assert(!moved.open((root/"missing-archive").string())&&moved.entryCount()==0);
 assert(copy.read(2).front()==74); // resetting one owner must not close another
 bool rejected=false;try{copy.read(3);}catch(const std::out_of_range&){rejected=true;}assert(rejected);
 copy={};moved={};a={}; // release handles before filesystem cleanup on Windows
 fs::remove(path);
}
size_t record(Bytes& b,size_t at,const std::string& name,uint32_t block,uint32_t size,bool directory){
 const size_t n=33+name.size()+(name.size()%2==0);b[at]=n;put(b,at+2,block);put(b,at+10,size);b[at+25]=directory?2:0;b[at+32]=name.size();for(size_t i=0;i<name.size();++i)b[at+33+i]=name[i];return n;
}
Bytes iso(bool movie=false){
 Bytes b((movie?25:24)*2048);size_t p=16*2048;b[p]=1;for(size_t i=0;i<5;++i)b[p+1+i]="CD001"[i];b[p+6]=1;put(b,p+80,movie?25:24);put(b,p+128,2048,2);record(b,p+156,std::string(1,'\0'),20,2048,true);
 record(b,20*2048,"VYRUZ",21,2048,true);size_t at=21*2048;auto archive=rsc();at+=record(b,at,"VZ-DATA.RSC;1",22,archive.size(),false);at+=record(b,at,"VZ-SLIDE.RSC;1",23,archive.size(),false);
 if(movie){record(b,at,"Intro.avi;1",24,5,false);for(int i=0;i<5;++i)b[24*2048+i]=i+10;}
 std::copy(archive.begin(),archive.end(),b.begin()+22*2048);std::copy(archive.begin(),archive.end(),b.begin()+23*2048);return b;
}
void chunkedImportChecks(const fs::path& root){
 const auto archive=threadedRsc();Bytes movie(65537);for(size_t i=0;i<movie.size();++i)movie[i]=uint8_t(i*7);
 const uint32_t archiveBlocks=(archive.size()+2047)/2048;
 const uint32_t second=22+archiveBlocks,video=second+archiveBlocks,blocks=video+(movie.size()+2047)/2048;
 auto image=iso();image.resize(size_t(blocks)*2048);put(image,16*2048+80,blocks);
 std::fill(image.begin()+21*2048,image.begin()+22*2048,0);
 size_t at=21*2048;at+=record(image,at,"VZ-DATA.RSC;1",22,archive.size(),false);
 at+=record(image,at,"VZ-SLIDE.RSC;1",second,archive.size(),false);
 record(image,at,"INTRO.AVI;1",video,movie.size(),false);
 std::copy(archive.begin(),archive.end(),image.begin()+22*2048);
 std::copy(archive.begin(),archive.end(),image.begin()+second*2048);
 std::copy(movie.begin(),movie.end(),image.begin()+video*2048);
 auto read=[](const fs::path& path){std::ifstream f(path,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});};
 for(int layout:{0,16,24}){
  const auto dir=root/("chunked-"+std::to_string(layout));fs::create_directories(dir);
  fs::path source=dir/"disc.iso";
  if(!layout)write(source,image);
  else{
   // Nonzero data-track start plus poisoned raw-sector headers/trailers.
   Bytes raw(size_t(blocks+7)*2352,0xa5);
   for(size_t n=0;n<blocks;++n)std::copy_n(image.begin()+n*2048,2048,raw.begin()+(n+7)*2352+layout);
   write(dir/"disc.bin",raw);source=dir/"disc.cue";
   std::ofstream cue(source);cue<<"FILE \"disc.bin\" BINARY\nTRACK 01 "<<(layout==16?"MODE1":"MODE2")<<"/2352\nINDEX 01 00:00:07\n";
  }
  vyruz::DataLocation location;std::string error;std::vector<vyruz::ImportProgress> progress;
  assert(vyruz::prepareData(source.string(),(dir/"cache").string(),location,error,[&](const auto&p){progress.push_back(p);return true;}));
  assert(read(fs::path(location.directory)/"vz-data.rsc")==archive);
  assert(read(fs::path(location.directory)/"Vz-slide.rsc")==archive);
  assert(read(fs::path(location.directory)/"Intro.avi")==movie);
  size_t checkpoints=0;for(const auto&p:progress)if(p.stage==vyruz::ImportStage::Resources&&p.completed==65536)++checkpoints;
  assert(checkpoints==3); // both archives and the partial-final-sector movie
  const auto canceled=dir/"canceled";
  assert(!vyruz::prepareData(source.string(),canceled.string(),location,error,[](const auto&p){return p.stage!=vyruz::ImportStage::Resources||p.completed<65536;}));
  assert(error.find("cancelled")!=std::string::npos&&!fs::exists(canceled/"current.txt"));
  for(const auto&e:fs::directory_iterator(canceled))assert(!e.path().filename().string().starts_with(".import-"));
 }
}
int main(){try{
 const auto root=fs::temp_directory_path()/("vyruz-import-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(root/"game");
 archiveHandleChecks(root);
 chunkedImportChecks(root);
 const auto image=root/"game/disc.iso",cache=root/"user/imports";write(image,iso());vyruz::DataLocation location;std::string error;
 assert(vyruz::prepareData((root/"game").string(),cache.string(),location,error)&&location.imported);const auto imported=location.directory;
 vyruz::Vfs vfs;assert(vfs.init(imported));Bytes data;assert(vfs.read("underground/t1u1.3dg",data)&&data==Bytes({1,2,3,4}));
 assert(!vfs.read("../../etc/passwd",data)&&data.empty());assert(vfs.read("../underground/t1u1.3dg",data));
 assert(vyruz::prepareData((root/"game").string(),cache.string(),location,error)&&!location.imported&&location.directory==imported);
 fs::remove(image);assert(vyruz::prepareData((root/"game").string(),cache.string(),location,error)&&location.directory==imported);
 // Direct invocation with the old image path also resolves its receipt.
 assert(vyruz::prepareData(image.string(),cache.string(),location,error)&&location.directory==imported);
 write(root/"bad.rsc",Bytes(32));vyruz::RscArchive archive;assert(archive.open(imported+"/vz-data.rsc"));assert(!archive.open((root/"bad.rsc").string())&&archive.entryCount()==0);
 auto broken=rsc();put(broken,4,0xfffffff0);write(root/"bad.rsc",broken);assert(!archive.open((root/"bad.rsc").string()));
 broken=rsc();broken[32]=broken[33]=0;write(root/"bad.rsc",broken);assert(!archive.open((root/"bad.rsc").string())); // encrypted name offset 65535
 broken=rsc();broken.pop_back();write(root/"bad.rsc",broken);assert(!archive.open((root/"bad.rsc").string()));
 auto tree=root/"loose";fs::create_directories(tree/"UnderGround");write(tree/"UnderGround/T1u1.3dG",{4,3,2,1});assert(vfs.init(tree.string()));assert(vfs.read("UNDERGROUND/T1U1.3DG",data)&&data==Bytes({4,3,2,1}));
 assert(!vfs.init((root/"missing").string())&&!vfs.read("UNDERGROUND/T1U1.3DG",data));
 // Portable imports follow the executable folder, not the working directory
 // or user's data root. Relative selection survives source deletion and rename.
 const auto portable=root/"portable";fs::create_directory(portable);
 const auto portableCache=vyruz::defaultImportRoot(portable.string());
 assert(fs::path(portableCache)==portable/"data");
 write(portable/"disc.iso",iso(true));
 assert(vyruz::prepareData(portable.string(),portableCache,location,error)&&location.imported);
 const auto selected=fs::path(location.directory).filename();
 assert(fs::exists(portable/"data/current.txt"));
 fs::remove(portable/"disc.iso");fs::rename(portable,root/"moved");
 const auto moved=root/"moved";const auto movedCache=vyruz::defaultImportRoot(moved.string());
 assert(vyruz::prepareData(moved.string(),movedCache,location,error)&&!location.imported);
 assert(fs::path(location.directory)==moved/"data"/selected);
 assert(vfs.init(location.directory)&&vfs.read("intro.avi",data)&&data==Bytes({10,11,12,13,14}));
 // An explicitly selected outside disc is still imported beside the executable
 // and becomes the default local selection. A bad explicit image must fail.
 write(root/"external.iso",iso(true));
 assert(vyruz::prepareData((root/"external.iso").string(),movedCache,location,error));
 const auto externalImport=location.directory;fs::remove(root/"external.iso");
 assert(vyruz::prepareData(moved.string(),movedCache,location,error)&&location.directory==externalImport);
 write(root/"bad-explicit.iso",Bytes(100));
 assert(!vyruz::prepareData((root/"bad-explicit.iso").string(),movedCache,location,error));
 // Legacy archives can be copied directly into data/ without source media.
 const auto legacy=root/"legacy";fs::create_directories(legacy/"data");
 write(legacy/"data/vz-data.rsc",rsc());write(legacy/"data/Vz-slide.rsc",rsc());
 assert(vyruz::prepareData(legacy.string(),vyruz::defaultImportRoot(legacy.string()),location,error));
 assert(fs::path(location.directory)==legacy/"data");
 // A non-directory destination fails rather than silently importing elsewhere.
 const auto blocked=root/"blocked";fs::create_directory(blocked);write(blocked/"disc.iso",iso());write(blocked/"data",Bytes(1));
 assert(!vyruz::prepareData(blocked.string(),vyruz::defaultImportRoot(blocked.string()),location,error));
 assert(fs::exists(blocked/"disc.iso"));
 // Optional movie is retained byte-for-byte and survives source deletion.
 fs::create_directories(root/"movie");const auto movieSource=root/"movie/disc.iso";write(movieSource,iso(true));
 assert(vyruz::prepareData((root/"movie").string(),cache.string(),location,error)&&location.imported);
 const fs::path movieImport=location.directory;
 assert(vfs.init(location.directory)&&vfs.read("intro.avi",data)&&data==Bytes({10,11,12,13,14}));
 // Simulate an old importer receipt (before integrity records): unchanged image can backfill atomically.
 fs::remove(movieImport/".vurus-files-v1");
 fs::remove(movieImport/"Intro.avi");
 assert(vyruz::prepareData((root/"movie").string(),cache.string(),location,error)&&location.imported&&location.directory==movieImport.string());
 assert(vfs.init(location.directory)&&vfs.read("intro.avi",data)&&data==Bytes({10,11,12,13,14}));
 fs::remove(movieImport/"Intro.avi");
 assert(vyruz::prepareData(movieSource.string(),cache.string(),location,error)&&location.imported);
 fs::remove(movieSource);
 assert(vyruz::prepareData((root/"movie").string(),cache.string(),location,error)&&!location.imported);
 assert(vfs.init(location.directory)&&vfs.read("intro.avi",data));
 // No source after a pre-movie import remains usable, with no fabricated movie.
 fs::remove(movieImport/"Intro.avi");
 assert(vyruz::prepareData((root/"movie").string(),cache.string(),location,error)&&!location.imported);
 // Unrelated new images must not modify the receipt's selected installation.
 write(root/"movie/other.iso",iso(true));
 assert(vyruz::prepareData((root/"movie").string(),cache.string(),location,error)&&!location.imported&&location.directory==movieImport.string());
 assert(!fs::exists(movieImport/"Intro.avi"));
 // Invalid optional extent fails a fresh transaction; no partial install survives.
 fs::create_directories(root/"badmovie");auto badMovie=iso(true);
 const auto recordSize=[](const std::string& name){return 33+name.size()+(name.size()%2==0);};
 const size_t movieRecord=21*2048+recordSize("VZ-DATA.RSC;1")+recordSize("VZ-SLIDE.RSC;1");
 badMovie[movieRecord+25]=2;write(root/"badmovie/disc.iso",badMovie);
 assert(!vyruz::prepareData((root/"badmovie").string(),cache.string(),location,error));
 // A matching but now unreadable movie must not break an older playable cache.
 fs::create_directories(root/"recoverbad");const auto recoverSource=root/"recoverbad/disc.iso";
 write(recoverSource,iso(true));assert(vyruz::prepareData(recoverSource.string(),cache.string(),location,error));
 const fs::path recoverImport=location.directory;fs::remove(recoverImport/".vurus-files-v1");fs::remove(recoverImport/"Intro.avi");
 const auto recoverStamp=fs::last_write_time(recoverSource);write(recoverSource,badMovie);fs::last_write_time(recoverSource,recoverStamp);
 assert(vyruz::prepareData((root/"recoverbad").string(),cache.string(),location,error)&&!location.imported);
 assert(location.directory==recoverImport.string()&&!fs::exists(recoverImport/"Intro.avi"));
 for(const auto& e:fs::directory_iterator(recoverImport))assert(e.path().filename().string().rfind(".intro-",0)!=0);
 // The alternate raw MODE1 sector layout is detected without a CUE.
 auto mode1=iso();Bytes mode1bin(24*2352);for(size_t i=0;i<24;++i)std::copy_n(mode1.begin()+i*2048,2048,mode1bin.begin()+i*2352+16);
 write(root/"mode1.bin",mode1bin);assert(vyruz::prepareData((root/"mode1.bin").string(),cache.string(),location,error)&&location.imported);
 auto raw=iso(true);Bytes bin(27*2352);for(size_t i=0;i<25;++i)std::copy_n(raw.begin()+i*2048,2048,bin.begin()+i*2352+24);
 fs::create_directories(root/"cue");write(root/"cue/disc.bin",bin);
 {std::ofstream cue(root/"cue/disc.cue");cue<<"FILE \"DISC.BIN\" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n TRACK 02 AUDIO\n INDEX 01 00:00:25\n";}
 std::vector<vyruz::ImportProgress> progress;
 const auto cancelCache=root/"cancel-cache";
 assert(!vyruz::prepareData((root/"cue").string(),cancelCache.string(),location,error,[&](const auto& p){progress.push_back(p);return !(p.stage==vyruz::ImportStage::Music&&p.completed>0);}));
 assert(error.find("cancelled")!=std::string::npos&&fs::exists(root/"cue/disc.bin"));
 assert(!fs::exists(cancelCache/"current.txt"));
 for(const auto& e:fs::directory_iterator(cancelCache))assert(e.path().filename().string().rfind(".import-",0)!=0);
 progress.clear();
 assert(vyruz::prepareData((root/"cue").string(),cancelCache.string(),location,error,[&](const auto& p){progress.push_back(p);return true;}));
 const auto progressImport=location.directory;fs::remove(fs::path(progressImport)/".vurus-files-v1");
 bool resources=false,music=false,finished=false;
 for(const auto& p:progress){assert(p.completed<=p.total);resources|=p.stage==vyruz::ImportStage::Resources&&p.total&&p.completed==p.total;music|=p.stage==vyruz::ImportStage::Music&&p.total&&p.completed==p.total;finished|=p.stage==vyruz::ImportStage::Finishing;}
 assert(resources&&music&&finished);
 // Resource cancellation leaves neither a selected cache nor staged files.
 const auto cancelResources=root/"cancel-resources";
 assert(!vyruz::prepareData((root/"cue").string(),cancelResources.string(),location,error,[](const auto& p){return !(p.stage==vyruz::ImportStage::Resources&&p.completed>0);}));
 assert(!fs::exists(cancelResources/"current.txt"));
 for(const auto& e:fs::directory_iterator(cancelResources))assert(e.path().filename().string().rfind(".import-",0)!=0);
 // Cancelling optional legacy movie recovery preserves the playable archives.
 fs::remove(fs::path(progressImport)/"Intro.avi");
 assert(!vyruz::prepareData((root/"cue").string(),cancelCache.string(),location,error,[](const auto& p){return p.stage!=vyruz::ImportStage::Resources;}));
 assert(vyruz::prepareData((root/"cue").string(),cancelCache.string(),location,error));
 assert(vyruz::prepareData((root/"cue").string(),cache.string(),location,error)&&location.imported);
 assert(fs::file_size(fs::path(location.directory)/"music/track02.wav")==44+2*2352);
 assert(vfs.init(location.directory)&&vfs.read("Intro.avi",data)&&data==Bytes({10,11,12,13,14}));
 fs::remove(root/"cue/disc.bin");assert(vyruz::prepareData((root/"cue").string(),cache.string(),location,error));
 fs::create_directories(root/"invalid");write(root/"invalid/disc.iso",Bytes(100));assert(!vyruz::prepareData((root/"invalid").string(),cache.string(),location,error)&&!error.empty());
 write(root/"invalid/second.iso",iso());assert(!vyruz::prepareData((root/"invalid").string(),cache.string(),location,error)&&error.find("Multiple")!=std::string::npos);
 // Reject corrupt ISO directory records without publishing a receipt.
 auto corrupt=iso();corrupt[20*2048]=2;write(root/"invalid/second.iso",corrupt);fs::remove(root/"invalid/disc.iso");assert(!vyruz::prepareData((root/"invalid").string(),cache.string(),location,error));
 for(const auto& e:fs::directory_iterator(cache))assert(e.path().filename().string().rfind(".import-",0)!=0);
 // Retained archive streams must close before deleting their fixture files.
 vfs={};archive={};
 fs::remove_all(root);
 return 0;
}catch(const std::exception& error){std::cerr<<"Disc import test: "<<error.what()<<'\n';return 1;}
}
