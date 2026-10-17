#pragma once
#include <filesystem>
#include <string>

namespace vyruz {
// Copy an older user-folder file only when the portable file is absent.
// Never overwrite portable progress or remove the original file.
inline bool importLegacyUserFile(const std::filesystem::path& source,
                                 const std::filesystem::path& destination,
                                 std::string& error) {
    error.clear();
    std::error_code ec;
    if(std::filesystem::exists(destination,ec))return true;
    if(ec){error=ec.message();return false;}
    if(!std::filesystem::exists(source,ec)) {
        if(ec){error=ec.message();return false;}
        return true;
    }
    std::filesystem::copy_file(source,destination,std::filesystem::copy_options::none,ec);
    if(ec){error=ec.message();return false;}
    return true;
}
}
