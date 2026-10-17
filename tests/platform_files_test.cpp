#include "platform/files.h"
#include "host_temp.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace vyruz;
static void need(bool good,const char* message){if(!good)throw std::runtime_error(message);}
static std::vector<uint8_t> read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
int main(){try{
    const auto root=hostTempDirectory("vurus-platform-files-");const auto path=root/"state";std::string error;
    const std::vector<uint8_t> first{0,1,2,255},second(1024*1024+17,42);
    need(platform::atomicWrite(path,first,error)&&read(path)==first,"initial binary publication");
    need(platform::atomicWrite(path,second,error)&&read(path)==second,"complete replacement across write chunks");
    need(platform::atomicWrite(path,{},error)&&read(path).empty(),"empty replacement");
    const auto directory=root/"directory";std::filesystem::create_directory(directory);
    {std::ofstream marker(directory/"keep");marker<<"keep";}
    need(!platform::atomicWrite(directory,first,error)&&!error.empty(),"cannot replace directory with file");
    need(std::filesystem::exists(directory/"keep"),"failed replacement preserves directory");
    need(!platform::atomicWrite(root/"missing"/"state",first,error),"missing parent fails safely");
    need(platform::atomicWrite(path,first,error),"restore original for rename test");
    need(platform::atomicWrite(root/"replacement",second,error),"stage replacement");
    need(platform::replaceFile(root/"replacement",path,error)&&read(path)==second,"receipt replacement overwrites existing file");
    for(const auto& e:std::filesystem::directory_iterator(root))need(e.path().filename().string().find(".tmp.")==std::string::npos,"no leaked temporary files");
    std::filesystem::remove_all(root);std::cout<<"PASS host atomic files and replacement failure preservation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
