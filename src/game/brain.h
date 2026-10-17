#pragma once
#include "game/content.h"
#include <optional>
#include <span>
#include <functional>

namespace vyruz {
enum class BrainBase { Fixed, Patrol, Straight, Random, Unsupported };
struct BrainConfig {
    BrainBase base = BrainBase::Unsupported;
    bool seeker = false, tracker = false, guardian = false, guardianCenterFollowsActor = false;
    std::optional<ClassId> targetClass;
    float sensorRange = 0, minimumDistance = 0, scanDelayMs = 250;
    float visionDegrees = 360, guardianRange = 0;
    ContentPosition home{}, center{};
    int pathId = -1;
    bool reverse = false, closed = false;
    float randomRange = 0, randomDelayMs = 0;
};
struct BrainState {
    float scanRemainingMs = 0;
    bool acquired = false, initialized = false, reverse = false;
    int point = -1;
};
struct BrainInput {
    float dtMs = 0;
    ContentPosition position{}, targetPosition{};
    int facing = 0;
    ClassId targetClass{};
    bool targetAlive = true, targetVisible = true, directRoute = true;
    float weaponRange = 0;
    std::span<const ContentPosition> path;
    // Supplied by movement: arrival is based on the executed route, not dt.
    bool reachedDestination = false;
    // Native Spider seeker admission is narrower than detecting a target.
    // Rejected routes leave the base patrol running; admitted Spiders attack
    // from rest. Other enemy consumers retain their existing movement policy.
    bool seekerRouteAccepted = true, stationaryAttack = false;
    // Spider facing is its visible 24-pose token minus one. Logical heading
    // is the containing six-pose sector; firing waits for the sector endpoint.
    bool spiderFacing = false;
    // 4406e0 runs an active seeker before its Fixed idle base (Walker audit).
    bool fixedSeekerMoves = false;
};
struct BrainIntent {
    std::optional<ContentPosition> destination, aim;
    bool fire = false, unsupported = false;
    bool patrolDestination = false;
};
BrainConfig makeBrainConfig(const ObjectPlacement&, const SceneContent&);
BrainIntent advanceBrain(const BrainConfig&, BrainState&, const BrainInput&);
float nativeDistanceSquared(ContentPosition, ContentPosition);
// Pure native candidate primitive. Callback returns an integer in [low, high].
// Eligibility must include the native grid mask and dynamic-occupancy checks.
std::optional<ContentPosition> chooseRandomDestination(ContentPosition position,
    float range, int mapWidth, int mapHeight,
    const std::function<int(int low,int high)>& randomInclusive,
    const std::function<bool(int x,int y)>& cellEligible);
} // namespace vyruz
