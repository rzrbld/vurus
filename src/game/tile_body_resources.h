#pragma once
#include "game/tile_body_native.h"
#include <functional>
namespace vyruz {
using NativeTileResourceReader=std::function<bool(const std::string&,std::vector<std::uint8_t>&)>;
// Resolves the original current-world rebasing and named GAF readiness. This
// is loading work only; no file reads or animation decodes occur during ticks.
bool loadNativeTileBodyResources(NativeTileBodyCatalog&,const std::string& mapPath,
                                 const NativeTileResourceReader&,std::string& error);
}
