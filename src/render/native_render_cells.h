#pragma once
#include "game/native_actor_cells.h"
#include <algorithm>
#include <cstdint>

namespace vyruz {
struct NativeRenderWindow { int left=0,top=0,right=0,bottom=0; };
//433b4f..433bcd: integer translation, 64x32 cells, original padding.
inline NativeRenderWindow nativeRenderWindow(int32_t x,int32_t y,uint32_t width,
        uint32_t height,int mapWidth,int mapHeight) {
    const auto first=[](int32_t translation,unsigned shift,int margin) {
        return translation>0?0:std::max(0,int((uint32_t(0)-uint32_t(translation))>>shift)-margin);
    };
    NativeRenderWindow w;w.left=first(x,6,1);w.top=first(y,5,2);
    w.right=std::min(mapWidth,int(width>>6)+3+w.left);
    w.bottom=std::min(mapHeight,int(height>>5)+5+w.top);return w;
}
template<class Visit> void visitNativeRenderCells(const NativeActorCells& cells,
        NativeRenderWindow w,Visit&& visit) {
    //433c02..433c70: Y major, X minor, then the existing head/next chain.
    for(int y=w.top;y<w.bottom;++y)for(int x=w.left;x<w.right;++x) {
        const size_t cell=size_t(x)*size_t(cells.height)+size_t(y);
        if(cell>=cells.heads.size())continue;
        for(size_t id=cells.heads[cell];id!=NativeActorCells::Missing;) {
            if(id>=cells.next.size())break;
            const auto next=cells.next[id];visit(id);id=next;
        }
    }
}
}
