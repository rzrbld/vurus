#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace vyruz {
struct SpriteMip { int width,height;std::vector<uint8_t> rgba; };
inline std::vector<SpriteMip> filteredSpriteMips(const uint8_t* source,int width,int height) {
    std::vector<SpriteMip> levels{{width,height,{source,source+size_t(width)*height*4}}};
    auto& pixels=levels[0].rgba;
    // Color-key pixels contain magenta/black, not meaningful edge colors.
    // Extend visible RGB into transparent pixels without changing their alpha.
    std::vector<int> queue;queue.reserve(size_t(width)*height);
    std::vector<bool> visited(size_t(width)*height);
    for(int i=0;i<width*height;++i)if(pixels[size_t(i)*4+3]){queue.push_back(i);visited[i]=true;}
    for(size_t head=0;head<queue.size();++head) {
        const int i=queue[head],x=i%width,y=i/width;
        const int neighbors[]={x?i-1:-1,x+1<width?i+1:-1,y?i-width:-1,y+1<height?i+width:-1};
        for(int n:neighbors)if(n>=0&&!visited[n]) {
            for(int c=0;c<3;++c)pixels[size_t(n)*4+c]=pixels[size_t(i)*4+c];
            visited[n]=true;queue.push_back(n);
        }
    }
    while(levels.back().width>1 || levels.back().height>1) {
        const auto& previous=levels.back();
        SpriteMip next{std::max(1,previous.width/2),std::max(1,previous.height/2),{}};
        next.rgba.resize(size_t(next.width)*next.height*4);
        for(int y=0;y<next.height;++y)for(int x=0;x<next.width;++x) {
            unsigned alpha=0,count=0,weighted[3]{},plain[3]{};
            for(int sy=y*previous.height/next.height;sy<(y+1)*previous.height/next.height;++sy)
                for(int sx=x*previous.width/next.width;sx<(x+1)*previous.width/next.width;++sx) {
                    const auto* p=&previous.rgba[(size_t(sy)*previous.width+sx)*4];
                    alpha+=p[3];++count;
                    for(int c=0;c<3;++c){weighted[c]+=unsigned(p[c])*p[3];plain[c]+=p[c];}
                }
            auto* p=&next.rgba[(size_t(y)*next.width+x)*4];
            for(int c=0;c<3;++c)p[c]=alpha?weighted[c]/alpha:plain[c]/count;
            p[3]=(alpha+count/2)/count;
        }
        levels.push_back(std::move(next));
    }
    return levels;
}
}
