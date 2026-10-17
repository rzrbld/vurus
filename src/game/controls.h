#pragma once

namespace vyruz {

// Surface coordinates increase right/down; commands are relative to aim.
struct MovementCommand {
    float forward = 0;
    float strafe = 0;
    bool aimActive = false;
    float aimX = 0.f;
    float aimY = 0.f;
};

struct PlayerMotion {
    int direction = 0; // 24 GAF directions: 0 SW, 3 S, 9 E, 15 N, 21 W
    float turnSeconds = 0.f;
    float velocityX = 0.f; // native internal x/z units per nominal 14 ms
    float velocityZ = 0.f;
    float magnitude = 0.f; // previous solver magnitude, before grip
    float thrust = 0.f;    // signed, independent of velocity
};

struct PlayerMotionConfig {
    // Thrust block bounds (class +3c/+40), distinct from Speed-min/max.
    float speedMin = -10.f, speedMax = 45.f;
    float acceleration = 1.f, deceleration = .5f;
    float weight = 15.f, grip = 1.f;
    float frictionX = .972f, frictionZ = .972f;
    float thresholdX = .01f, thresholdZ = .01f;
    float speedThreshold = 45.f;
    float turnSeconds = .018f; // authored visual half-pose interval
    bool inertia = true;
    bool reverseTurns = false;
};

struct Displacement { float x = 0.f; float y = 0.f; };
Displacement nativeToSurface(float x, float z);
// The y member of these two results denotes native z, not surface y.
Displacement surfaceToNative(float x, float y);
Displacement nativeHeading(int direction);
int directionFromVector(float x, float y);
// Original projected heading lookup and native momentum equations (438b20).
int nativeDirectionFromVector(float x,float y);
Displacement advanceNativePlayerMotion(PlayerMotion& motion, const MovementCommand& command,
                                 float dt, const PlayerMotionConfig& config = {});

// Live player path. Fixed host cadence and swept collision remain port adapters.
Displacement advancePlayerMotion(PlayerMotion&,const MovementCommand&,float dt,
                                 const PlayerMotionConfig& config = {});

} // namespace vyruz
