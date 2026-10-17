#include "game/level.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {
const std::vector<std::string> categories = {
    "GROUND", "ROOF", "OBSTACLE", "LEFTWALL", "WATER", "HALF-GROUND",
    "HALF-ROOF", "HORIZONTALWALL", "GROUND+LEFTWALL", "GROUND+RIGHTWALL",
    "CORNER", "RIGHTWALL", "WALLCORNER", "ROOF+", "BRIDGE"};

std::vector<uint8_t> catalog() {
    std::string text = "# GAF files\n1 # Count\nD:\\Game\\ANIM\\OBSTACLE\\moving.gaf\n"
                       "# Objects\n16 # Count\n";
    for (const auto& name : categories) text += "0 D:\\Game with spaces\\BITMAP\\" + name + "\\piece.tga\n";
    text += "1 moving\n";
    return {text.begin(), text.end()};
}
void word(std::vector<uint8_t>& bytes, int value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}
std::vector<uint8_t> header(int w, int h) {
    std::vector<uint8_t> bytes;
    word(bytes, (w - 1) * 64); word(bytes, (h - 1) * 32);
    return bytes;
}
void piece(std::vector<uint8_t>& bytes, int x, int y, int id) {
    word(bytes, x); word(bytes, y); word(bytes, id);
}
vyruz::Level load(const std::vector<uint8_t>& bytes, int w = 8, int h = 8) {
    vyruz::Level level;
    level.mapW = w; level.mapH = h;
    assert(vyruz::loadTerrain(bytes, catalog(), level));
    return level;
}
bool near(float a, float b, float tolerance = 0.001f) { return std::abs(a - b) <= tolerance; }
bool hasEdge(const vyruz::Level& level, float x0, float y0, float x1, float y1, bool water = false) {
    return std::any_of(level.collisionEdges.begin(), level.collisionEdges.end(), [&](const auto& e) {
        return near(e.x0, x0) && near(e.y0, y0) && near(e.x1, x1) && near(e.y1, y1) &&
               e.waterBoundary == water;
    });
}
void sameGeometry(const vyruz::Level& a, const vyruz::Level& b) {
    assert(a.terrain == b.terrain && a.collisionMask == b.collisionMask);
    assert(a.collisionEdges.size() == b.collisionEdges.size());
    for (const auto& e : a.collisionEdges) assert(hasEdge(b, e.x0, e.y0, e.x1, e.y1, e.waterBoundary));
}

