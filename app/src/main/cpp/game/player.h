// Gerak pemain gaya Source/CS (gesekan, akselerasi, lompat, jongkok, step 18 unit).
#pragma once
#include "collision.h"

namespace cs {

struct PlayerInput {
    float moveX = 0, moveY = 0;  // -1..1 (X = kanan, Y = maju)
    float lookYaw = 0, lookPitch = 0; // delta derajat sejak frame lalu
    bool jump = false, duck = false, walk = false;
};

class Player {
public:
    Vec3 origin, velocity;
    float pitch = 0, yaw = 0;
    bool onGround = false, ducked = false, noclip = false;

    void spawn(const Vec3& o, float yawDeg);
    void update(float dt, const PlayerInput& in, const Collision& col);
    Vec3 eye() const { return origin + Vec3(0, 0, ducked ? 46.0f : 64.0f); }
    Vec3 mins() const { return Vec3(-16, -16, 0); }
    Vec3 maxs() const { return Vec3(16, 16, ducked ? 54.0f : 72.0f); }
    float speed2D() const { return std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y); }

    float worldMinZ = -16384.f;
    Vec3 spawnOrigin;
    float spawnYaw = 0;

private:
    void friction(float dt);
    void accelerate(const Vec3& wishdir, float wishspeed, float accel, float dt);
    void slideMove(float dt, const Collision& col);
    void stepSlideMove(float dt, const Collision& col);
    void categorize(const Collision& col);
    void dispFloor(const Collision& col, float prevZ);
    bool jumpHeld_ = false;
};

} // namespace cs
