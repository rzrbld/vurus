#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>

#include "io/rsc.h"

namespace vyruz {

// Resolves game asset paths against either a loose extracted tree or the
// original .rsc archives, matching the original engine's path resolution.
class Vfs {
public:
    // dataDir may contain either extracted files or VZ-DATA.RSC / VZ-SLIDE.RSC.
    bool init(const std::string& dataDir);

    // Read a game path (e.g. "Common\\Data\\Menu\\menu.gaf" or
    // "..\\Common\\Data\\Extra\\story.tga"). Returns false if not found.
    bool read(const std::string& gamePath, std::vector<uint8_t>& out) const;

    // Original/imported Intro.avi is a loose media file; expose its resolved
    // case-insensitive path for bounded seeking instead of whole-file read().
    std::string loosePath(const std::string& gamePath)const {
        const auto found=loose_.find(normalize(gamePath));return found==loose_.end()?std::string{}:found->second;
    }
    const std::string& error() const {return error_;}
    bool hasRsc() const { return data_.entryCount() > 0 || slides_.entryCount() > 0; }

private:
    std::string dataDir_,error_;
    std::map<std::string,std::string> loose_;
    RscArchive data_;
    RscArchive slides_;
    bool rscLoaded_ = false;

    static std::string normalize(const std::string& gamePath);
};

} // namespace vyruz