void masksAndSlots() {
    auto bytes = header(8, 8); piece(bytes, 2, 2, 1);
    const auto ground = load(bytes);
    assert(ground.collisionMask[3 * 8 + 2] == 0x1e);
    assert(ground.collisionMask[3 * 8 + 3] == 0x78);
    assert(ground.collisionMask[4 * 8 + 2] == 0x87);
    assert(ground.collisionMask[4 * 8 + 3] == 0xe1);
    assert(ground.collisionEdges.size() == 4);
    assert(hasEdge(ground, 192, 96, 128, 128));
    assert(hasEdge(ground, 192, 96, 256, 128));
    assert(hasEdge(ground, 128, 128, 192, 160));
    assert(hasEdge(ground, 256, 128, 192, 160));

    for (int id : {9, 10, 11}) { // combined ground/walls and Corner
        auto input = header(8, 8); piece(input, 2, 2, id);
        const auto level = load(input);
        assert(level.collisionMask == ground.collisionMask);
    }
    for (int id : {6, 8}) { // Half-Ground and HorizontalWall
        auto input = header(8, 8); piece(input, 2, 2, id);
        const auto level = load(input);
        assert(level.collisionMask[3 * 8 + 2] == 0);
        assert(level.collisionMask[4 * 8 + 2] == 0x87);
        assert(level.collisionEdges.size() == 2);
    }
    auto roofOnly = header(8, 8); piece(roofOnly, 2, 2, 2);
    auto tunnel = load(roofOnly);
    tunnel.layers = {std::vector<uint32_t>(64, 1), {}, std::vector<uint32_t>(64, 7)};
    assert(tunnel.collisionEdges.empty());
    assert(vyruz::terrainMoveAllowed(tunnel, 128, 64, 160, 64, 22));

    auto halfRoof = bytes; piece(halfRoof, 2, 2, 7);
    const auto cut = load(halfRoof);
    assert(cut.collisionMask[3 * 8 + 2] == 0x1e);
    assert(cut.collisionMask[4 * 8 + 2] == 0);
    assert(cut.collisionEdges.size() == 2);

    auto roof = bytes; piece(roof, 2, 2, 2); piece(roof, 2, 2, 14);
    const auto covered = load(roof);
    assert(covered.collisionMask == ground.collisionMask);
    assert(covered.terrain[2 * 8 + 2] == (vyruz::TerrainGround | vyruz::TerrainRoof));

    // Different slots are evaluated by type, independent of input record order.
    auto input = bytes; piece(input, 2, 2, 3);
    auto reverse = header(8, 8); piece(reverse, 2, 2, 3); piece(reverse, 2, 2, 1);
    sameGeometry(load(input), load(reverse));
    assert(load(input).collisionMask[3 * 8 + 2] == 0);

    // Same slot: only the final record survives; Water replaces Ground.
    input = bytes; piece(input, 2, 2, 5);
    const auto water = load(input);
    assert(water.terrain[2 * 8 + 2] == vyruz::TerrainWater);
    assert(water.collisionMask[3 * 8 + 2] == 0x100);
    assert(water.collisionEdges.empty());
    assert(vyruz::terrainMoveAllowed(water, 160, 100, 180, 100, 22));

    // Different cells: native X/Y order, independent of file record order.
    input = header(8, 8); piece(input, 2, 2, 1); piece(input, 3, 1, 3);
    reverse = header(8, 8); piece(reverse, 3, 1, 3); piece(reverse, 2, 2, 1);
    sameGeometry(load(input), load(reverse));
    assert(load(input).collisionMask[3 * 8 + 3] == 0x78); // neighbor clears complementary 0x87

    // Per-write clipping preserves the part of a diamond within the map.
    input = header(8, 8); piece(input, 7, 5, 1);
    const auto border = load(input);
    assert(border.collisionMask[6 * 8 + 7] == 0x1e);
    assert(border.collisionMask[7 * 8 + 7] == 0x87);
    assert(border.collisionEdges.size() == 2);
}

void zeroMaskNeighbors() {
    // A lone obstacle supplies all four zero-mask neighbor cases.
    auto bytes = header(8, 8); piece(bytes, 2, 2, 3);
    const auto obstacle = load(bytes);
    assert(std::all_of(obstacle.collisionMask.begin(), obstacle.collisionMask.end(),
                       [](auto bits) { return bits == 0; }));
    assert(obstacle.collisionEdges.size() == 4);
    assert(hasEdge(obstacle, 192, 96, 128, 128)); // e1: obstacle (x,y-1)
    assert(hasEdge(obstacle, 128, 128, 192, 160)); // 78: obstacle (x,y-2)
    assert(hasEdge(obstacle, 192, 96, 256, 128)); // 87: obstacle (x-1,y-1)
    assert(hasEdge(obstacle, 256, 128, 192, 160)); // 1e: obstacle (x-1,y-2)

    auto gaf = header(8, 8); piece(gaf, 2, 2, 16);
    sameGeometry(obstacle, load(gaf)); // GAF-linked category uses the same slot

    bytes = header(8, 8); piece(bytes, 2, 2, 5); piece(bytes, 2, 2, 4);
    const auto waterWall = load(bytes);
    assert(waterWall.collisionEdges.size() == 1);
    assert(hasEdge(waterWall, 256, 64, 192, 96)); // shifted e1, not cell's diagonal
    assert(!hasEdge(waterWall, 192, 64, 128, 96));

    // Current water/leftwall has precedence over a two-row obstacle neighbor.
    bytes = header(8, 8); piece(bytes, 2, 0, 3);
    piece(bytes, 2, 2, 5); piece(bytes, 2, 2, 4);
    const auto precedence = load(bytes);
    assert(hasEdge(precedence, 256, 64, 192, 96));
    assert(!hasEdge(precedence, 128, 64, 192, 96));

    bytes = header(8, 8); piece(bytes, 2, 2, 5); piece(bytes, 2, 2, 3);
    const auto wetObstacle = load(bytes);
    assert(hasEdge(wetObstacle, 192, 96, 128, 128, true));
    assert(hasEdge(wetObstacle, 128, 128, 192, 160, true));

    // Competing obstacle neighbors: the first case owns the water flag.
    bytes = header(8, 8); piece(bytes, 2, 2, 5); piece(bytes, 2, 2, 3);
    piece(bytes, 1, 1, 3);
    const auto firstWins = load(bytes);
    assert(hasEdge(firstWins, 192, 96, 128, 128, true));
    assert(!hasEdge(firstWins, 192, 96, 128, 128, false));

    // A nonzero unmatched mask never falls through to neighbor generation.
    bytes = header(8, 8); piece(bytes, 2, 2, 1); piece(bytes, 3, 3, 1);
    const auto joined = load(bytes);
    assert(joined.collisionMask[4 * 8 + 3] == 0xff);
    assert(!hasEdge(joined, 256, 128, 192, 160));

    // Top and left boundaries cannot read outside authored slot storage.
    bytes = header(8, 8); piece(bytes, 0, 0, 3);
    assert(load(bytes).collisionEdges.size() == 4);
    bytes = header(8, 8); piece(bytes, 0, 0, 5); piece(bytes, 0, 0, 4);
    assert(load(bytes).collisionEdges.empty());
}

