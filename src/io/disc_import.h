#pragma once
#include <string>
#include <functional>
#include <cstdint>
namespace vyruz {
// imported also reports successful movie backfill into an older import.
struct DataLocation {std::string directory;bool imported=false;};
enum class ImportStage { Checking, Resources, Music, Finishing };
struct ImportProgress {
    ImportStage stage=ImportStage::Checking;
    std::string item;
    uint64_t completed=0,total=0;
    // Full import work: copied payload plus verification reads. Zero means the
    // total is not yet known (discovery, cached launch or intro-only backfill).
    uint64_t overallCompleted=0,overallTotal=0;
};
// Called on the importer thread; return false to cancel before publication.
using ImportProgressCallback=std::function<bool(const ImportProgress&)>;
// Directory, ISO, or single-BIN CUE. All outputs live below cacheRoot; the
// source is read-only. Relative selection survives deleting the disc and moving
// the executable together with its cacheRoot (normally executableDirectory/data).
bool prepareData(const std::string& input,const std::string& cacheRoot,
                 DataLocation& result,std::string& error,
                 const ImportProgressCallback& progress={});
// Explicit full checksum verification for new imports; legacy trees have no record.
bool verifyImportedData(const std::string& directory,std::string& error,
                        const ImportProgressCallback& progress={});
std::string defaultImportRoot(const std::string& executableDirectory);
}
