#pragma once
// Kinematic capsule character controller on the Box3D mover API (CastMover / CollideMover /
// SolvePlanes). stepCharacter depends only on its arguments and the world geometry, so a client
// runs the same step for prediction against Scene::staticCopy of the place's JSON.
#include "engine/physics/Scene.h"
#include <cstdint>
#include <vector>

namespace aiverse::physics {
// Units: metres, seconds, radians, newton-seconds. +Y up. position = the feet (capsule bottom).
struct CharacterParams {
    float radius = 0.35f;
    float height = 1.8f;               // whole capsule, caps included (>= 2 * radius + 0.01)
    float walkSpeed = 4.0f;
    float runSpeed = 7.0f;
    float acceleration = 40.0f;        // m/s^2 towards the wanted horizontal velocity (also braking)
    float airControl = 0.3f;           // fraction of acceleration in the air, 0..1
    float jumpSpeed = 6.0f;            // apex ~ jumpSpeed^2 / (2 gravity)
    float gravity = 20.0f;             // m/s^2, own gravity (the world's is for bodies)
    float maxSlope = 0.8726646f;       // 50 degrees: steeper surfaces are walls you slide down
    float stepHeight = 0.35f;          // ledges up to this are climbed while walking
    float pushImpulseLimit = 20.0f;    // per tick and body
    float maxSpeed = 60.0f;            // hard clamp of the stored velocity (hostile / runaway state)
    uint64_t mask = kCategoryStatic | kCategoryDynamic | kCategoryKinematic | kCategoryCharacter;
};

struct CharacterState {
    b3Vec3 position{0, 0, 0};
    b3Vec3 velocity{0, 0, 0};          // own velocity (platform motion is added on top while grounded)
    float yaw = 0;
    bool grounded = false;
    BodyHandle ground = kNoBody;       // Scene handle of the walkable surface (output only)
    // Output: yaw the ground turned the character by this step (a rotating platform); yaw = input yaw + this.
    // Input yaw is absolute, so the game adds it to its look yaw for the next input frame.
    float groundTurn = 0;
};

// moveX = strafe right, moveZ = forward, both in [-1, 1] relative to yaw. yaw = 0 looks along -Z,
// increasing yaw turns left (counter-clockwise seen from above). jump is held, not edge triggered.
struct CharacterInput {
    float moveX = 0, moveZ = 0, yaw = 0;
    bool jump = false, run = false;

    static constexpr size_t kPackedSize = 5; // int8 x, int8 z, uint16 yaw, flags
    std::vector<uint8_t> pack() const;
    // Short or hostile bytes never fail: missing bytes read as neutral input, values are clamped.
    static CharacterInput unpack(const uint8_t* data, size_t size);
    static CharacterInput unpack(const std::vector<uint8_t>& bytes) { return unpack(bytes.data(), bytes.size()); }
    CharacterInput quantized() const { auto b = pack(); return unpack(b); } // what the server sees
};

// A dynamic body the character walked into this step. Hand to Scene::applyPushes.
struct Push {
    BodyHandle body = kNoBody;
    b3Vec3 point{0, 0, 0};             // world contact point
    b3Vec3 direction{0, 0, 0};         // horizontal unit vector, character -> body
    float speed = 0;                   // wanted speed of the body along direction
    float maxImpulse = 0;
};
using Pushes = std::vector<Push>;

bool validParams(const CharacterParams& params);
// The controller capsule relative to the feet.
b3Capsule characterCapsule(const CharacterParams& params);

// One fixed step. `self` is the character's own Scene proxy (createCharacter) that the queries skip;
// kNoBody when the world has none (prediction). Invalid params, dt <= 0 or non-finite state leave the
// position unchanged and zero the velocity.
void stepCharacter(b3WorldId world, const CharacterParams& params, CharacterState& state, const CharacterInput& input,
                   float dt, Pushes* out = nullptr, BodyHandle self = kNoBody);
// Same motor in a caller-selected orthonormal frame. local +Y is support up;
// yaw/input are frame-local; position, velocity and pushes remain world-space.
// Caller supplies gravity magnitude in params. No independent gravity policy.
bool stepCharacterOriented(b3WorldId world,const CharacterParams& params,CharacterState& state,
    const CharacterInput& input,float dt,b3Quat localToWorld,Pushes* out=nullptr,BodyHandle self=kNoBody);
// Collision-only 3D movement for existing swim/flight/zero-G velocity owners.
// Input velocity is world-space; no gravity, ground snapping or step-up.
// Does not move the proxy or step the Scene; caller commits once on success.
bool sweepCharacterVelocityOriented(b3WorldId world,const CharacterParams& params,
    CharacterState& state,b3Vec3 worldVelocity,float dt,b3Quat localToWorld,BodyHandle self=kNoBody);
}