void malformed() {
    auto good = header(8, 8); piece(good, 2, 2, 1);
    auto level = load(good);
    const auto original = level;
    assert(level.surfaceWidth==7*64&&level.surfaceHeight==7*32);
    auto reject = [&](std::vector<uint8_t> input, std::vector<uint8_t> names) {
        assert(!vyruz::loadTerrain(input, names, level));
        sameGeometry(level, original);
        assert(level.hasTerrainCollision);
        assert(level.surfaceWidth==original.surfaceWidth&&level.surfaceHeight==original.surfaceHeight);
    };
    auto bad = good; bad.pop_back(); reject(bad, catalog());
    bad = good; bad[0] = 0; reject(bad, catalog());
    bad = header(8, 8); piece(bad, 8, 2, 1); reject(bad, catalog());
    bad = header(8, 8); piece(bad, 2, 1, 1); reject(bad, catalog());
    bad = header(8, 8); piece(bad, 2, 2, 0); reject(bad, catalog());
    bad = header(8, 8); piece(bad, 2, 2, 17); reject(bad, catalog());
    auto names = catalog(); names.resize(names.size() - 10); reject(good, names);
    const std::string truncated = "# GAF files\n2\nD:\\ANIM\\GROUND\\a.gaf\n# Objects\n1\n0 D:\\GROUND\\a.tga\n";
    reject(good, {truncated.begin(), truncated.end()});
    const std::string unknown = "# GAF files\n0\n# Objects\n1\n0 D:\\GROUND\\UNKNOWN\\a.tga\n";
    reject(good, {unknown.begin(), unknown.end()});
}

