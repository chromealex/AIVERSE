#pragma once
// Runtime physics scene for authoritative hosts: bodies come and go at run time behind stable
// handles. Independent of World (fixed JSON manifest + save/restore). Float state, one owner thread.
#include "engine/data/Json.h"
#include <box3d/box3d.h>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace aiverse::physics {
struct CharacterParams;
struct CharacterState;
struct Push;

// 0 is never a valid handle. A handle = slot index + generation, so a destroyed body's handle
// stays dead: the slot is reused only after the free list cycles and with a new generation.
using BodyHandle = uint32_t;
constexpr BodyHandle kNoBody = 0;

// Shape filter categories. Character proxies have no contacts with bodies (pushes are explicit,
// bounded impulses); they are seen by queries (kCategoryQuery) and by sensors.
enum : uint64_t {
    kCategoryStatic = 1ull << 0,
    kCategoryDynamic = 1ull << 1,
    kCategoryKinematic = 1ull << 2,
    kCategoryCharacter = 1ull << 3,
    kCategorySensor = 1ull << 4,
    kCategoryQuery = 1ull << 63,
};

enum class Motion : uint8_t { Static, Dynamic, Kinematic };
enum class ShapeKind : uint8_t { Box, Sphere, Capsule, Mesh };

struct BodyDesc {
    Motion motion = Motion::Dynamic;
    ShapeKind shape = ShapeKind::Box;
    b3Vec3 size{1, 1, 1};              // box: full extents
    float radius = 0.5f;               // sphere / capsule
    float halfHeight = 0.5f;           // capsule: half distance between the cap centres (along local Y)
    std::vector<b3Vec3> vertices;      // mesh (static only), local space
    std::vector<int32_t> indices;      // mesh: 3 per triangle, counter-clockwise seen from outside
    b3Vec3 position{0, 0, 0};
    b3Quat rotation = b3Quat_identity;
    b3Vec3 linearVelocity{0, 0, 0}, angularVelocity{0, 0, 0};
    float density = 1000, friction = 0.5f, restitution = 0;
    float linearDamping = 0.05f, angularDamping = 0.05f, gravityScale = 1;
    bool sleep = true, bullet = false, sensor = false;
    uint64_t category = 0;             // 0: by motion (kCategoryStatic/Dynamic/Kinematic, kCategorySensor)
    uint64_t mask = ~0ull;
    uint32_t userData = 0;
};

struct SceneConfig {
    b3Vec3 gravity{0, -9.81f, 0};
    int tickRate = 60;                 // 10..240
    int substeps = 4;                  // 1..16
    int workers = 1;                   // 1..8 (web: 1)
    uint32_t maxBodies = 16384;        // 1..65535
};

struct ContactEvent { BodyHandle a = kNoBody, b = kNoBody; b3Vec3 point{0, 0, 0}, normal{0, 0, 0}; }; // normal a -> b
struct SensorEvent { BodyHandle sensor = kNoBody, visitor = kNoBody; bool begin = true; };
struct MoveEvent { BodyHandle body = kNoBody; bool fellAsleep = false; };
struct RayHit { BodyHandle body = kNoBody; b3Vec3 point{0, 0, 0}, normal{0, 0, 0}; float fraction = 1; };
struct QueryFilter {
    uint64_t mask = ~0ull;             // categories to hit
    BodyHandle ignore = kNoBody;       // e.g. the shooter's own character
    bool sensors = false;              // sensors are skipped unless asked for
};

class Scene {
public:
    enum class Load { All, StaticOnly };
    explicit Scene(const SceneConfig& config = {});
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // JSON scenes in the docs/physics.md format (+ "mesh" shape for static bodies). Throws JsonError
    // with a readable message. StaticOnly keeps static bodies (and skips joints): the client copy
    // for prediction collisions.
    static SceneConfig config(const Json& scene, int workers = 1);
    static std::unique_ptr<Scene> fromJson(const Json& scene, Load load = Load::All, int workers = 1);
    static std::unique_ptr<Scene> staticCopy(const Json& scene) { return fromJson(scene, Load::StaticOnly, 1); }
    void load(const Json& scene, Load load = Load::All); // adds bodies; JSON ids resolve with find()
    BodyHandle find(const std::string& id) const;

    // Returns kNoBody for an invalid description (non-finite, out of range, mesh not static) or when
    // maxBodies are alive. Never throws.
    BodyHandle create(const BodyDesc& desc);
    bool destroy(BodyHandle h);
    bool valid(BodyHandle h) const;
    size_t bodyCount() const { return live_; }
    uint32_t maxBodies() const { return config_.maxBodies; }
    b3BodyId native(BodyHandle h) const;          // b3_nullBodyId if dead
    static BodyHandle handleOf(b3BodyId body);    // Scene handle stored in the Box3D body user data

