#include "game/save_catalog.h"
#include <cstdlib>
#include <cstdarg>
#include <cerrno>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <fstream>
#include <iostream>
#include "host_temp.h"
static bool forceRenameFallback=false;
#ifdef __linux__
extern "C" long __real_syscall(long,...);
extern "C" long __wrap_syscall(long operation,...){
    if(operation!=SYS_renameat2){errno=ENOSYS;return -1;}
    va_list args;va_start(args,operation);int from=va_arg(args,int);const char* oldName=va_arg(args,const char*);
    int to=va_arg(args,int);const char* newName=va_arg(args,const char*);int flags=va_arg(args,int);va_end(args);
    if(forceRenameFallback){errno=EOPNOTSUPP;return -1;}
    return __real_syscall(operation,from,oldName,to,newName,flags);
}
#endif
using namespace vyruz;
namespace fs=std::filesystem;
static void check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
int main(){
    const fs::path root=hostTempDirectory("vyruz-save-catalog-"),base=root/"slot.vzs";
    SaveGame save;save.currentLevel="LEVEL/CAVE/CAVE.3DG";auto& world=save.levels[save.currentLevel].world;
    world.actors.emplace_back();world.objects.emplace_back();world.elapsedMs=123456;world.difficulty=3;
    world.actors[0].vitals.energy=72.5f;world.actors[0].vitals.shield=19;world.actors[0].vitals.money=3456;
    std::string error;check(writeSaveFile(base.string(),save,error),error.c_str());
    check(writeSaveFile((root/"manual.vzs").string(),save,error),error.c_str());
    auto legacy=listSaveCatalog(base,"Pilot");check(legacy.size()==1&&legacy[0].legacy&&legacy[0].valid(),"explicit legacy only, no root manual files");
    for(int i=0;i<9;++i){auto path=writeHistorySave(base,"Pilot",save,error);check(bool(path),error.c_str());check(path->filename()==("s00"+std::to_string(i)+".vzs"),"monotonic session names");}
    auto entries=listSaveCatalog(base,"Pilot");check(entries.size()==8,"seven histories plus legacy");
    for(size_t i=0;i<7;++i)check(entries[i].number==8-i&&!entries[i].legacy&&entries[i].valid(),"newest first with seven retained");
    const auto& newest=entries[0];check(newest.map==save.currentLevel&&newest.difficulty==3&&newest.elapsedMs==123456&&newest.energy==72.5f&&newest.shield==19&&newest.money==3456,"decoded metadata");
    check(entries.back().legacy&&fs::exists(base)&&fs::exists(root/"manual.vzs"),"legacy and manual untouched");
    const auto folder=saveHistoryDirectory(base,"Pilot");check(!fs::exists(folder/"s000.vzs")&&!fs::exists(folder/"s001.vzs"),"old sessions pruned only after new publication");
    {std::ofstream preview(folder/"s002.tga");preview<<"retire";}
    {std::ofstream preview(folder/"s003.tga");preview<<"retire";}
    fs::create_symlink(base,folder/"s004.tga");
    {std::ofstream preview(folder/"manual.tga");preview<<"keep";}
    {std::ofstream bad(folder/"s010.vzs");bad<<"corrupt";}
    {std::ofstream manual(folder/"s0001.vzs");manual<<"manual";}
    fs::create_symlink(base,folder/"s011.vzs");fs::create_directory(folder/"s012.vzs");
    entries=listSaveCatalog(base,"Pilot");check(entries.front().number==10&&!entries.front().valid()&&!entries.front().error.empty()&&!entries.front().label.empty(),"corrupt history remains visible");
    check(entries.size()==8 && entries.back().legacy,"browser admits seven numbered records plus legacy");
    for(const auto& entry:entries)check(entry.legacy || (entry.number!=11 && entry.number!=12),"symlinks and directories excluded");
    auto afterCollision=writeHistorySave(base,"Pilot",save,error);check(afterCollision&&afterCollision->filename()=="s013.vzs","all numbered collisions reserve their names");
    check(fs::is_symlink(folder/"s011.vzs")&&fs::is_directory(folder/"s012.vzs")&&fs::exists(folder/"s0001.vzs"),"collision and manual paths never overwritten or pruned");
    check(!fs::exists(folder/"s002.tga")&&!fs::exists(folder/"s003.tga")&&fs::is_symlink(folder/"s004.tga")&&fs::exists(folder/"manual.tga"),"retention removes only matching regular previews");
    {std::ofstream high(folder/"s1000.vzs");high<<"corrupt";}
    {std::ofstream low(folder/"s999.vzs");low<<"corrupt";}
    entries=listSaveCatalog(base,"Pilot");check(entries.front().number==999,"native descending lexicographic path order beyond three digits");
    auto other=writeHistorySave(base,"Other",save,error);check(other&&other->filename()=="s000.vzs","profiles have independent histories");
    forceRenameFallback=true;
    auto fallback=writeHistorySave(base,"Fallback",save,error);check(fallback&&fallback->filename()=="s000.vzs","exclusive publication works (forced Linux rename fallback where applicable)");
    SaveGame fallbackRead;check(readSaveFile(fallback->string(),fallbackRead,error)&&fallbackRead.currentLevel==save.currentLevel,"fallback writes a complete readable save");
    forceRenameFallback=false;
    auto traversal=writeHistorySave(base,"../../Other/\\",save,error);check(bool(traversal),error.c_str());
    check(traversal->parent_path().parent_path()==root/"saves"&&traversal->parent_path()!=other->parent_path(),"profile byte encoding cannot escape or alias");
    auto escaped=writeHistorySave(base,std::string("A\0B",3),save,error);check(escaped&&escaped->parent_path()!=saveHistoryDirectory(base,"A"),"embedded NUL profile safely encoded");
    auto before=listSaveCatalog(base,"Pilot");auto invalid=save;invalid.currentLevel="bad";
    check(!writeHistorySave(base,"Pilot",invalid,error)&&!error.empty(),"invalid payload write fails");
    check(listSaveCatalog(base,"Pilot").size()==before.size()&&fs::exists(*afterCollision),"failed encode retains old files");
    check(!writeHistorySave(root/"missing"/"slot.vzs","Pilot",save,error),"missing base parent fails safely");
    fs::create_directory(root/"symlink-case");fs::create_symlink(root/"saves",root/"symlink-case"/"saves");
    check(!writeHistorySave(root/"symlink-case"/"slot.vzs","Pilot",save,error),"history directory symlink is not followed");
    fs::create_symlink(other->parent_path(),root/"saves"/"p-4c696e6b");
    check(!writeHistorySave(base,"Link",save,error),"profile directory symlink is not followed");
    // Force the numbering failure after valid existing saves, before publication.
    {std::ofstream exhausted(folder/"s18446744073709551615.vzs");exhausted<<"reserved";}
    check(!writeHistorySave(base,"Pilot",save,error)&&fs::exists(*afterCollision)&&fs::exists(base),"exhausted numbering preserves all older files");
    for(const auto& entry:fs::directory_iterator(folder))check(!entry.path().filename().string().starts_with(".pending-"),"no pending file leaks");
    SaveGame retry;check(!readDefaultRetry(base,"Retry",retry,error),"fresh profile has no default retry");
    check(writeDefaultRetry(base,"Retry",save,error)&&readDefaultRetry(base,"Retry",retry,error),"independent atomic default retry round trip");
    check(retry.currentLevel==save.currentLevel&&retry.levels.at(save.currentLevel).world.elapsedMs==123456,"retry entry metadata retained");
    check(listSaveCatalog(base,"Retry").size()==1,"default retry is absent from numbered save browser");
    auto retryInvalid=save;retryInvalid.currentLevel="bad";check(!writeDefaultRetry(base,"Retry",retryInvalid,error),"invalid retry cannot replace existing record");
    check(readDefaultRetry(base,"Retry",retry,error)&&retry.currentLevel==save.currentLevel,"failed replacement preserves default");
    check(!readDefaultRetry(base,"OtherMissing",retry,error),"retry profile isolation");
    fs::create_directory(saveHistoryDirectory(base,"RetryLink"));fs::create_symlink(base,defaultRetryPath(base,"RetryLink"));
    check(!readDefaultRetry(base,"RetryLink",retry,error)&&!writeDefaultRetry(base,"RetryLink",save,error)&&fs::is_symlink(defaultRetryPath(base,"RetryLink")),"retry symlinks neither read nor replaced");
    check(!writeDefaultRetry(root/"symlink-case"/"slot.vzs","Retry",save,error),"retry refuses parent history symlink");
    fs::remove_all(root);std::cout<<"Save catalog history, metadata, isolation and failure checks passed\n";
}
