#pragma once
#include "game/save_state.h"
#include <span>
namespace vyruz {
// Port-only, versioned format. Both decode and encode leave output unchanged on error.
bool encodeSave(const SaveGame&,std::vector<uint8_t>&,std::string& error);
bool decodeSave(std::span<const uint8_t>,SaveGame&,std::string& error);
bool writeSaveFile(const std::string& path,const SaveGame&,std::string& error);
bool readSaveFile(const std::string& path,SaveGame&,std::string& error);
uint64_t catalogFingerprint(const SceneContent&);
bool validSaveLevelPath(const std::string&);
}
