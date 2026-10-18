#include "render/tile_atlas.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
int main(){
    // Exhaust all stored words, including bit15 and non-magenta keys. This
    // independent old expansion is the pixel oracle, not GafBitmap::pixel().
    for(uint16_t key:{uint16_t(0),uint16_t(0x7c1f),uint16_t(0xffff)}){
        vyruz::GafBitmap bitmap;bitmap.width=256;bitmap.height=256;bitmap.colorKey=key;
        bitmap.rgb555.resize(65536);for(size_t i=0;i<65536;++i)bitmap.rgb555[i]=uint16_t(i);
        const auto image=bitmap.rgbaImage();assert(image.rgba.size()==65536*4);
        for(unsigned v=0;v<65536;++v){assert(image.rgba[v*4]==((v>>10)&31)*255/31);
            assert(image.rgba[v*4+1]==((v>>5)&31)*255/31);assert(image.rgba[v*4+2]==(v&31)*255/31);
            assert(image.rgba[v*4+3]==(v==key?0:255));}
    }
    std::vector<vyruz::GafBitmap> packed(5);std::vector<vyruz::Image> expanded;
    for(size_t i=0;i<packed.size();++i){auto& b=packed[i];b.width=64;b.height=32;b.rgb555.resize(2048);
        for(size_t n=0;n<2048;++n)b.rgb555[n]=n%7?uint16_t(n*37+i*13):b.colorKey;
        expanded.push_back(b.rgbaImage());}
    assert(vyruz::paddedTileAtlas(packed,2).rgba==vyruz::paddedTileAtlas(expanded,2).rgba);
    vyruz::GafBitmap invalid;std::vector<uint8_t> scratch{1,2,3};
    assert(!invalid.expand(scratch)&&scratch.empty());invalid.width=2;invalid.height=1;invalid.rgb555={0};
    assert(!invalid.expand(scratch)&&scratch.empty());

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