    Motion motion(BodyHandle h) const;
    b3Vec3 position(BodyHandle h) const;
    b3Quat rotation(BodyHandle h) const;
    b3Vec3 linearVelocity(BodyHandle h) const;
    b3Vec3 angularVelocity(BodyHandle h) const;
    bool awake(BodyHandle h) const;
    uint32_t userData(BodyHandle h) const;
    bool setUserData(BodyHandle h, uint32_t value);
    bool setTransform(BodyHandle h, b3Vec3 position, b3Quat rotation);      // teleport
    bool moveKinematic(BodyHandle h, b3Vec3 position, b3Quat rotation);     // reach it over the next step
    bool setLinearVelocity(BodyHandle h, b3Vec3 v);
    bool setAngularVelocity(BodyHandle h, b3Vec3 w);
    bool applyImpulse(BodyHandle h, b3Vec3 impulse);                        // at the centre of mass
    bool applyImpulseAt(BodyHandle h, b3Vec3 impulse, b3Vec3 point);
    bool applyForce(BodyHandle h, b3Vec3 force);                            // for the next step
    bool applyTorque(BodyHandle h, b3Vec3 torque);
    bool setAwake(BodyHandle h, bool awake);

    // One fixed tick (1 / tickRate, substeps). Events below are copied and stay valid until the next step.
    void step();
    const std::vector<ContactEvent>& contactBegins() const { return contactBegins_; }
    // Sensor begin / end per (sensor, visitor). Destroying either body ends its overlaps: the end events come
    // with the next step(), first in the list, with the dead handle (compare, do not query it).
    const std::vector<SensorEvent>& sensorEvents() const { return sensorEvents_; }
    const std::vector<MoveEvent>& moved() const { return moved_; }

    // Queries skip sensors unless asked; results name live handles, sorted (all hits by fraction,
    // overlaps by handle) so they do not depend on broad-phase order.
    RayHit raycast(b3Vec3 origin, b3Vec3 translation, const QueryFilter& filter = {}) const;
    // Swept sphere, native Box3D shape query; radius 0 uses raycast.
    // Initial overlaps without a valid normal are not depenetration results.
    RayHit sphereCast(b3Vec3 origin,b3Vec3 translation,float radius,const QueryFilter& filter={}) const;
    std::vector<RayHit> raycastAll(b3Vec3 origin, b3Vec3 translation, const QueryFilter& filter = {}) const;
    std::vector<BodyHandle> overlapSphere(b3Vec3 centre, float radius, const QueryFilter& filter = {}) const;
    std::vector<BodyHandle> overlapBox(b3Vec3 centre, b3Vec3 halfExtents, b3Quat rotation, const QueryFilter& filter = {}) const;
    std::vector<BodyHandle> overlapCapsule(b3Vec3 a, b3Vec3 b, float radius, const QueryFilter& filter = {}) const;

    // Character proxy: a kinematic capsule (feet at `feet`) other characters, queries and sensors see.
    // Pass its handle as `self` to stepCharacter. moveCharacter follows the state over the next step.
    BodyHandle createCharacter(const CharacterParams& params, b3Vec3 feet, uint32_t userData = 0);
    bool moveCharacter(BodyHandle h, const CharacterState& state, bool teleport = false);
    bool moveCharacterOriented(BodyHandle h,const CharacterState& state,b3Quat localToWorld,bool teleport=false);
    // Applies pushes reported by stepCharacter: impulse along the push direction that brings the body
    // to the character's speed at that point, clamped to the push's maxImpulse.
    void applyPushes(const std::vector<Push>& pushes);

    b3WorldId world() const { return world_; }
    uint64_t tick() const { return tick_; }
    int tickRate() const { return config_.tickRate; }
    float dt() const { return 1.0f / float(config_.tickRate); }

private:
    struct Slot {
        b3BodyId id = b3_nullBodyId;
        b3MeshData* mesh = nullptr;
        uint32_t user = 0;
        uint16_t generation = 1;
        Motion motion = Motion::Static;
        bool live = false;
        std::string name;
    };
    const Slot* slot(BodyHandle h) const;
    Slot* slot(BodyHandle h) { return const_cast<Slot*>(static_cast<const Scene*>(this)->slot(h)); }
    BodyHandle bodyOfShape(b3ShapeId shape) const;
    bool acceptShape(b3ShapeId shape, const QueryFilter& filter) const;
    std::vector<BodyHandle> overlap(b3Vec3 origin, const b3Vec3* points, int count, float radius, const QueryFilter& filter) const;
    void release(Slot& s);

    SceneConfig config_;
    b3WorldId world_ = b3_nullWorldId;
    std::vector<Slot> slots_;
    std::deque<uint32_t> free_;
    std::map<std::string, BodyHandle> names_;
    size_t live_ = 0;
    uint64_t tick_ = 0;
    std::vector<ContactEvent> contactBegins_;
    std::vector<SensorEvent> sensorEvents_;
    std::vector<MoveEvent> moved_;
    // Live sensor overlaps (sensor, visitor): destroy() ends them itself (Box3D drops destroyed shapes'
    // end events); those ends are delivered with the next step's events.
    std::set<std::pair<BodyHandle, BodyHandle>> sensorPairs_;
    std::vector<SensorEvent> pendingEnds_;
};
}
