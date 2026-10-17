#pragma once
#include <filesystem>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#include <process.h>
inline int getpid(){return _getpid();}
#else
#include <unistd.h>
#endif
inline std::filesystem::path hostTempDirectory(const char* prefix){
    auto root=std::filesystem::temp_directory_path();
    for(int i=0;i<100;++i){auto path=root/(std::string(prefix)+std::to_string(getpid())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));if(std::filesystem::create_directory(path))return path;}
    throw std::runtime_error("Cannot create isolated test directory");
}
