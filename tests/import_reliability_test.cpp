// Linux adapter failure controls; all media and output paths are isolated.
#define main discImportFixtureMain
#include "disc_import_test.cpp"
#undef main
#include <sys/statvfs.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>
#include <iostream>
static bool noSpace=false,failSync=false;
static int syncCalls=0,failSyncAt=0;
extern "C" int __real_statvfs(const char*,struct statvfs*);
extern "C" int __wrap_statvfs(const char* p,struct statvfs* s){const int r=__real_statvfs(p,s);if(!r&&noSpace)s->f_bavail=0;return r;}
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd){if(failSync || (failSyncAt&&++syncCalls==failSyncAt)){errno=ENOSPC;return -1;}return __real_fsync(fd);}
static std::string text(const fs::path& p){std::ifstream in(p);return {std::istreambuf_iterator<char>(in),{}};}
static void childExit(pid_t pid,int expected){int status=0;assert(waitpid(pid,&status,0)==pid);assert(WIFEXITED(status)&&WEXITSTATUS(status)==expected);}
static void noStages(const fs::path& cache){for(const auto& e:fs::directory_iterator(cache))assert(e.path().filename().string().rfind(".import-",0)!=0);}
int main(){
 const auto root=fs::temp_directory_path()/("vurus-reliability-"+std::to_string(getpid()));assert(!fs::exists(root));fs::create_directories(root);
 const auto source=root/"disc.iso";write(source,iso(true));const auto cache=root/"data";
 vyruz::DataLocation location;std::string error;
 assert(vyruz::prepareData(source.string(),cache.string(),location,error));
 const auto original=location.directory,receipt=text(cache/"current.txt");
 assert(vyruz::verifyImportedData(original,error));
 // Same-size payload corruption is caught by explicit integrity verification.
 auto payload=rsc();payload.back()^=1;write(fs::path(original)/"vz-data.rsc",payload);
 assert(!vyruz::verifyImportedData(original,error)&&error.find("damaged")!=std::string::npos);
 write(fs::path(original)/"vz-data.rsc",rsc());assert(vyruz::verifyImportedData(original,error));
 fs::remove(fs::path(original)/"Intro.avi");
 assert(!vyruz::prepareData(source.string(),cache.string(),location,error)&&location.directory.empty());
 write(fs::path(original)/"Intro.avi",{10,11,12,13,14});
 // A verification record cannot request traversal or silently omit archives.
 const auto manifest=fs::path(original)/".vurus-files-v1";const auto savedManifest=text(manifest);
 {std::ofstream out(manifest);out<<"Vurus files v1\n../outside 0 0\n";}
 assert(!vyruz::verifyImportedData(original,error));{std::ofstream out(manifest);out<<savedManifest;}
 const auto dataOnly=root/"data-only";fs::create_directory(dataOnly);write(dataOnly/"VZ-DATA.RSC",rsc());
 assert(vyruz::prepareData(dataOnly.string(),cache.string(),location,error)&&location.directory==dataOnly.string());
 // Preserve the selected installation and receipt on every failed new import.
 const auto second=root/"second.iso";write(second,iso(true));
 noSpace=true;assert(!vyruz::prepareData(second.string(),cache.string(),location,error));noSpace=false;
 assert(error.find("Not enough free space")!=std::string::npos&&location.directory.empty());assert(text(cache/"current.txt")==receipt);noStages(cache);
 failSync=true;assert(!vyruz::prepareData(second.string(),cache.string(),location,error));failSync=false;
 assert(error.find("No space left")!=std::string::npos);assert(text(cache/"current.txt")==receipt);noStages(cache);
 // Fail each durability boundary, including after directory/receipt rename.
 // A committed selection may already be new, but it must always be complete.
 int syncFailures=0;
 for(int boundary=1;boundary<=20;++boundary){
  const auto output=root/("sync-"+std::to_string(boundary));
  assert(vyruz::prepareData(source.string(),output.string(),location,error));
  syncCalls=0;failSyncAt=boundary;const bool ok=vyruz::prepareData(second.string(),output.string(),location,error);failSyncAt=0;
  if(!ok){++syncFailures;assert(location.directory.empty());}
  noStages(output);auto selected=text(output/"current.txt");assert(!selected.empty());selected.pop_back();assert(vyruz::verifyImportedData((output/selected).string(),error));
  assert(vyruz::prepareData(second.string(),output.string(),location,error)&&vyruz::verifyImportedData(location.directory,error));
 }
 assert(syncFailures>=10);
 // A real failed file write, not just a preflight estimate (RLIMIT_FSIZE).
 auto pid=fork();assert(pid>=0);if(!pid){signal(SIGXFSZ,SIG_IGN);rlimit limit{64,64};assert(setrlimit(RLIMIT_FSIZE,&limit)==0);const bool ok=vyruz::prepareData(second.string(),cache.string(),location,error);_exit(!ok&&!error.empty()&&location.directory.empty()?0:1);}childExit(pid,0);noStages(cache);assert(text(cache/"current.txt")==receipt);
 if(geteuid()!=0){
  fs::permissions(cache,fs::perms::owner_read|fs::perms::owner_exec);
  const bool ok=vyruz::prepareData(second.string(),cache.string(),location,error);
  fs::permissions(cache,fs::perms::owner_all);assert(!ok&&location.directory.empty());assert(text(cache/"current.txt")==receipt);
 }
 // Unknown lookalikes and symlink targets are never retired as our staging.
 const auto unknown=cache/".import-abc-123";fs::create_directory(unknown);write(unknown/"keep",{8});
 const auto outside=root/"outside";fs::create_directory(outside);write(outside/"keep",{9});fs::create_directory_symlink(outside,cache/".import-def-123");
 assert(vyruz::prepareData(second.string(),cache.string(),location,error));assert(fs::exists(unknown/"keep")&&fs::exists(outside/"keep"));
 fs::remove_all(unknown);fs::remove(cache/".import-def-123");
 // An actual abrupt process exit leaves a durable owned stage. Restart must
 // retire it, restart from unchanged source, and publish a complete import.
 for(int stop=0;stop<5;++stop){
  const auto trial=root/("crash-"+std::to_string(stop));fs::create_directory(trial);
  auto raw=iso(true);Bytes bin(27*2352);for(size_t i=0;i<25;++i)std::copy_n(raw.begin()+i*2048,2048,bin.begin()+i*2352+24);
  write(trial/"disc.bin",bin);{std::ofstream cue(trial/"disc.cue");cue<<"FILE \"disc.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nTRACK 02 AUDIO\nINDEX 01 00:00:25\n";}
  const auto output=trial/"data";pid=fork();assert(pid>=0);if(!pid){
   vyruz::prepareData((trial/"disc.cue").string(),output.string(),location,error,[&](const auto& p){
    if((stop==0&&p.stage==vyruz::ImportStage::Resources&&p.completed>0)||
       (stop==1&&p.stage==vyruz::ImportStage::Music&&p.completed>0)||
       (stop==2&&p.item.starts_with("Verifying "))||
       (stop==3&&p.item=="Publishing completed import")||
       (stop==4&&p.item=="Selecting game resources"))_exit(77);return true;});_exit(1);
  }childExit(pid,77);
  assert(vyruz::prepareData((trial/"disc.cue").string(),output.string(),location,error));noStages(output);
  assert(vyruz::verifyImportedData(location.directory,error));assert(fs::file_size(fs::path(location.directory)/"music/track02.wav")==44+2*2352);
 }
 // A killed legacy movie backfill leaves only a reserved temp file; retry
 // cleans it without changing the playable archives or unrelated files.
 const auto legacyCache=root/"legacy-movie";assert(vyruz::prepareData(source.string(),legacyCache.string(),location,error));
 const fs::path legacyDir=location.directory;fs::remove(legacyDir/".vurus-files-v1");fs::remove(legacyDir/"Intro.avi");write(legacyDir/"keep.txt",{7});
 pid=fork();assert(pid>=0);if(!pid){vyruz::prepareData(source.string(),legacyCache.string(),location,error,[](const auto& p){if(p.stage==vyruz::ImportStage::Resources&&p.completed>0)_exit(77);return true;});_exit(1);}childExit(pid,77);
 assert(vyruz::prepareData(source.string(),legacyCache.string(),location,error));assert(fs::exists(legacyDir/"keep.txt")&&fs::file_size(legacyDir/"Intro.avi")==5);
 for(const auto& e:fs::directory_iterator(legacyDir))assert(!e.path().filename().string().starts_with(".intro-"));
 // Interrupted pre-marker/receipt files and a read-only destination at receipt
 // publication are recoverable without changing the previous selection.
 fs::create_directory(cache/".import-aaa-789");write(cache/".import-aaa-789/.vurus-staging",{});
 write(cache/".receipt-aaa-789",{});write(cache/".selected-aaa-789",{'a','a','a','\n'});
 const auto third=root/"third.iso";write(third,iso());const auto beforeThird=text(cache/"current.txt");
 if(geteuid()!=0){const bool ok=vyruz::prepareData(third.string(),cache.string(),location,error,[&](const auto& p){if(p.item=="Selecting game resources")fs::permissions(cache,fs::perms::owner_read|fs::perms::owner_exec);return true;});fs::permissions(cache,fs::perms::owner_all);assert(!ok&&location.directory.empty()&&text(cache/"current.txt")==beforeThird);}
 assert(vyruz::prepareData(third.string(),cache.string(),location,error));noStages(cache);assert(!fs::exists(cache/".receipt-aaa-789")&&!fs::exists(cache/".selected-aaa-789"));
 // A second live launcher may not clean another launcher's staging.
 const auto concurrent=root/"concurrent";bool tested=false;
 assert(vyruz::prepareData(second.string(),concurrent.string(),location,error,[&](const auto& p){if(!tested&&p.stage==vyruz::ImportStage::Resources){tested=true;auto child=fork();assert(child>=0);if(!child){vyruz::DataLocation other;std::string e;bool ok=vyruz::prepareData(second.string(),concurrent.string(),other,e);_exit(!ok&&e.find("another launcher")!=std::string::npos?0:1);}childExit(child,0);}return true;}));assert(tested);noStages(concurrent);
 // Corrupt media must not publish over a previous selection. Truncation,
 // malformed directory/name/extent, missing archive, and corrupt RSC header.
 for(int fault=0;fault<7;++fault){auto bytes=iso(true);switch(fault){
  case 0:bytes.resize(17*2048);break;
  case 1:bytes[20*2048]=2;break;
  case 2:bytes[21*2048+32]=255;break;
  case 3:put(bytes,21*2048+2,UINT32_MAX);break;
  case 4:bytes[21*2048+33]='X';break;
  case 5:bytes[22*2048+28]^=1;break;
  case 6:put(bytes,22*2048+4,0);break;
 }const auto bad=root/("bad-"+std::to_string(fault)+".iso");write(bad,bytes);const auto before=text(cache/"current.txt");assert(!vyruz::prepareData(bad.string(),cache.string(),location,error)&&location.directory.empty());assert(text(cache/"current.txt")==before);noStages(cache);}
 assert(fs::exists(source)&&fs::exists(second)&&vyruz::verifyImportedData(original,error));fs::remove_all(root);
 std::cout<<"PASS import crash/retry, concurrency, ENOSPC estimate/fsync, real write failure, permissions, integrity and media corruption controls\n";
}
