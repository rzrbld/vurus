#pragma once
#include "assets/tga.h"
#include <algorithm>
#include <span>
namespace vyruz {
constexpr int tileAtlasPitchX=66,tileAtlasPitchY=34;
constexpr int tileAtlasX(int tile,int columns){return (tile%columns)*tileAtlasPitchX+1;}
constexpr int tileAtlasY(int tile,int columns){return (tile/columns)*tileAtlasPitchY+1;}
// One replicated texel around each64x32 tile prevents atlas-neighbor sampling
// at fractional camera positions. Interior pixels/alpha and geometry are exact.
inline Image paddedTileAtlas(std::span<const Image> tiles,int columns){
    Image atlas;atlas.width=columns*tileAtlasPitchX;
    atlas.height=std::max(1,(int(tiles.size())+columns-1)/columns)*tileAtlasPitchY;
    atlas.rgba.resize(size_t(atlas.width)*atlas.height*4);
    for(size_t i=0;i<tiles.size();++i){
        const auto& tile=tiles[i];if(tile.rgba.empty())continue;
        const int ox=tileAtlasX(int(i),columns),oy=tileAtlasY(int(i),columns);
        for(int y=-1;y<=32;++y)for(int x=-1;x<=64;++x){
            const size_t source=(size_t(std::clamp(y,0,tile.height-1))*tile.width+std::clamp(x,0,tile.width-1))*4;
            const size_t target=(size_t(oy+y)*atlas.width+ox+x)*4;
            std::copy_n(tile.rgba.data()+source,4,atlas.rgba.data()+target);
        }
    }
    return atlas;
}
}
