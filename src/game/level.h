#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "assets/tga.h"

namespace vyruz {

enum TerrainKind : uint8_t {
    TerrainGround = 1 << 0,
    TerrainRoof = 1 << 1,
    TerrainWall = 1 << 2,
    TerrainObstacle = 1 << 3,
    TerrainWater = 1 << 4,
    TerrainCorner = 1 << 5,
};

struct CollisionEdge {
    float x0, y0, x1, y1;
    bool waterBoundary = false; // native kind 2; response filtering remains untraced
};

// Derived broad-phase bounds; preorder nodes skip whole unrelated subtrees.
struct NativeNavigationDecor {
    float minX=0,minY=0,maxX=0,maxY=0;
    int cellX=0,cellY=0,kind=0;
    bool water=false,disabled=false,rectangle=false;
};

struct TerrainEdgeNode {
    float minX, minY, maxX, maxY;
    size_t begin, end, escape;
};

// A loaded Vyruz level (.3DG): isometric tile bitmaps + tile-index layers.
struct Level {
    int tileW = 64;
    int tileH = 32;
    int mapW = 80;
    int mapH = 130;
    // Exact .3DI header pixel extents; the tile grid includes a padded row and
    // column. Zero denotes a synthetic/no-terrain-header fixture.
    int surfaceWidth = 0, surfaceHeight = 0;
    int layerCount = 6;
    std::vector<Image> tiles;                       // tile 0 is real; map index 0 is empty
    std::vector<uint32_t> blockFlags;               // foreground row phase; UINT32_MAX = no redraw
    std::vector<uint32_t> tileDrawFlags;            // native bitmap draw flags, separate from row phase
    std::vector<std::vector<uint32_t>> layers;      // [layer][mapW*mapH], 0 = empty
    std::vector<uint8_t> terrain;                    // .3DI/.3DX authored categories per diamond
    std::vector<uint16_t> collisionMask;             // native ground-edge bit patterns
    bool hasTerrainCollision = false;               // distinguishes empty authored edges from absent metadata
    std::vector<size_t> nativeDecorOrder;
    std::vector<TerrainEdgeNode> nativeDecorTree;
    std::vector<NativeNavigationDecor> nativeNavigationDecor; // original 41adb0/41aa00 graph shapes
    std::vector<CollisionEdge> collisionEdges;      // exact masks and zero-mask neighbor boundaries
    std::vector<size_t> collisionEdgeOrder;
    std::vector<TerrainEdgeNode> collisionEdgeTree;
};

// Loads the isometric tile/map data from a .3DG file.
bool loadLevel(const std::vector<uint8_t>& d3gData, Level& out);

// .3DI's 1-based references resolve through the ordered # Objects table in
// .3DX; its entries are (tx, ty, objectIndex) in 6-byte records.
bool loadTerrain(const std::vector<uint8_t>& instances, const std::vector<uint8_t>& catalog,
                 Level& level);

// Loaded terrain builds this automatically. After editing collisionEdges,
// rebuild or clear collisionEdgeTree; synthetic unindexed levels use a full scan.
void rebuildTerrainIndex(Level& level);
void rebuildNativeDecorIndex(Level& level);

// Return the authored category at the closest even-parity diamond centre.
uint8_t terrainAt(const Level& level, float worldX, float worldY);

// Legacy diagnostic for levels without authored collision metadata.
// Loaded .3DI movement uses terrainMoveAllowed, including empty edge lists.
bool terrainWalkable(const Level& level, float worldX, float worldY);

// Test one movement step for a radius-bearing player against static terrain
// edges. Radius is in native units; positions are surface pixels. Existing
// overlap may escape without deepening (within numerical contact tolerance).
// Over-water skips kind-2 shorelines only; default movement remains unchanged.
bool terrainMoveAllowed(const Level& level, float fromX, float fromY,
                        float toX, float toY, float radius, bool overWater = false);

struct TerrainDisplacement {
    float x = 0.f, y = 0.f;
    bool constrained = false;
    //440744: selected actor->Ammo impact skips the actor's remaining tick.
    bool ammoAbort = false;
};

struct TerrainContact {
    float x = 0.f, y = 0.f; // surface point on the blocking edge
    float normalX = 0.f, normalZ = 0.f; // native unit vector toward the obstacle
    bool waterBoundary = false;
};
struct TerrainContacts {
    std::array<TerrainContact, 4> values{};
    size_t count = 0;
};

// Continuous clearance; optional contacts are reset on every call. Legacy
// callers slide along tangents; elastic player response stops at first contact.
// Contact selection remains the port's swept solver, not native list ordering.
TerrainDisplacement moveOnTerrain(const Level& level, float x, float y,
                                  float dx, float dy, float radius,
                                  TerrainContacts* contacts = nullptr, bool slide = true, bool overWater = false);

} // namespace vyruz
