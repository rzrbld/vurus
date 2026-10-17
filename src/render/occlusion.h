#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace vyruz {
struct Level;
// Native 420200 marker predicate at the current body bitmap's world centre.
// This tests authored tile metadata, not a pixel coverage percentage.
bool coveredPlayerCenter(const Level& level, float x, float y);
struct ForegroundTile {
    int x, y, layer;
    uint32_t tile; // zero-based bitmap index
    int phase;
};

// Single ordinary body specialization of the native row compositor.
// Bounds are a half-open sprite rectangle in world/surface pixels.
std::vector<ForegroundTile> foregroundTiles(const Level& level, float groundY,
                                           float left, float top,
                                           float right, float bottom);

struct SpriteBounds {
    int key = 0; // ground Y * 4 | native subtype
    float left = 0, top = 0, right = 0, bottom = 0;
};
struct CompositeCommand {
    bool isSprite = false;
    size_t sprite = 0;
    ForegroundTile tile{};
};
// Complete visible queue merge using accumulated dirty cells.
std::vector<CompositeCommand> composeObjects(const Level& level,
                                            const std::vector<SpriteBounds>& sprites,
                                            float viewLeft, float viewTop,
                                            float viewRight, float viewBottom);
} // namespace vyruz
