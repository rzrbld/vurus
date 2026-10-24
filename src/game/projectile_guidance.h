#pragma once
#include "game/content.h"
#include <cstddef>
#include <optional>
#include <span>

namespace vyruz {
// 41e1e0 uses the SHOOTER's cell/heading, not the emitted projectile's muzzle.
struct GuidanceLaunchQuery {
    ContentPosition position{};
    int cellX=0,cellY=0,heading=0,mapWidth=0,mapHeight=0,rings=20;
    bool playerShooter=false,shooterEligible=true;
    std::optional<ClassId> targetClass;
};
struct GuidanceLaunchCandidate {
    size_t id=0;
    ClassId classId{};
    ContentPosition position{};
    int cellX=0,cellY=0;
    // eligible represents membership in the native candidate grid. Do not
    // silently add line-of-sight, distance, invisibility or nearest tests here.
    bool eligible=true,countsAsEnemy=false,ordinaryActor=true;
    bool classedActor=true,visible=true;
    int activationDelayMs=0;
    std::optional<ContentPosition> nativeCenter; // original stored +0x40/+0x44
};
// Native order: expanding square rings, top-to-bottom rows, left-to-right cells,
// then the cell's linked list. Caller span order breaks ties inside a cell.
// World supplies maintained cell chains; actor IDs do not break same-cell ties.
std::optional<size_t> selectGuidanceLaunchTarget(const GuidanceLaunchQuery&,
                                               std::span<const GuidanceLaunchCandidate>);
struct GuidanceTrackingQuery {
    ContentPosition position{}; // guidance center, normally projectile position
    int cellX=0,cellY=0,heading=0,mapWidth=0,mapHeight=0;
    int sensorRange=0,visionDegrees=360;
    std::optional<ClassId> targetClass;
    std::optional<ContentPosition> nativeCenter;
};
// 42da80 scans x-major/y-minor cells, then linked/span order; no closest-target sort.
std::optional<size_t> selectGuidanceTrackingTarget(const GuidanceTrackingQuery&,
                                                 std::span<const GuidanceLaunchCandidate>);
}
