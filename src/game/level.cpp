#include "game/level.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>

#include "assets/gaf.h"
#include "assets/gob.h"

namespace vyruz {

namespace {

inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

std::string uppercase(std::string text) {
    for (char& ch : text) {
        if (ch == '/') ch = '\\';
        else ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

// Native terrain class: immediate parent directory, independent of bitmap name.
uint8_t classFromPath(const std::string& name) {
    const std::string path = uppercase(name);
    const auto end = path.find_last_of('\\');
    if (end == std::string::npos || end == 0) return 0;
    const auto start = path.find_last_of('\\', end - 1);
    const auto category = path.substr(start == std::string::npos ? 0 : start + 1,
                                      end - (start == std::string::npos ? 0 : start + 1));
    static const std::array<const char*, 16> names = {
        "", "BRIDGE", "CORNER", "GROUND", "GROUND+LEFTWALL", "GROUND+RIGHTWALL",
        "HALF-GROUND", "HALF-ROOF", "HORIZONTALWALL", "LEFTWALL", "OBSTACLE",
        "RIGHTWALL", "ROOF", "ROOF+", "WALLCORNER", "WATER"};
    for (size_t i = 1; i < names.size(); ++i)
        if (category == names[i]) return static_cast<uint8_t>(i);
    return 0;
}

uint8_t kindFromClass(uint8_t type) {
    switch (type) {
        case 2: return TerrainCorner;
        case 3: case 6: return TerrainGround;
        case 4: case 5: return TerrainGround | TerrainWall;
        case 7: case 12: case 13: return TerrainRoof;
        case 8: case 9: case 11: case 14: return TerrainWall;
        case 10: return TerrainObstacle;
        case 15: return TerrainWater;
        default: return 0;
    }
}

// Slot order is the mask-builder visit order (native byte offsets 0,6,2,4).
int slotFromClass(uint8_t type) {
    switch (type) {
        case 2: case 3: case 6: case 8: case 15: return 0;
        case 4: case 5: case 9: case 10: case 11: case 14: return 1;
        case 7: case 12: return 2;
        case 1: case 13: return 3;
        default: return -1;
    }
}

// Decode one 64x32 RGB555 BMP_ block (68-byte header + pixels) without expanding RGB555.
//
// The baked `.3DG` tiles are triangular half-faces (see docs/formats/render.md):
// opaque regions are encoded by the pixels' color key. Decode them as-is;
// there is no reflection or compositing here.
GafBitmap decodeTile(const uint8_t* p, size_t len) {
    GafBitmap img;
    if (len < 68 || std::memcmp(p, "BMP_", 4) != 0) return img;
    const int w = static_cast<int>(rd32(p + 32));
    const int h = static_cast<int>(rd32(p + 36));
    if (w != 64 || h != 32 || len < 68u + 64u * 32u * 2u) return img;
    img.width = w;
    img.height = h;
    img.rgb555.resize(64 * 32);
    const uint8_t* px = p + 68;
    for (size_t i = 0; i < 64 * 32; ++i) {
        img.rgb555[i] = rd16(px + i * 2);
    }
    return img;
}

} // namespace

bool loadLevel(const std::vector<uint8_t>& d3gData, Level& out) {
    GafChunk root;
    if (!parseGaf(d3gData, root)) return false;
    const GafChunk* head = root.findChild("HEAD");
    const GafChunk* data = root.findChild("DATA");
    if (!head || !data) return false;

    // Tile count from HEAD BLK_ descriptor.
    const GafChunk* headBlk = head->findChild("BLK_");
    if (!headBlk || headBlk->payload.size() < 4) return false;
    const int tileCount = static_cast<int>(rd32(headBlk->payload.data()));
    if (tileCount <= 0 || tileCount > 100000) return false;

    // Tile bitmaps from DATA BLK_ (a GOB_ payload).
    const GafChunk* dataBlk = data->findChild("BLK_");
    if (!dataBlk) return false;
    std::vector<std::vector<uint8_t>> blkComps;
    if (!decodeGobPayload(dataBlk->payload, blkComps) || blkComps.size() < 2) return false;
    const auto& tileStrip = blkComps[1]; // tileCount x (68 + 64*32*2)
    const size_t stride = 68 + 64 * 32 * 2; // 4164
    if (tileStrip.size() < static_cast<size_t>(tileCount) * stride) return false;

    // Map index 0 is empty; map index 1 references the first stored tile.
    out.tiles.resize(tileCount);
    out.tileDrawFlags.resize(tileCount);
    for (int i = 0; i < tileCount; ++i) {
        out.tileDrawFlags[i] = rd32(tileStrip.data() + static_cast<size_t>(i) * stride + 20);
        out.tiles[i] = decodeTile(tileStrip.data() + static_cast<size_t>(i) * stride, stride);
        if (out.tiles[i].rgb555.empty()) return false;
    }

    // Foreground row phases from the block descriptor table (16 bytes each,
    // phase in u32[3]); these do not determine collision.
    const auto& blockDesc = blkComps[0];
    out.blockFlags.resize(tileCount, 0);
    for (int i = 0; i < tileCount; ++i) {
        if (static_cast<size_t>(i) * 16 + 16 <= blockDesc.size()) {
            out.blockFlags[i] = rd32(blockDesc.data() + static_cast<size_t>(i) * 16 + 12);
        }
    }

    // Map layers from DATA MAP_ (a GOB_ payload).
    const GafChunk* dataMap = data->findChild("MAP_");
    if (!dataMap) return false;
    std::vector<std::vector<uint8_t>> mapComps;
    if (!decodeGobPayload(dataMap->payload, mapComps) || mapComps.size() < 3) return false;

    // map header: [layerCount, ?, ?, mapW, mapH, ?]
    const auto& hdr = mapComps[0];
    if (hdr.size() < 20) return false;
    out.layerCount = static_cast<int>(rd32(hdr.data()));
    out.mapW = static_cast<int>(rd32(hdr.data() + 12));
    out.mapH = static_cast<int>(rd32(hdr.data() + 16));
    if (out.mapW <= 0 || out.mapH <= 0 || out.layerCount <= 0 || out.layerCount > 16) return false;

    const size_t layerSize = static_cast<size_t>(out.mapW) * out.mapH * 4;
    const auto& mapData = mapComps[2];
    if (mapData.size() < layerSize * out.layerCount) return false;

    out.layers.resize(out.layerCount);
    for (int L = 0; L < out.layerCount; ++L) {
        out.layers[L].resize(static_cast<size_t>(out.mapW) * out.mapH);
        const uint8_t* src = mapData.data() + static_cast<size_t>(L) * layerSize;
        for (int i = 0; i < out.mapW * out.mapH; ++i) {
            out.layers[L][i] = rd32(src + static_cast<size_t>(i) * 4);
        }
    }
    return true;
}

bool loadTerrain(const std::vector<uint8_t>& instances, const std::vector<uint8_t>& catalog,
                 Level& level) {
    if (instances.size() < 4 || (instances.size() - 4) % 6 ||
        instances.size() > 6'000'004 || level.mapW <= 0 || level.mapH <= 0 ||
        level.mapW - 1 > 65535 / 64 || level.mapH - 1 > 65535 / 32 ||
        rd16(instances.data()) != (level.mapW - 1) * 64 ||
        rd16(instances.data() + 2) != (level.mapH - 1) * 32) return false;

    // The .3DX GAF list precedes its bitmap/object list; nonzero object
    // entries refer back to a GAF in that first list.
    std::istringstream text(std::string(catalog.begin(), catalog.end()));
    std::vector<std::string> gafPaths(1);
    std::vector<uint8_t> classes(1);
    std::string line;
    enum class Section { None, Gaf, Objects } section = Section::None;
    int remaining = -1;
    while (std::getline(text, line)) {
        const auto start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        line.erase(0, start);
        if (line.rfind("# GAF files", 0) == 0) { section = Section::Gaf; remaining = -1; continue; }
        if (line.rfind("# Objects", 0) == 0) {
            if (section != Section::Gaf || remaining != 0) return false;
            section = Section::Objects; remaining = -1; continue;
        }
        if (line[0] == '#' || section == Section::None) continue;
        std::istringstream row(line);
        if (remaining == -1) {
            if (!(row >> remaining) || remaining < 0 || remaining > 4096) return false;
            continue;
        }
        if (remaining == 0) continue;
        if (section == Section::Gaf) {
            gafPaths.push_back(line);
        } else {
            int gafIndex = -1;
            std::string name;
            if (!(row >> gafIndex) || gafIndex < 0) return false;
            std::getline(row >> std::ws, name);
            if (name.empty()) return false;
            if (gafIndex) {
                if (static_cast<size_t>(gafIndex) >= gafPaths.size()) return false;
                name = gafPaths[gafIndex];
            }
            const auto type = classFromPath(name);
            if (!type) return false; // Unknown collision class must not disappear silently.
            classes.push_back(type);
        }
        --remaining;
    }
    if (section != Section::Objects || remaining != 0 || classes.size() < 2) return false;

    const size_t cells = static_cast<size_t>(level.mapW) * level.mapH;
    std::vector<std::array<uint8_t, 4>> slots(cells);
    for (size_t pos = 4; pos < instances.size(); pos += 6) {
        const int tx = rd16(instances.data() + pos);
        const int ty = rd16(instances.data() + pos + 2);
        const int id = rd16(instances.data() + pos + 4);
        if (tx >= level.mapW || ty >= level.mapH || ((tx + ty) & 1) ||
            id == 0 || static_cast<size_t>(id) >= classes.size()) return false;
        const auto type = classes[id];
        const int slot = slotFromClass(type);
        if (slot < 0) return false;
        slots[static_cast<size_t>(ty) * level.mapW + tx][slot] = type;
    }
    std::vector<uint8_t> terrain(cells, 0);
    std::vector<uint16_t> mask(cells, 0);
    constexpr uint16_t patterns[] = {0x1e, 0x78, 0x87, 0xe1};
    // Native storage is X-major; preserve visit order even in our row-major table.
    for (int x = 0; x < level.mapW; ++x) {
        for (int y = 0; y < level.mapH; ++y) {
            const size_t cell = static_cast<size_t>(y) * level.mapW + x;
            for (const auto type : slots[cell]) {
                terrain[cell] |= kindFromClass(type);
                for (int part = 0; part < 4; ++part) {
                    const int mx = x + (part & 1), my = y + 1 + part / 2;
                    if (mx >= level.mapW || my >= level.mapH) continue;
                    auto& bits = mask[static_cast<size_t>(my) * level.mapW + mx];
                    if ((type >= 2 && type <= 5) ||
                        ((type == 6 || type == 8) && part >= 2)) bits |= patterns[part];
                    if (type == 10 || (type == 7 && part >= 2)) bits &= ~patterns[part];
                    if (type == 15) bits |= 0x100;
                }
            }
        }
    }
    auto typeAt = [&](int x, int y, int slot) -> uint8_t {
        if (x < 0 || y < 0 || x >= level.mapW || y >= level.mapH) return 0;
        return slots[static_cast<size_t>(y) * level.mapW + x][slot];
    };
    std::vector<CollisionEdge> edges;
    std::vector<NativeNavigationDecor> navDecor;
    for (int y = 0; y < level.mapH; ++y) {
        for (int x = 0; x < level.mapW; ++x) {
            uint16_t bits = mask[static_cast<size_t>(y) * level.mapW + x] & 0xff;
            int shift = 0;
            if (!bits && y > 0) {
                if (typeAt(x, y - 1, 1) == 10) bits = 0xe1;
                else if (x > 0 && typeAt(x, y, 0) == 15 && typeAt(x, y, 1) == 9) {
                    bits = 0xe1;
                    shift = 1;
                } else if (y > 1 && typeAt(x, y - 2, 1) == 10) bits = 0x78;
                else if (x > 0 && typeAt(x - 1, y - 1, 1) == 10) bits = 0x87;
                else if (x > 0 && y > 1 && typeAt(x - 1, y - 2, 1) == 10) bits = 0x1e;
            }
            if (bits != 0x1e && bits != 0x78 && bits != 0x87 && bits != 0xe1) continue;
            const int ownerX = x - (bits == 0x1e || bits == 0x87 ? 1 : 0);
            const int ownerY = y - (bits == 0x1e || bits == 0x78 ? 2 : 1);
            const bool water = typeAt(ownerX, ownerY, 0) == 15;
            const float left = static_cast<float>((x + shift) * 64);
            const float top = static_cast<float>(y * 32);
            // Native 41adb0 uses exact float constants from 50952c/50955c.
            constexpr float half=35.777088165283203125f, full=71.55417633056640625f;
            float nx=float(x+y)*half, ny=float(y-x)*half;
            if(shift){nx+=half;ny-=half;}
            if(bits==0x1e||bits==0xe1)
                navDecor.push_back({nx+half,ny-half,nx+half,ny+half,x,y,bits==0x1e?-2:-5,water,false});
            else navDecor.push_back({nx,ny,nx+full,ny,x,y,bits==0x78?-6:-3,water,false});
            if (bits == 0x1e || bits == 0xe1)
                edges.push_back({left + 64, top, left, top + 32, water});
            else
                edges.push_back({left, top, left + 64, top + 32, water});
        }
    }
    // 41aa00: reverse cell traversal, joins neighbouring ground decor into
    // rectangular blockers before 40f090 inserts them into the navigation graph.
    std::vector<size_t> navByCell(cells,navDecor.size());
    for(size_t i=0;i<navDecor.size();++i)navByCell[size_t(navDecor[i].cellY)*level.mapW+navDecor[i].cellX]=i;
    auto neighbour=[&](int x,int y,int kind)->NativeNavigationDecor* {
        if(x<0||y<0||x>=level.mapW||y>=level.mapH)return nullptr;
        size_t i=navByCell[size_t(y)*level.mapW+x];if(i>=navDecor.size())return nullptr;
        auto& d=navDecor[i];return d.kind==kind&&!d.disabled&&!d.water?&d:nullptr;
    };
    constexpr float nativeFull=71.55417633056640625f;
    for(auto it=navDecor.rbegin();it!=navDecor.rend();++it){auto& d=*it;if(d.disabled||d.water||d.rectangle)continue;
        NativeNavigationDecor* other=nullptr;
        switch(d.kind){
        case -6: other=neighbour(d.cellX+1,d.cellY,-2);if(other)d.minY-=nativeFull;break;
        case -5: other=neighbour(d.cellX,d.cellY+1,-6);if(!other)other=neighbour(d.cellX+1,d.cellY,-3);if(other)d.maxX+=nativeFull;break;
        case -3: other=neighbour(d.cellX,d.cellY+1,-2);if(other)d.maxY+=nativeFull;break;
        case -2: other=neighbour(d.cellX,d.cellY-1,-3);if(other)d.minX-=nativeFull;break;
        }
        if(other){other->disabled=true;d.rectangle=true;}
    }
    std::reverse(navDecor.begin(),navDecor.end()); // 40f090 y descending, x descending
    level.nativeNavigationDecor=std::move(navDecor);
    rebuildNativeDecorIndex(level);
    level.terrain = std::move(terrain);
    level.collisionMask = std::move(mask);
    level.collisionEdges = std::move(edges);
    level.hasTerrainCollision = true;
    level.surfaceWidth=rd16(instances.data());
    level.surfaceHeight=rd16(instances.data()+2);
    rebuildTerrainIndex(level);
    return true;
}

void rebuildTerrainIndex(Level& level) {
    auto& order = level.collisionEdgeOrder;
    auto& tree = level.collisionEdgeTree;
    tree.clear();
    order.resize(level.collisionEdges.size());
    std::iota(order.begin(), order.end(), size_t{0});
    // Invalid diagnostic geometry keeps the original linear-query behavior.
    for (const auto& e : level.collisionEdges)
        if (!std::isfinite(e.x0) || !std::isfinite(e.y0) ||
            !std::isfinite(e.x1) || !std::isfinite(e.y1)) return;
    if (order.empty()) return;
    tree.reserve(order.size() * 2);
    const auto build = [&](auto&& self, size_t begin, size_t end) -> void {
        const size_t node = tree.size();
        TerrainEdgeNode bounds{INFINITY, INFINITY, -INFINITY, -INFINITY, begin, end, 0};
        for (size_t i = begin; i < end; ++i) {
            const auto& e = level.collisionEdges[order[i]];
            bounds.minX = std::min({bounds.minX, e.x0, e.x1});
            bounds.minY = std::min({bounds.minY, e.y0, e.y1});
            bounds.maxX = std::max({bounds.maxX, e.x0, e.x1});
            bounds.maxY = std::max({bounds.maxY, e.y0, e.y1});
        }
        tree.push_back(bounds);
        if (end - begin > 8) {
            const bool xAxis = bounds.maxX - bounds.minX > bounds.maxY - bounds.minY;
            const size_t middle = begin + (end - begin) / 2;
            std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end,
                [&](size_t a, size_t b) {
                    const auto& ea = level.collisionEdges[a];
                    const auto& eb = level.collisionEdges[b];
                    return xAxis ? double(ea.x0) + ea.x1 < double(eb.x0) + eb.x1
                                 : double(ea.y0) + ea.y1 < double(eb.y0) + eb.y1;
                });
            self(self, begin, middle);
            self(self, middle, end);
        }
        tree[node].escape = tree.size();
    };
    build(build, 0, order.size());
}

void rebuildNativeDecorIndex(Level& level) {
 auto& order=level.nativeDecorOrder;auto& tree=level.nativeDecorTree;order.clear();tree.clear();
 for(size_t i=0;i<level.nativeNavigationDecor.size();++i)if(!level.nativeNavigationDecor[i].disabled)order.push_back(i);
 if(order.empty())return;
 auto build=[&](auto&& self,size_t begin,size_t end)->void {
  size_t id=tree.size();TerrainEdgeNode b{INFINITY,INFINITY,-INFINITY,-INFINITY,begin,end,0};
  for(size_t i=begin;i<end;++i){auto d=level.nativeNavigationDecor[order[i]];b.minX=std::min(b.minX,d.minX);b.minY=std::min(b.minY,d.minY);b.maxX=std::max(b.maxX,d.maxX);b.maxY=std::max(b.maxY,d.maxY);}tree.push_back(b);
  if(end-begin>8){bool x=b.maxX-b.minX>b.maxY-b.minY;size_t middle=begin+(end-begin)/2;std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[&](size_t ai,size_t bi){auto a=level.nativeNavigationDecor[ai],b=level.nativeNavigationDecor[bi];return x?double(a.minX)+a.maxX<double(b.minX)+b.maxX:double(a.minY)+a.maxY<double(b.minY)+b.maxY;});self(self,begin,middle);self(self,middle,end);}tree[id].escape=tree.size();
 };build(build,0,order.size());
}

uint8_t terrainAt(const Level& level, float worldX, float worldY) {
    if (level.terrain.size() != static_cast<size_t>(level.mapW) * level.mapH ||
        worldX < 0 || worldY < 0) return 0;
    const int cx = static_cast<int>(std::floor(worldX / 64.f));
    const int cy = static_cast<int>(std::floor(worldY / 32.f));
    float best = std::numeric_limits<float>::infinity();
    uint8_t result = 0;
    for (int ty = std::max(0, cy - 1); ty <= std::min(level.mapH - 1, cy + 2); ++ty) {
        for (int tx = std::max(0, cx - 1); tx <= std::min(level.mapW - 1, cx + 2); ++tx) {
            const uint8_t kind = level.terrain[static_cast<size_t>(ty) * level.mapW + tx];
            if (kind == 0) continue;
            const float dx = worldX / 64.f - tx, dy = worldY / 32.f - ty;
            const float distance = dx * dx + dy * dy;
            if (distance < best) { best = distance; result = kind; }
        }
    }
    return best <= 1.f ? result : 0;
}

bool terrainWalkable(const Level& level, float worldX, float worldY) {
    if (worldX < 0.f || worldY < 0.f ||
        worldX >= level.mapW * level.tileW || worldY >= level.mapH * level.tileH) return false;
    const int tx = static_cast<int>(worldX / level.tileW);
    const int ty = static_cast<int>(worldY / level.tileH);
    const uint8_t kind = terrainAt(level, worldX, worldY);
    if (kind & (TerrainWall | TerrainObstacle)) return false;
    if (kind & TerrainGround) return true;
    // Roof is a visual/height class, not an automatic solid-wall bit in the
    // original collision-mask builder. A visible floor remains traversable.
    if (kind & TerrainRoof) {
        const size_t cell = static_cast<size_t>(ty) * level.mapW + tx;
        return !level.layers.empty() && cell < level.layers[0].size() &&
               level.layers[0][cell] != 0;
    }
    // Missing static class metadata: restrict movement to clear baked ground.
    if (level.layers.empty()) return false;
    const size_t cell = static_cast<size_t>(ty) * level.mapW + tx;
    if (cell >= level.layers[0].size() || level.layers[0][cell] == 0) return false;
    for (size_t layer = 2; layer < level.layers.size(); ++layer) {
        if (cell < level.layers[layer].size() && level.layers[layer][cell] != 0) return false;
    }
    return true;
}

namespace {
constexpr float clearanceTolerance = 0.0001f;
// Native circle lives in the boundary basis a=(x/64+y/32)*16sqrt(5),
// b=(y/32-x/64)*16sqrt(5). Rotation can be omitted for distances.
constexpr double metricX = 0.7905694150420949; // sqrt(5/8)
constexpr double metricY = 1.5811388300841898; // sqrt(5/2)

struct MetricEdge { double x0, y0, x1, y1; };

MetricEdge metricEdge(const CollisionEdge& e) {
    return {e.x0 * metricX, e.y0 * metricY, e.x1 * metricX, e.y1 * metricY};
}

double pointDistance(double x, double y, const MetricEdge& edge) {
    const double dx = edge.x1 - edge.x0, dy = edge.y1 - edge.y0;
    const double length2 = dx * dx + dy * dy;
    const double t = length2 > 0 ? std::clamp(((x - edge.x0) * dx + (y - edge.y0) * dy) /
                                            length2, 0.0, 1.0) : 0.0;
    return std::hypot(x - edge.x0 - t * dx, y - edge.y0 - t * dy);
}

double sweptDistance(double x, double y, double nx, double ny, const MetricEdge& edge) {
    const double dx = nx - x, dy = ny - y;
    const double ex = edge.x1 - edge.x0, ey = edge.y1 - edge.y0;
    const double cross = dx * ey - dy * ex;
    if (cross != 0.f) {
        const double ax = edge.x0 - x, ay = edge.y0 - y;
        const double t = (ax * ey - ay * ex) / cross;
        const double u = (ax * dy - ay * dx) / cross;
        if (t >= 0.f && t <= 1.f && u >= 0.f && u <= 1.f) return 0.f;
    }
    const MetricEdge path{x, y, nx, ny};
    return std::min({pointDistance(x, y, edge), pointDistance(nx, ny, edge),
                     pointDistance(edge.x0, edge.y0, path),
                     pointDistance(edge.x1, edge.y1, path)});
}

bool edgeAllows(const CollisionEdge& edge, float x, float y, float nx, float ny, float radius) {
    // Conservative swept ellipse bounds avoid expensive segment distances for
    // unrelated map edges, particularly during repeated navigation queries.
    const double rx=radius/metricX, ry=radius/metricY;
    if (std::max(x,nx)+rx < std::min(edge.x0,edge.x1) ||
        std::min(x,nx)-rx > std::max(edge.x0,edge.x1) ||
        std::max(y,ny)+ry < std::min(edge.y0,edge.y1) ||
        std::min(y,ny)-ry > std::max(edge.y0,edge.y1)) return true;
    // An existing penetration may stay tangent or escape, but may not first
    // deepen, cross the wall, or conceal penetration of another boundary.
    const auto transformed = metricEdge(edge);
    const double before = pointDistance(x * metricX, y * metricY, transformed);
    const double minimum = sweptDistance(x * metricX, y * metricY,
                                        nx * metricX, ny * metricY, transformed);
    if (before < radius - clearanceTolerance) {
        const double after = pointDistance(nx * metricX, ny * metricY, transformed);
        return after >= before && minimum + clearanceTolerance >= before;
    }
    return minimum + clearanceTolerance >= radius;
}
} // namespace

bool terrainMoveAllowed(const Level& level, float fromX, float fromY,
                        float toX, float toY, float radius, bool overWater) {
    if (!std::isfinite(fromX) || !std::isfinite(fromY) || !std::isfinite(toX) ||
        !std::isfinite(toY) || !std::isfinite(radius) ||
        toX < 0.f || toY < 0.f || toX >= level.mapW * level.tileW ||
        toY >= level.mapH * level.tileH || radius < 0.f) return false;
    if (!level.hasTerrainCollision && level.collisionEdges.empty())
        return terrainWalkable(level, toX, toY);
    if (!level.collisionEdgeTree.empty() &&
        level.collisionEdgeOrder.size() == level.collisionEdges.size()) {
        const double rx = radius / metricX, ry = radius / metricY;
        const double minX = std::min(fromX, toX) - rx, maxX = std::max(fromX, toX) + rx;
        const double minY = std::min(fromY, toY) - ry, maxY = std::max(fromY, toY) + ry;
        for (size_t n = 0; n < level.collisionEdgeTree.size();) {
            const auto& node = level.collisionEdgeTree[n];
            if (maxX < node.minX || minX > node.maxX || maxY < node.minY || minY > node.maxY) {
                n = node.escape;
                continue;
            }
            if (node.end - node.begin <= 8) {
                for (size_t i = node.begin; i < node.end; ++i)
                    if (const auto& edge=level.collisionEdges[level.collisionEdgeOrder[i]];
                        !(overWater && edge.waterBoundary) &&
                        !edgeAllows(edge,fromX,fromY,toX,toY,radius)) return false;
            }
            ++n;
        }
    } else {
        for (const auto& edge : level.collisionEdges)
            if (!(overWater && edge.waterBoundary) &&
                !edgeAllows(edge, fromX, fromY, toX, toY, radius)) return false;
    }
    return true;
}

TerrainDisplacement moveOnTerrain(const Level& level, float x, float y,
                                  float dx, float dy, float radius,
                                  TerrainContacts* contacts, bool allowSlide, bool overWater) {
    if (contacts) *contacts = {};
    const float startX = x, startY = y;
    bool constrained=false;
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(x) ||
        !std::isfinite(y) || !std::isfinite(radius) || radius < 0.f) return {};
    const float distance = std::hypot(dx, dy);
    // Bound work for malformed callers. Ordinary motion is under a few pixels.
    if (!std::isfinite(distance) || distance > 65536.f) return {};
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / 2.f)));
    dx /= steps;
    dy /= steps;
    for (int step = 0; step < steps; ++step) {
        if (terrainMoveAllowed(level, x, y, x + dx, y + dy, radius, overWater)) {
            x += dx;
            y += dy;
            continue;
        }
        constrained=true;
        float low = 0.f, high = 1.f;
        for (int i = 0; i < 16; ++i) {
            const float mid = (low + high) * 0.5f;
            if (terrainMoveAllowed(level, x, y, x + dx * mid, y + dy * mid, radius, overWater)) low = mid;
            else high = mid;
        }
        const float oldX = x, oldY = y;
        x += dx * low;
        y += dy * low;
        // Closest point in the native-circle metric gives the impact normal,
        // including endpoint contacts. The metric basis is native X/Z rotated
        // by 45 degrees, so undo that rotation for the response solver.
        if (contacts) {
            for (const auto& edge : level.collisionEdges) {
                if (overWater && edge.waterBoundary) continue;
                if (edgeAllows(edge, oldX, oldY, oldX + dx, oldY + dy, radius)) continue;
                const auto e = metricEdge(edge);
                const double ex = e.x1 - e.x0, ey = e.y1 - e.y0;
                const double length2 = ex * ex + ey * ey;
                const double t = length2 > 0 ? std::clamp(
                    ((x * metricX - e.x0) * ex + (y * metricY - e.y0) * ey) /
                    length2, 0.0, 1.0) : 0.0;
                const double qx = e.x0 + t * ex, qy = e.y0 + t * ey;
                double nx = qx - x * metricX, ny = qy - y * metricY;
                double norm = std::hypot(nx, ny);
                const double initialDistance = pointDistance(oldX * metricX, oldY * metricY, e);
                if (norm > std::min<double>(radius, initialDistance) + 0.003) continue;
                // A zero-radius/initially coincident contact has no unique
                // normal. Choose the edge normal facing attempted movement.
                if (norm < 1e-9) {
                    nx = -ey; ny = ex;
                    if (nx * dx * metricX + ny * dy * metricY < 0) { nx = -nx; ny = -ny; }
                    norm = std::hypot(nx, ny);
                }
                if (norm < 1e-9) continue;
                constexpr double invSqrt2 = 0.7071067811865475;
                TerrainContact contact{static_cast<float>(qx / metricX),
                    static_cast<float>(qy / metricY),
                    static_cast<float>((nx + ny) * invSqrt2 / norm),
                    static_cast<float>((ny - nx) * invSqrt2 / norm), edge.waterBoundary};
                bool duplicate = false;
                for (size_t i = 0; i < contacts->count; ++i)
                    if (contacts->values[i].normalX * contact.normalX +
                        contacts->values[i].normalZ * contact.normalZ > 0.9999f) duplicate = true;
                if (!duplicate && contacts->count < contacts->values.size())
                    contacts->values[contacts->count++] = contact;
            }
        }
        if (!allowSlide) break;
        TerrainDisplacement slide{};
        float best = 0.f;
        for (const auto& edge : level.collisionEdges) {
            if (overWater && edge.waterBoundary) continue;
            if (edgeAllows(edge, oldX, oldY, oldX + dx, oldY + dy, radius)) continue;
            const float ex = edge.x1 - edge.x0, ey = edge.y1 - edge.y0;
            const float length2 = ex * ex * metricX * metricX + ey * ey * metricY * metricY;
            if (length2 == 0.f) continue;
            const float scale = (dx * ex * metricX * metricX + dy * ey * metricY * metricY) *
                                (1.f - low) / length2;
            float sx = ex * scale, sy = ey * scale;
            if (!terrainMoveAllowed(level, x, y, x + sx, y + sy, radius, overWater)) {
                float accepted = 0.f, rejected = 1.f;
                for (int i = 0; i < 16; ++i) {
                    const float mid = (accepted + rejected) * 0.5f;
                    if (terrainMoveAllowed(level, x, y, x + sx * mid, y + sy * mid, radius, overWater))
                        accepted = mid;
                    else rejected = mid;
                }
                sx *= accepted;
                sy *= accepted;
            }
            const float progress = sx * dx + sy * dy;
            if (progress > best) {
                slide = {sx, sy};
                best = progress;
            }
        }
        x += slide.x;
        y += slide.y;
    }
    return {x - startX, y - startY,constrained};
}

} // namespace vyruz
