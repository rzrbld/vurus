#pragma once
#include "game/save_file.h"
#include <filesystem>
namespace vyruz {
struct SaveCatalogEntry {
    std::filesystem::path path;
    uint64_t number=0; // native sessions begin at zero; use legacy to identify fixed slot
    bool legacy=false;
    std::string label,error,map;
    int difficulty=1;
    double elapsedMs=0;
    float energy=0,shield=0;
    int64_t money=0;
    bool valid() const { return error.empty(); }
};
// Byte-hex encoding is reversible and cannot introduce a path separator.
std::filesystem::path saveHistoryDirectory(const std::filesystem::path& baseSlot,const std::string& profile);
//Separate native default/retry record; excluded from numbered Load Game rows.
std::filesystem::path defaultRetryPath(const std::filesystem::path& baseSlot,const std::string& profile);
bool writeDefaultRetry(const std::filesystem::path& baseSlot,const std::string& profile,const SaveGame&,std::string& error);
bool readDefaultRetry(const std::filesystem::path& baseSlot,const std::string& profile,SaveGame&,std::string& error);
// Snapshot once when opening the browser; no retained handles or per-frame I/O.
std::vector<SaveCatalogEntry> listSaveCatalog(const std::filesystem::path& baseSlot,const std::string& profile);
// Publish a new unique numbered file before retiring old numbered history.
// Legacy/manual files and symlinks are never replaced or pruned.
std::optional<std::filesystem::path> writeHistorySave(const std::filesystem::path& baseSlot,
    const std::string& profile,const SaveGame&,std::string& error);
std::optional<std::filesystem::path> writeHistorySave(const std::filesystem::path& baseSlot,
    const std::string& profile,SaveGame&&,std::string& error);

}