void movement() {
    vyruz::Level level;
    level.mapW = level.mapH = 16;
    level.hasTerrainCollision = true;
    level.collisionEdges = {{200, 0, 200, 400}};
    const float horizontalRadius = 22.f / std::sqrt(0.625f);
    const float contact = 200.f - horizontalRadius;
    assert(vyruz::terrainMoveAllowed(level, 150, 100, contact, 100, 22));
    assert(!vyruz::terrainMoveAllowed(level, 150, 100, contact + 0.01f, 100, 22));
    assert(!vyruz::terrainMoveAllowed(level, 150, 100, 250, 100, 22)); // endpoints clear, sweep crosses
    assert(!vyruz::terrainMoveAllowed(level, 250, 100, 150, 100, 22)); // other side
    assert(vyruz::terrainMoveAllowed(level, contact, 100, contact, 160, 22));
    assert(vyruz::terrainMoveAllowed(level, 190, 100, 180, 100, 22)); // initial overlap escape
    assert(!vyruz::terrainMoveAllowed(level, 190, 100, 195, 100, 22));
    assert(!vyruz::terrainMoveAllowed(level, 190, 100, 240, 100, 22)); // no escape through wall
    assert(vyruz::terrainMoveAllowed(level, 180, 100, 190, 100, 5)); // radius independently matters

    const auto hit = vyruz::moveOnTerrain(level, 150, 100, 100, 0, 22);
    assert(near(150 + hit.x, contact, 0.002f) && near(hit.y, 0));
    vyruz::TerrainContacts contacts;
    const auto stop = vyruz::moveOnTerrain(level, 150, 100, 100, 0, 22, &contacts, false);
    assert(near(stop.x, hit.x) && near(stop.y, hit.y));
    assert(contacts.count == 1);
    assert(near(contacts.values[0].x, 200) && near(contacts.values[0].y, 100));
    assert(near(contacts.values[0].normalX, std::sqrt(0.5f)));
    assert(near(contacts.values[0].normalZ, -std::sqrt(0.5f)));
    assert(!contacts.values[0].waterBoundary);
    const auto obliqueStop = vyruz::moveOnTerrain(level, contact, 100, 30, 60, 22, &contacts, false);
    assert(near(obliqueStop.x, 0, 0.002f) && near(obliqueStop.y, 0, 0.002f));
    assert(contacts.count == 1);
    vyruz::moveOnTerrain(level, contact, 100, -30, 60, 22, &contacts, false);
    assert(contacts.count == 0); // reset and no bounce when separating
    vyruz::moveOnTerrain(level, 250, 100, -100, 0, 22, &contacts, false);
    assert(contacts.count == 1 && contacts.values[0].normalX < 0 && contacts.values[0].normalZ > 0);
    const auto slide = vyruz::moveOnTerrain(level, contact, 100, 30, 60, 22);
    assert(near(slide.x, 0, 0.002f) && near(slide.y, 60, 0.002f));
    float x = contact, y = 100;
    for (int i = 0; i < 10; ++i) {
        const auto step = vyruz::moveOnTerrain(level, x, y, 3, 6, 22);
        x += step.x; y += step.y;
    }
    assert(near(x, contact + slide.x, 0.002f) && near(y, 100 + slide.y, 0.002f));

    // Another edge cannot be entered by escaping a deeper first overlap.
    level.collisionEdges.push_back({220, 0, 220, 400});
    assert(!vyruz::terrainMoveAllowed(level, 210, 100, 215, 100, 22));

    level.collisionEdges = {{200, 0, 200, 400}, {0, 200, 400, 200}};
    const auto corner = vyruz::moveOnTerrain(level, 150, 150, 100, 100, 22);
    assert(near(150 + corner.x, contact, 0.002f));
    assert(near(150 + corner.y, 200 - 22 / std::sqrt(2.5f), 0.002f));

    // Both diagonal orientations: approach from either side, tangent, escape,
    // and repeated large normal steps. Radius is measured in native coordinates.
    for (float sign : {-1.f, 1.f}) {
        level.collisionEdges = {{128, 128, 256, 128 + sign * 64}};
        const float cx = 192, cy = 128 + sign * 32;
        const float nx = -sign * 0.5f, ny = 1;
        // Native metric distance for this normal displacement is 1.397542 * scale.
        const float scale = 22.f / 1.397542486f;
        for (float side : {-1.f, 1.f}) {
            const float px = cx + side * nx * scale, py = cy + side * ny * scale;
            assert(vyruz::terrainMoveAllowed(level, px, py, px + 8, py + sign * 4, 22));
            assert(!vyruz::terrainMoveAllowed(level, px, py,
                                              px - side * nx, py - side * ny, 22));
            assert(vyruz::terrainMoveAllowed(level, px - side * nx, py - side * ny, px, py, 22));
        }
    }
    // Real-level magnitudes: translating geometry must not freeze tangent motion.
    level.mapW = level.mapH = 128;
    for (float sign : {-1.f, 1.f}) {
        level.collisionEdges = {{6000, 3000, 6128, 3000 + sign * 64}};
        const float scale = 22.f / 1.397542486f;
        const float px = 6064 - sign * 0.5f * scale, py = 3000 + sign * 32 + scale;
        const auto tangent = vyruz::moveOnTerrain(level, px, py, 32, sign * 16, 22);
        assert(near(tangent.x, 32, 0.002f) && near(tangent.y, sign * 16, 0.002f));
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    assert(!vyruz::terrainMoveAllowed(level, 100, 100, nan, 100, 22));
    assert(!vyruz::terrainMoveAllowed(level, 100, 100, 110, 100, -1));
    assert(!vyruz::terrainMoveAllowed(level, 100, 100, -1, 100, 22));
    assert(near(vyruz::moveOnTerrain(level, 100, 100, nan, 0, 22).x, 0));
    contacts.count = 2;
    vyruz::moveOnTerrain(level, 100, 100, nan, 0, 22, &contacts, false);
    assert(contacts.count == 0);
    // Endpoint contact normal is radial in native space, not a line tangent.
    level.collisionEdges = {{200, 100, 200, 200, true}};
    const auto endpoint = vyruz::moveOnTerrain(level, 150, 80, 80, 20, 22, &contacts, false);
    assert(contacts.count == 1 && contacts.values[0].waterBoundary);
    assert(near(contacts.values[0].x, 200) && near(contacts.values[0].y, 100));
    assert(vyruz::terrainMoveAllowed(level, 150, 80, 150 + endpoint.x, 80 + endpoint.y, 22));
    // Ammo Over-water passes the same shoreline that still blocks vehicles.
    assert(vyruz::terrainMoveAllowed(level,150,80,230,100,22,true));
    const auto over=vyruz::moveOnTerrain(level,150,80,80,20,22,&contacts,false,true);
    assert(near(over.x,80) && near(over.y,20) && contacts.count==0);
    // Solid walls and map extents remain blocking for Over-water bodies.
    level.collisionEdges.push_back({240,0,240,400,false});
    assert(!vyruz::terrainMoveAllowed(level,150,80,280,100,22,true));
    assert(!vyruz::terrainMoveAllowed(level,150,80,-1,100,22,true));
    // Simultaneous corners expose distinct bounded normals.
    const float cx = 200 - 22/std::sqrt(0.625f), cy = 200 - 22/std::sqrt(2.5f);
    level.collisionEdges = {{200, 0, 200, 400}, {0, 200, 400, 200}};
    vyruz::moveOnTerrain(level, cx, cy, 5, 5, 22, &contacts, false);
    assert(contacts.count == 2);

}
} // namespace

