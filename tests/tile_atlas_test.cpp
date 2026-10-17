#include "render/tile_atlas.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
int main(){
    std::vector<vyruz::Image> tiles(5);
    for(size_t i=0;i<tiles.size();++i){
        auto& tile=tiles[i];tile.width=64;tile.height=32;tile.rgba.resize(64*32*4);
        for(size_t n=0;n<tile.rgba.size();++n)tile.rgba[n]=(n*37+i*13)%256;
    }
    const auto atlas=vyruz::paddedTileAtlas(tiles,2);
    assert(atlas.width==132&&atlas.height==102);
    // Every interior pixel and alpha is unchanged; each gutter/corner matches
    // its own tile edge, including transparent keyed edges, never its neighbor.
    for(int i=0;i<5;++i)for(int y=-1;y<=32;++y)for(int x=-1;x<=64;++x){
        const size_t source=(std::clamp(y,0,31)*64+std::clamp(x,0,63))*4;
        const size_t dest=((vyruz::tileAtlasY(i,2)+y)*atlas.width+vyruz::tileAtlasX(i,2)+x)*4;
        for(int c=0;c<4;++c)assert(atlas.rgba[dest+c]==tiles[i].rgba[source+c]);
    }
}
