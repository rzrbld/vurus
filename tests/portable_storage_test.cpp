#include "io/portable_storage.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const fs::path& path){std::ifstream file(path);return {std::istreambuf_iterator<char>(file),{}};}
int main(){try{
    const auto root=fs::temp_directory_path()/("vurus-storage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);const auto old=root/"legacy",next=root/"portable";std::string error;
    check(vyruz::importLegacyUserFile(old,next,error)&&!fs::exists(next),"missing legacy creates nothing");
    {std::ofstream file(old);file<<"saved progress";}
    check(vyruz::importLegacyUserFile(old,next,error),"first import succeeds");
    check(read(old)=="saved progress"&&read(next)==read(old),"copy preserves original bytes");
    {std::ofstream file(next);file<<"newer progress";}
    check(vyruz::importLegacyUserFile(old,next,error)&&read(next)=="newer progress","never replace portable progress");
    check(!vyruz::importLegacyUserFile(old,root/"missing-directory"/"file",error)&&!error.empty(),"copy failure reported");
    check(read(old)=="saved progress","failure preserves original");
    fs::remove_all(root);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
