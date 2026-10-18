#include "render/level_map.h"
#include <algorithm>
#include <cmath>
namespace vyruz {
namespace {
//46db00 software stretch lookup: source span minus the stored float epsilon,
//converted to16.16, then divided by the inclusive destination span.
int sample(int pixel,int source,int destination) {
    if(destination<=1)return 0;
    const auto span=static_cast<int64_t>((static_cast<long double>(source)-static_cast<float>(.0001))*65536);
    return std::clamp(int((int64_t(pixel)*(span/(destination-1)))>>16),0,source-1);
}
}
Image makeLevelMap(const Level& level,int width,int height) {
    if(width<=0||height<=0||width>4096||height>4096||level.mapW<=0||level.mapH<=0||level.tileW<=0||level.tileH<=0)return {};
    const int64_t worldWidth=int64_t(level.mapW)*level.tileW,worldHeight=int64_t(level.mapH)*level.tileH;
    Image out;out.width=width;out.height=height;out.rgba.resize(static_cast<size_t>(width)*height*4);
    //46b140 projects each cell's inclusive endpoints, fills upscale gaps, then
    //draws each layer in authored order. It is not one global nearest lookup.
    for(const auto& layer:level.layers) {
        int previousBottom=-1;
        for(int cy=0;cy<level.mapH;++cy) {
            const int top=std::min(previousBottom+1,int(int64_t(cy)*level.tileH*(height-1)/std::max<int64_t>(1,worldHeight-1)));
            const int bottom=int((int64_t(cy+1)*level.tileH-1)*(height-1)/std::max<int64_t>(1,worldHeight-1));
            previousBottom=bottom;int previousRight=-1;
            for(int cx=0;cx<level.mapW;++cx) {
                const int left=std::min(previousRight+1,int(int64_t(cx)*level.tileW*(width-1)/std::max<int64_t>(1,worldWidth-1)));
                const int right=int((int64_t(cx+1)*level.tileW-1)*(width-1)/std::max<int64_t>(1,worldWidth-1));
                previousRight=right;
                const size_t cell=size_t(cy)*level.mapW+cx;
                if(cell>=layer.size() || !layer[cell] || layer[cell]>level.tiles.size())continue;
                const auto& tile=level.tiles[layer[cell]-1];
                if(!tile.valid())continue;
                for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x) {
                    const size_t pixel=(size_t(sample(y-top,tile.height,bottom-top+1))*tile.width+sample(x-left,tile.width,right-left+1));
                    const auto color=tile.pixel(pixel);if(!color[3])continue;
                    auto* destination=out.rgba.data()+(size_t(y)*width+x)*4;
                    std::copy_n(color.data(),3,destination);
                    destination[3]=255;
                }
            }
        }
    }
    //436460 selects keyed mode1 with the new bitmap's default black key.
    //Source-key rejection precedes its0x000800 specular green addition.
    for(size_t p=0;p<out.rgba.size();p+=4) {
        if(!out.rgba[p] && !out.rgba[p+1] && !out.rgba[p+2])out.rgba[p+3]=0;
        else out.rgba[p+1]=static_cast<uint8_t>(std::min(255,int(out.rgba[p+1])+8));
    }
    return out;
}
}
