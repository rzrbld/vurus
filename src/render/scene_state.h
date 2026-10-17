#pragma once
#include "render/animation.h"
#include "render/enemy_orbits.h"
#include <optional>
#include <string>
#include <vector>

namespace vyruz {
struct SceneChannelState {
    size_t object=0; bool shadow=false; std::string bankPath; uint32_t sequence=0;
    AnimationCursorState cursor; uint64_t gateRevision=0; bool firing=false;
    uint64_t fireRevision=0; bool lift=false; uint64_t visualRevision=0;
};
// Live checkpoint state. Native disk loads recreate transient FX; these fields
// deliberately stay outside the portable SceneRendererState disk payload.
struct SceneEffectChannelState {
    SceneChannelState channel;
    float visibility=0,fadeClockMs=0;
    bool visibilityAlpha=true,walkerTurned=false;
    int flightTilt=0;
    double flightClockMs=0;
    ContentPosition flightPosition{},flightPrevious{};
};
struct SceneAttachedOrbitState {
    size_t object=0;
    SpriteReference sprite;
    EnemyOrbitState history;
    float visibility=0,fadeClockMs=0;
    bool visible=false,animationInitialized=false;
    AnimationCursorState cursor;
    ContentPosition parent{};
    float altitude=0;
};
struct SceneEffectsState {
    struct TerrainChannel {
        uint32_t x=0,y=0,slot=0,source=0,sequence=0;
        std::string bankPath;
        AnimationCursorState cursor;
    };
    // Live rollback only. Original disk loads recreate kind4 scenery ANMs.
    std::optional<std::vector<TerrainChannel>> terrain;
    double terrainClockMs=0;
    std::vector<uint32_t> terrainStartMs;
    struct EnemyLean { size_t object=0; int value=0; };
    std::vector<EnemyLean> enemyLeans;
    std::vector<SceneEffectChannelState> invisibleChannels,enemyFire;
    std::vector<SceneAttachedOrbitState> orbits;
    double orbitTimeMs=0;
    bool resetEnemyFire=false;
};
struct SceneRendererState {
    size_t objectCount=0;
    std::vector<SceneChannelState> channels;
    std::optional<SceneEffectsState> effects;
};
}