void indexedQueries() {
    vyruz::Level level;
    level.hasTerrainCollision = true;
    std::mt19937 random(419);
    std::uniform_real_distribution<float> x(0, 5000), y(0, 4000), step(-250, 250), radius(0, 100);
    for (int i = 0; i < 2200; ++i) {
        const float px=x(random), py=y(random);
        level.collisionEdges.push_back({px, py, px+step(random), py+step(random),i%2==0});
    }
    auto linear = level;
    vyruz::rebuildTerrainIndex(level);
    assert(!level.collisionEdgeTree.empty());
    // Short moves, long routes, overlap, point queries and varying radii use
    // the unchanged linear solver as an oracle, including copied indexes.
    auto indexed = level;
    for (int i = 0; i < 30000; ++i) {
        const float px=x(random), py=y(random), r=radius(random);
        const float nx=i%3==0 ? x(random) : px+step(random);
        const float ny=i%3==0 ? y(random) : py+step(random);
        assert(vyruz::terrainMoveAllowed(indexed,px,py,nx,ny,r) ==
               vyruz::terrainMoveAllowed(linear,px,py,nx,ny,r));
        assert(vyruz::terrainMoveAllowed(indexed,px,py,px,py,r) ==
               vyruz::terrainMoveAllowed(linear,px,py,px,py,r));
        assert(vyruz::terrainMoveAllowed(indexed,px,py,nx,ny,r,true) ==
               vyruz::terrainMoveAllowed(linear,px,py,nx,ny,r,true));
    }
    indexed.collisionEdges.clear();
    assert(vyruz::terrainMoveAllowed(indexed,100,100,110,110,22));
}

int main() {
    indexedQueries();
    masksAndSlots();
    zeroMaskNeighbors();
    malformed();
    movement();
}
