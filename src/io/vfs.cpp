#include "io/vfs.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>

namespace vyruz {

namespace {

bool readLoose(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const auto size = f.tellg();
    if (size < 0 || size>256*1024*1024) return false;
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if(size && !f.read(reinterpret_cast<char*>(bytes.data()),size))return false;
    out=std::move(bytes);return true;
}

} // namespace

std::string Vfs::normalize(const std::string& gamePath) {
    std::string p = gamePath;
    std::transform(p.begin(), p.end(), p.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    std::replace(p.begin(), p.end(), '/', '\\');

    // Strip leading "..\"
    if (p.rfind("..\\", 0) == 0) p.erase(0, 3);

    // The original sometimes requests "Data\..." without the "Common\" prefix.
    if (p.rfind("DATA\\", 0) == 0) p = "COMMON\\" + p;

    return p;
}

bool Vfs::init(const std::string& dataDir) {
    *this=Vfs{};dataDir_=dataDir;
    namespace fs=std::filesystem;
    try {
        if(!fs::is_directory(dataDir)){error_="Not a game-data directory: "+dataDir;return false;}
        for(const auto& entry:fs::recursive_directory_iterator(dataDir)) {
            if(!entry.is_regular_file())continue;
            const auto relative=entry.path().lexically_relative(dataDir).generic_string();
            const auto key=normalize(relative);
            if(!loose_.emplace(key,entry.path().string()).second){error_="Ambiguous case-insensitive resource: "+relative;return false;}
        }
        for(auto [name,archive]:{std::pair{"VZ-DATA.RSC",&data_},std::pair{"VZ-SLIDE.RSC",&slides_}}){
            const auto found=loose_.find(name);
            if(found!=loose_.end() && !archive->open(found->second)){
                error_="Invalid or truncated archive: "+found->second+". Restore it from your original disc image.";return false;
            }
        }
        rscLoaded_=hasRsc();
        if(!rscLoaded_ && !loose_.contains("UNDERGROUND\\T1U1.3DG")){
            error_="No Vyruz resources in "+dataDir+". Supply the directory containing vz-data.rsc, or an extracted resource tree.";return false;
        }
        return true;
    }catch(const fs::filesystem_error& e){error_="Cannot access game data: "+std::string(e.what());return false;}
}

bool Vfs::read(const std::string& gamePath, std::vector<uint8_t>& out) const {
    const std::string key = normalize(gamePath);

    out.clear();
    // Reject traversal after the single legacy ../ prefix was normalized.
    if(key.empty() || key.front()=='\\' || key.find(':')!=std::string::npos)return false;
    size_t begin=0;
    while(begin<key.size()){
        const auto end=key.find('\\',begin);
        const auto part=key.substr(begin,end==std::string::npos?end:end-begin);
        if(part==".." || part==".")return false;
        if(end==std::string::npos)break;
        begin=end+1;
    }
    const auto file=loose_.find(key);
    if(file!=loose_.end())return readLoose(file->second,out);
    try {
    // 2) rsc archives
    int idx = data_.find(key);
    if (idx >= 0) {
        out = data_.read(static_cast<uint32_t>(idx));
        return true;
    }
    idx = slides_.find(key);
    if (idx >= 0) {
        out = slides_.read(static_cast<uint32_t>(idx));
        return true;
    }

    }catch(const std::exception&){out.clear();}
    return false;
}

} // namespace vyruz
