#include "render/occlusion.h"
#include "game/level.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace vyruz {
bool coveredPlayerCenter(const Level& level, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 ||
        level.tileW<=0 || level.tileH<=0 || x>=double(level.mapW)*level.tileW ||
        y>=double(level.mapH)*level.tileH) return false;
    const size_t cell=size_t(y/level.tileH)*level.mapW+size_t(x/level.tileW);
    bool base=false;
    for(size_t layer=0;layer<std::min(size_t(2),level.layers.size());++layer)
        base|=cell<level.layers[layer].size() && level.layers[layer][cell]!=0;
    for(size_t layer=2;layer<level.layers.size();++layer) {
        if(cell>=level.layers[layer].size())continue;
        const auto index=level.layers[layer][cell];
        if(!index || index>level.tiles.size() || index>level.blockFlags.size())continue;
        const auto phase=level.blockFlags[index-1];
        if(phase<2 || phase>INT32_MAX)continue;
        const auto flags=index<=level.tileDrawFlags.size()?level.tileDrawFlags[index-1]:0;
        if(!(flags&1) || !base)return true;
    }
    return false;
}
std::vector<ForegroundTile> foregroundTiles(const Level& level, float groundY,
                                           float left, float top,
                                           float right, float bottom) {
    std::vector<ForegroundTile> result;
    if (!(left < right && top < bottom) || level.mapW <= 0 || level.mapH <= 0)
        return result;
    const int firstX = std::max(0, static_cast<int>(std::floor((left) / 64)));
    const int lastX = std::min(level.mapW - 1, static_cast<int>(std::ceil((right) / 64)) - 1);
    const int firstY = std::max(0, static_cast<int>(std::floor((top) / 32)));
    const int lastY = std::min(level.mapH - 1, static_cast<int>(std::ceil((bottom) / 32)) - 1);
    const int bodyRow = static_cast<int>(std::floor(groundY / 32));
    for (int y = firstY; y <= lastY; ++y) {
        for (int x = firstX; x <= lastX; ++x) {
            const auto cell = static_cast<size_t>(y) * level.mapW + x;
            for (size_t layer = 2; layer < level.layers.size(); ++layer) {
                const auto& grid = level.layers[layer];
                if (cell >= grid.size()) break;
                const uint32_t index = grid[cell];
                if (!index || index > level.tiles.size() || index > level.blockFlags.size()) break;
                const uint32_t phase = level.blockFlags[index - 1];
                if (phase == UINT32_MAX) continue; // native signed -1
                if (phase > 4) break;
                if (y + static_cast<int>(phase) > bodyRow)
                    result.push_back({x, y, static_cast<int>(layer), index - 1,
                                      static_cast<int>(phase)});
            }
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return std::tuple(a.y + a.phase, -a.phase, a.x, a.layer) <
               std::tuple(b.y + b.phase, -b.phase, b.x, b.layer);
    });
    return result;
}
std::vector<CompositeCommand> composeObjects(const Level& level,
                                            const std::vector<SpriteBounds>& sprites,
                                            float viewLeft, float viewTop,
                                            float viewRight, float viewBottom) {
    std::vector<CompositeCommand> result;
    if (level.mapW <= 0 || level.mapH <= 0 || viewLeft >= viewRight || viewTop >= viewBottom) return result;
    const int x0 = std::clamp(static_cast<int>(std::floor((viewLeft)/64)),0,level.mapW-1);
    const int x1 = std::clamp(static_cast<int>(std::ceil((viewRight)/64))-1,0,level.mapW-1);
    const int y0 = std::clamp(static_cast<int>(std::floor((viewTop)/32)),0,level.mapH-1);
    const int y1 = std::clamp(static_cast<int>(std::ceil((viewBottom)/32))-1,0,level.mapH-1);
    std::vector<uint8_t> dirty(static_cast<size_t>(level.mapW)*level.mapH);
    struct Event { int row, stage, key; CompositeCommand draw; };
    std::vector<Event> events;
    for (size_t i=0;i<sprites.size();++i) {
        const auto& s=sprites[i];
        if (s.right<=viewLeft || s.left>=viewRight || s.bottom<=viewTop || s.top>=viewBottom ||
            s.left>=s.right || s.top>=s.bottom) continue;
        const int subtype=s.key&3;
        const int row=subtype==3 ? y0-1 : std::clamp(static_cast<int>(std::floor(s.key/128.0)),y0,y1+5);
        const int stage=subtype==1?1:subtype==2?2:3;
        events.push_back({row,stage,s.key,{true,i,{}}});
    }
    for (int phase=4;phase>=0;--phase) for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) {
        const auto cell=static_cast<size_t>(y)*level.mapW+x;
        for (size_t layer=2;layer<level.layers.size();++layer) {
            if (cell>=level.layers[layer].size()) break;
            const uint32_t index=level.layers[layer][cell];
            if (!index || index>level.tiles.size() || index>level.blockFlags.size()) break;
            const auto flag=level.blockFlags[index-1];
            if (flag==UINT32_MAX) continue;
            if (flag>static_cast<uint32_t>(phase)) break;
            if (flag==static_cast<uint32_t>(phase))
                events.push_back({y+phase,0,-phase,{false,0,{x,y,static_cast<int>(layer),index-1,phase}}});
        }
    }
    std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b) {
        return std::tie(a.row,a.stage,a.key)<std::tie(b.row,b.stage,b.key);
    });
    for (const auto& event:events) {
        const auto& command=event.draw;
        if (!command.isSprite) {
            if (dirty[static_cast<size_t>(command.tile.y)*level.mapW+command.tile.x]) result.push_back(command);
            continue;
        }
        result.push_back(command);
        const auto& s=sprites[command.sprite];
        const int sx0=std::max(x0,static_cast<int>(std::floor((s.left)/64)));
        const int sx1=std::min(x1,static_cast<int>(std::ceil((s.right)/64))-1);
        const int sy0=std::max(y0,static_cast<int>(std::floor((s.top)/32)));
        const int sy1=std::min(y1,static_cast<int>(std::ceil((s.bottom)/32))-1);
        for (int y=sy0;y<=sy1;++y) for (int x=sx0;x<=sx1;++x) dirty[static_cast<size_t>(y)*level.mapW+x]=1;
    }
    return result;
}
} // namespace vyruz
