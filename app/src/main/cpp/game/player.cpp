#include "player.h"

namespace cs {

static constexpr float kGravity = 800.f, kStopSpeed = 80.f, kFriction = 5.2f, kAccel = 5.5f,
                       kAirAccel = 12.f, kMaxSpeed = 250.f, kWalkSpeed = 130.f, kDuckSpeed = 85.f,
                       kJumpSpeed = 301.993f, kStep = 18.f;

void Player::spawn(const Vec3& o, float yawDeg) {
    origin = o + Vec3(0, 0, 2);
    velocity = Vec3();
    yaw = yawDeg;
    pitch = 0;
    ducked = false;
    onGround = false;
    spawnOrigin = o;
    spawnYaw = yawDeg;
}

void Player::friction(float dt) {
    float sp = speed2D();
    if (sp < 0.1f) { velocity.x = velocity.y = 0; return; }
    float control = std::max(sp, kStopSpeed);
    float ns = std::max(0.0f, sp - control * kFriction * dt) / sp;
    velocity.x *= ns;
    velocity.y *= ns;
}

void Player::accelerate(const Vec3& wishdir, float wishspeed, float accel, float dt) {
    float cur = dot(velocity, wishdir);
    float add = wishspeed - cur;
    if (add <= 0) return;
    float a = std::min(accel * dt * wishspeed, add);
    velocity += wishdir * a;
}

static Vec3 clipVelocity(const Vec3& v, const Vec3& n, float overbounce) {
    float back = dot(v, n) * overbounce;
    Vec3 o = v - n * back;
    for (int i = 0; i < 3; ++i) if (std::fabs(o[i]) < 0.003f) o[i] = 0;
    return o;
}

void Player::slideMove(float dt, const Collision& col) {
    float timeLeft = dt;
    Vec3 planes[5];
    int np = 0;
    for (int bump = 0; bump < 4 && timeLeft > 0; ++bump) {
        Vec3 end = origin + velocity * timeLeft;
        TraceResult tr = col.traceBox(origin, end, mins(), maxs());
        if (tr.allSolid) {
            // terjebak: dorong ke atas sedikit
            origin.z += 1.0f;
            velocity.z = 0;
            return;
        }
        if (tr.fraction > 0) { origin = tr.endpos; np = 0; }
        if (tr.fraction >= 1) break;
        timeLeft -= timeLeft * tr.fraction;
        if (np >= 5) { velocity = Vec3(); break; }
        planes[np++] = tr.normal;
        velocity = clipVelocity(velocity, tr.normal, 1.0f);
        for (int i = 0; i < np - 1; ++i)
            if (dot(velocity, planes[i]) < 0) {
                Vec3 dir = normalize(cross(planes[i], planes[np - 1]));
                velocity = dir * dot(dir, velocity);
            }
    }
}

void Player::stepSlideMove(float dt, const Collision& col) {
    Vec3 startO = origin, startV = velocity;
    slideMove(dt, col);
    if (!onGround) return;
    Vec3 downO = origin, downV = velocity;

    // coba lagi dari posisi naik 18 unit
    origin = startO;
    velocity = startV;
    TraceResult up = col.traceBox(origin, origin + Vec3(0, 0, kStep), mins(), maxs());
    if (up.allSolid) { origin = downO; velocity = downV; return; }
    origin = up.endpos;
    slideMove(dt, col);
    TraceResult down = col.traceBox(origin, origin - Vec3(0, 0, kStep + 1), mins(), maxs());
    if (!down.allSolid) origin = down.endpos;
    if (down.fraction < 1 && down.normal.z < 0.7f) { origin = downO; velocity = downV; return; }
    float dDown = (downO.x - startO.x) * (downO.x - startO.x) + (downO.y - startO.y) * (downO.y - startO.y);
    float dUp = (origin.x - startO.x) * (origin.x - startO.x) + (origin.y - startO.y) * (origin.y - startO.y);
    if (dDown >= dUp) { origin = downO; velocity = downV; }
    else velocity.z = downV.z;
}

void Player::categorize(const Collision& col) {
    if (velocity.z > 180) { onGround = false; return; }
    TraceResult tr = col.traceBox(origin, origin - Vec3(0, 0, 2), mins(), maxs());
    onGround = tr.fraction < 1 && tr.normal.z >= 0.7f && !tr.allSolid;
    if (onGround) { origin = tr.endpos; if (velocity.z < 0) velocity.z = 0; }
}

void Player::dispFloor(const Collision& col, float prevZ) {
    float fz;
    Vec3 n;
    float top = std::max(prevZ, origin.z) + kStep;
    float bottom = origin.z - (velocity.z <= 0 ? 2.0f : 0.0f);
    if (col.dispFloor(origin.x, origin.y, top, bottom, fz, n)) {
        if (n.z >= 0.7f) {
            if (velocity.z <= 0 && fz >= origin.z - 2.0f) {
                origin.z = fz;
                velocity.z = 0;
                onGround = true;
            }
        } else if (fz > prevZ + 2.0f) {
            // lereng terlalu curam: hentikan gerak horizontal
            velocity.x = velocity.y = 0;
        }
    }
}

void Player::update(float dt, const PlayerInput& in, const Collision& col) {
    dt = std::min(dt, 0.05f);
    yaw -= in.lookYaw;
    pitch += in.lookPitch;
    pitch = std::max(-89.f, std::min(89.f, pitch));
    if (yaw > 360.f) yaw -= 360.f;
    if (yaw < 0.f) yaw += 360.f;

    float yr = yaw * (float)M_PI / 180.f, pr = pitch * (float)M_PI / 180.f;
    Vec3 fwd(std::cos(yr), std::sin(yr), 0), right(std::sin(yr), -std::cos(yr), 0);
    float mag = std::min(1.0f, std::sqrt(in.moveX * in.moveX + in.moveY * in.moveY));

    if (noclip) {
        Vec3 look(std::cos(pr) * std::cos(yr), std::cos(pr) * std::sin(yr), -std::sin(pr));
        Vec3 wish = look * in.moveY + right * in.moveX;
        float sp = in.walk ? 300.f : 900.f;
        if (in.jump) wish.z += 1;
        if (in.duck) wish.z -= 1;
        velocity = normalize(wish) * (sp * std::min(1.0f, length(wish)));
        origin += velocity * dt;
        onGround = false;
        return;
    }

    // jongkok / berdiri
    if (in.duck && !ducked) { ducked = true; if (!onGround) origin.z += 18; }
    else if (!in.duck && ducked) {
        Vec3 o2 = onGround ? origin : origin - Vec3(0, 0, 18);
        TraceResult tr = col.traceBox(o2, o2, Vec3(-16, -16, 0), Vec3(16, 16, 72));
        if (!tr.startSolid) { ducked = false; origin = o2; }
    }

    Vec3 wishdir = normalize(fwd * in.moveY + right * in.moveX);
    float wishspeed = (ducked ? kDuckSpeed : in.walk ? kWalkSpeed : kMaxSpeed) * mag;

    float prevZ = origin.z;
    if (in.jump && onGround && !jumpHeld_) {
        velocity.z = kJumpSpeed;
        onGround = false;
    }
    jumpHeld_ = in.jump;

    if (onGround) {
        friction(dt);
        accelerate(wishdir, wishspeed, kAccel, dt);
        velocity.z = 0;
    } else {
        accelerate(wishdir, std::min(wishspeed, 30.0f), kAirAccel, dt);
        velocity.z -= kGravity * dt * 0.5f;
    }
    stepSlideMove(dt, col);
    if (!onGround) velocity.z -= kGravity * dt * 0.5f;
    categorize(col);
    dispFloor(col, prevZ);

    if (origin.z < worldMinZ - 1024.f) spawn(spawnOrigin, spawnYaw);
}

} // namespace cs
