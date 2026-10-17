#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vyruz {
struct NativeTileBodySource {
    std::uint32_t kind=0,subtype=0,flags=0;
    std::string resource,animation;
    // Derived resource binding, outside the persisted native pool layout.
    std::string resolvedResource;
    std::uint32_t sequence=0; // NAM_ one-based ANM_ index.
};
struct NativeTileBodyPlacement {
    std::uint32_t x=0,y=0,slot=0,source=0;
    friend bool operator==(const NativeTileBodyPlacement&,const NativeTileBodyPlacement&)=default;
};
struct NativeTileBodyCatalog {
    std::uint32_t width=0,height=0;
    std::vector<NativeTileBodySource> sources; // Index0 is the native empty reference.
    // Original offsets0,2,4,6; distinct from Level's collision mask slot order.
    std::vector<std::array<std::uint16_t,4>> cells; // Port row-major storage only.
};
// Parses resource references and instance overrides, without pretending they
// have successfully loaded. The resource owner marks each loaded Anim source's
// flags bit1 before admission, as41f0b0 does after GAF/animation lookup.
bool decodeNativeTileBodyCatalog(std::span<const std::uint8_t> instances,
                                std::string_view descriptions,NativeTileBodyCatalog& out,
                                std::string& error);
// 433930: X-major ascending, Y ascending, each cell offsets0,2,4,6.
// A callback false is the original factory failure early-return boundary.
template<class Factory> bool visitNativeTileBodyPlacements(const NativeTileBodyCatalog& catalog,Factory&& factory) {
    if(catalog.cells.size()!=std::size_t(catalog.width)*catalog.height)return false;
    for(std::uint32_t x=0;x<catalog.width;++x)for(std::uint32_t y=0;y<catalog.height;++y)
        for(std::uint32_t slot=0;slot<4;++slot) {
            const auto source=catalog.cells[std::size_t(y)*catalog.width+x][slot];
            if(!source || source>=catalog.sources.size())continue;
            const auto& definition=catalog.sources[source];
            if(definition.kind==1 && (definition.flags&1) && !factory(NativeTileBodyPlacement{x,y,slot,source}))return false;
        }
    return true;
}
}
