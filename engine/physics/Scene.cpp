#include "Scene.h"
#include "Character.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace aiverse::physics {
namespace {
constexpr float kMaxCoord = 100000, kMaxSpeed = 1000;
constexpr size_t kMaxMeshVertices = 65536, kMaxMeshTriangles = 131072;
constexpr size_t kReuseAfter = 256; // free slots kept cold before one is reused

bool finite(float x, float limit) { return std::isfinite(x) && std::abs(x) <= limit; }
bool finite(b3Vec3 v, float limit) { return finite(v.x, limit) && finite(v.y, limit) && finite(v.z, limit); }
bool inRange(float x, float lo, float hi) { return std::isfinite(x) && x >= lo && x <= hi; }
b3Vec3 add(b3Vec3 a, b3Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
b3Vec3 sub(b3Vec3 a, b3Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
b3Vec3 scale(b3Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(b3Vec3 a, b3Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
bool normalizeQuat(b3Quat& q) {
    float l = std::sqrt(q.v.x * q.v.x + q.v.y * q.v.y + q.v.z * q.v.z + q.s * q.s);
    if (!std::isfinite(l) || l < 1e-6f) return false;
    q = {{q.v.x / l, q.v.y / l, q.v.z / l}, q.s / l};
    return true;
}
b3QueryFilter queryFilter(uint64_t mask) { return {kCategoryQuery, mask, 0, nullptr}; }

// JSON helpers in the World loader's style: every error names the body and the key.
[[noreturn]] void fail(const std::string& msg) { throw JsonError("physics: " + msg); }
void check(bool ok, const std::string& msg) { if (!ok) fail(msg); }
float number(const Json& j, const char* key, float fallback, float lo, float hi, const std::string& where) {
    if (!j.has(key)) return fallback;
    check(j[key].isNumber(), where + std::string(key) + " must be a number");
    double v = j[key].toDouble();
    check(std::isfinite(v) && v >= lo && v <= hi, where + std::string(key) + " out of range [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
    return float(v);
}
b3Vec3 vec(const Json& j, b3Vec3 fallback, float limit, const std::string& what) {
    if (j.isNull()) return fallback;
    check(j.isArray() && j.size() == 3, what + " must be an array of three numbers");
    float out[3];
    for (int i = 0; i < 3; ++i) {
        check(j[size_t(i)].isNumber(), what + " must be numeric");
        double x = j[size_t(i)].toDouble();
        check(std::isfinite(x) && std::abs(x) <= limit, what + " out of range");
        out[i] = float(x);
    }
    return {out[0], out[1], out[2]};
}
}

Scene::Scene(const SceneConfig& config) : config_(config) {
    config_.tickRate = std::clamp(config_.tickRate, 10, 240);
    config_.substeps = std::clamp(config_.substeps, 1, 16);
    config_.workers = std::clamp(config_.workers, 1, 8);
#ifdef __EMSCRIPTEN__
    config_.workers = 1;
#endif
    config_.maxBodies = std::clamp<uint32_t>(config_.maxBodies, 1, 65535);
    if (!finite(config_.gravity, 1000)) config_.gravity = {0, -9.81f, 0};
    auto def = b3DefaultWorldDef();
    def.workerCount = uint32_t(config_.workers);
    def.gravity = config_.gravity;
    world_ = b3CreateWorld(&def);
}

Scene::~Scene() {
    if (b3World_IsValid(world_)) b3DestroyWorld(world_); // owns bodies and shapes
    for (auto& s : slots_) if (s.mesh) b3DestroyMesh(s.mesh);
}

SceneConfig Scene::config(const Json& scene, int workers) {
    check(scene.isObject(), "scene must be an object");
    SceneConfig c;
    float rate = number(scene, "tickRate", 60, 10, 240, ""), steps = number(scene, "substeps", 4, 1, 16, "");
    check(rate == std::floor(rate) && steps == std::floor(steps), "tickRate and substeps must be integers");
    c.tickRate = int(rate);
    c.substeps = int(steps);
    c.gravity = vec(scene["gravity"], {0, -9.81f, 0}, 1000, "gravity");
    check(workers >= 1 && workers <= 8, "workers must be 1..8");
    c.workers = workers;
    if (scene.has("maxBodies")) c.maxBodies = uint32_t(number(scene, "maxBodies", 16384, 1, 65535, ""));
    return c;
}

std::unique_ptr<Scene> Scene::fromJson(const Json& scene, Load load, int workers) {
    auto s = std::make_unique<Scene>(config(scene, workers));
    s->load(scene, load);
    return s;
}

void Scene::load(const Json& scene, Load load) {
    check(scene.isObject(), "scene must be an object");
    const auto& bodies = scene["bodies"];
    check(bodies.isNull() || (bodies.isArray() && bodies.size() <= config_.maxBodies), "bodies must be an array of at most maxBodies objects");
    std::vector<BodyHandle> created;
    try {
        for (size_t i = 0; i < bodies.size(); ++i) {
            const auto& j = bodies[i];
            check(j.isObject(), "bodies[" + std::to_string(i) + "] must be an object");
            std::string id = j.getStr("id");
            check(!id.empty(), "bodies[" + std::to_string(i) + "]: id must be a nonempty string");
            check(!names_.count(id), "body '" + id + "': duplicate id");
            std::string where = "body '" + id + "': ";
            BodyDesc d;
            auto mode = j.getStr("motion", "dynamic");
            check(mode == "static" || mode == "dynamic" || mode == "kinematic", where + "unknown motion '" + mode + "'");
            d.motion = mode == "static" ? Motion::Static : mode == "dynamic" ? Motion::Dynamic : Motion::Kinematic;
            if (load == Load::StaticOnly && d.motion != Motion::Static) continue;
            auto shape = j.getStr("shape", "box");
            d.position = vec(j["position"], {0, 0, 0}, kMaxCoord, where + "position");
            d.linearVelocity = vec(j["velocity"], {0, 0, 0}, 500, where + "velocity");
            d.angularVelocity = vec(j["angularVelocity"], {0, 0, 0}, 100, where + "angularVelocity");
            d.linearDamping = number(j, "linearDamping", 0.05f, 0, 100, where);
            d.angularDamping = number(j, "angularDamping", 0.05f, 0, 100, where);
            d.sleep = j.getBool("sleep", true);
            d.bullet = j.getBool("bullet", false);
            d.sensor = j.getBool("sensor", false);
            auto axis = vec(j["axis"], {0, 1, 0}, 1, where + "axis");
            float angle = number(j, "angle", 0, -100, 100, where);
            float len = std::sqrt(dot(axis, axis));
            check(len > 0.001f, where + "rotation axis is zero");
            d.rotation = b3MakeQuatFromAxisAngle(scale(axis, 1 / len), angle);
            d.density = number(j, "density", 1000, 0.01f, 100000, where);
            d.friction = number(j, "friction", 0.5f, 0, 2, where);
            d.restitution = number(j, "restitution", 0, 0, 1, where);
            if (j.has("userData")) d.userData = uint32_t(number(j, "userData", 0, 0, 4294967040.0f, where));
            if (shape == "box") {
                d.shape = ShapeKind::Box;
                d.size = vec(j["size"], {1, 1, 1}, 10000, where + "size");
                check(d.size.x >= 0.02f && d.size.y >= 0.02f && d.size.z >= 0.02f, where + "box size must be at least 0.02");
            } else if (shape == "sphere") {
                d.shape = ShapeKind::Sphere;
                d.radius = number(j, "radius", 0.5f, 0.01f, 100, where);
            } else if (shape == "capsule") {
                d.shape = ShapeKind::Capsule;
                d.radius = number(j, "radius", 0.4f, 0.01f, 100, where);
                d.halfHeight = number(j, "halfHeight", 0.6f, 0.01f, 100, where);
            } else if (shape == "mesh") {
                d.shape = ShapeKind::Mesh;
                check(d.motion == Motion::Static, where + "mesh shape requires static motion");
                const auto& v = j["vertices"];
                const auto& ix = j["indices"];
                check(v.isArray() && v.size() >= 3 && v.size() <= kMaxMeshVertices, where + "vertices must be an array of 3.." + std::to_string(kMaxMeshVertices) + " points");
                check(ix.isArray() && ix.size() >= 3 && ix.size() % 3 == 0 && ix.size() <= 3 * kMaxMeshTriangles, where + "indices must hold 3 per triangle");
                for (size_t k = 0; k < v.size(); ++k) d.vertices.push_back(vec(v[k], {0, 0, 0}, kMaxCoord, where + "vertices[" + std::to_string(k) + "]"));
                for (size_t k = 0; k < ix.size(); ++k) {
                    check(ix[k].isNumber(), where + "indices must be integers");
                    double x = ix[k].toDouble();
                    check(x == std::floor(x) && x >= 0 && x < double(v.size()), where + "index " + std::to_string(k) + " out of range");
                    d.indices.push_back(int32_t(x));
                }
            } else {
                fail(where + "unsupported shape '" + shape + "'");
            }
            BodyHandle h = create(d);
            check(h != kNoBody, where + (live_ >= config_.maxBodies ? "body limit reached" : "rejected by Box3D (degenerate geometry?)"));
            created.push_back(h);
            slot(h)->name = id;
            names_[id] = h;
        }
        const auto& joints = scene["joints"];
        check(joints.isNull() || (joints.isArray() && joints.size() <= 4096), "joints must be an array of at most 4096 objects");
        if (load == Load::All) {
            for (size_t i = 0; i < joints.size(); ++i) {
                const auto& j = joints[i];
                std::string where = "joints[" + std::to_string(i) + "]: ";
                auto a = native(find(j.getStr("a"))), b = native(find(j.getStr("b")));
                check(b3Body_IsValid(a) && b3Body_IsValid(b) && !B3_ID_EQUALS(a, b), where + "a and b must name two different bodies");
                auto kind = j.getStr("kind", "distance");
                auto aa = vec(j["anchorA"], {0, 0, 0}, kMaxCoord, where + "anchorA"), ab = vec(j["anchorB"], {0, 0, 0}, kMaxCoord, where + "anchorB");
                if (kind == "distance") {
                    auto d = b3DefaultDistanceJointDef();
                    d.base.bodyIdA = a; d.base.bodyIdB = b; d.base.localFrameA.p = aa; d.base.localFrameB.p = ab;
                    d.length = number(j, "length", 1, 0.01f, 1000, where);
                    b3CreateDistanceJoint(world_, &d);
                } else if (kind == "hinge") {
                    auto d = b3DefaultRevoluteJointDef();
                    d.base.bodyIdA = a; d.base.bodyIdB = b; d.base.localFrameA.p = aa; d.base.localFrameB.p = ab;
                    b3CreateRevoluteJoint(world_, &d);
                } else {
                    fail(where + "unsupported joint kind '" + kind + "'");
                }
            }
        }
    } catch (...) {
        for (auto h : created) destroy(h); // load is all or nothing
        throw;
    }
}

BodyHandle Scene::find(const std::string& id) const {
    auto it = names_.find(id);
    return it == names_.end() || !valid(it->second) ? kNoBody : it->second;
}

const Scene::Slot* Scene::slot(BodyHandle h) const {
    uint32_t index = (h & 0xFFFFu);
    if (index == 0 || index > slots_.size()) return nullptr;
    const Slot& s = slots_[index - 1];
    return s.live && s.generation == (h >> 16) ? &s : nullptr;
}

bool Scene::valid(BodyHandle h) const { return slot(h) != nullptr; }
b3BodyId Scene::native(BodyHandle h) const { auto* s = slot(h); return s ? s->id : b3_nullBodyId; }
BodyHandle Scene::handleOf(b3BodyId body) {
    if (!b3Body_IsValid(body)) return kNoBody;
    return BodyHandle(uintptr_t(b3Body_GetUserData(body)) & 0xFFFFFFFFu);
}

BodyHandle Scene::create(const BodyDesc& d) {
    if (live_ >= config_.maxBodies) return kNoBody;
    if (!finite(d.position, kMaxCoord) || !finite(d.linearVelocity, kMaxSpeed) || !finite(d.angularVelocity, kMaxSpeed)) return kNoBody;
    b3Quat q = d.rotation;
    if (!normalizeQuat(q)) return kNoBody;
    if (!inRange(d.density, 1e-4f, 1e6f) || !inRange(d.friction, 0, 10) || !inRange(d.restitution, 0, 1) ||
        !inRange(d.linearDamping, 0, 1000) || !inRange(d.angularDamping, 0, 1000) || !inRange(d.gravityScale, -100, 100)) return kNoBody;
    switch (d.shape) {
    case ShapeKind::Box:
        if (!inRange(d.size.x, 0.01f, 20000) || !inRange(d.size.y, 0.01f, 20000) || !inRange(d.size.z, 0.01f, 20000)) return kNoBody;
        break;
    case ShapeKind::Sphere:
        if (!inRange(d.radius, 0.005f, 1000)) return kNoBody;
        break;
    case ShapeKind::Capsule:
        if (!inRange(d.radius, 0.005f, 1000) || !inRange(d.halfHeight, 0.005f, 1000)) return kNoBody;
        break;
    case ShapeKind::Mesh:
        if (d.motion != Motion::Static || d.sensor || d.vertices.size() < 3 || d.vertices.size() > kMaxMeshVertices ||
            d.indices.size() < 3 || d.indices.size() % 3 || d.indices.size() > 3 * kMaxMeshTriangles) return kNoBody;
        for (auto& v : d.vertices) if (!finite(v, kMaxCoord)) return kNoBody;
        for (auto i : d.indices) if (i < 0 || size_t(i) >= d.vertices.size()) return kNoBody;
        break;
    default:
        return kNoBody;
    }

    uint32_t index;
    if (!free_.empty() && (free_.size() >= kReuseAfter || slots_.size() >= config_.maxBodies)) {
        index = free_.front();
        free_.pop_front();
    } else {
        index = uint32_t(slots_.size());
        slots_.emplace_back();
    }
    Slot& s = slots_[index];
    BodyHandle h = (BodyHandle(s.generation) << 16) | (index + 1);

    auto bd = b3DefaultBodyDef();
    bd.type = d.motion == Motion::Static ? b3_staticBody : d.motion == Motion::Dynamic ? b3_dynamicBody : b3_kinematicBody;
    bd.position = d.position;
    bd.rotation = q;
    if (d.motion != Motion::Static) { bd.linearVelocity = d.linearVelocity; bd.angularVelocity = d.angularVelocity; }
    bool kinematic = d.motion == Motion::Kinematic; // damping would slow a platform set to a velocity
    bd.linearDamping = kinematic ? 0 : d.linearDamping;
    bd.angularDamping = kinematic ? 0 : d.angularDamping;
    bd.gravityScale = d.gravityScale;
    bd.enableSleep = d.sleep;
    bd.isBullet = d.bullet;
    bd.userData = reinterpret_cast<void*>(uintptr_t(h));
    b3BodyId body = b3CreateBody(world_, &bd);
    if (!b3Body_IsValid(body)) { free_.push_front(index); return kNoBody; }

    auto sd = b3DefaultShapeDef();
    sd.density = d.density;
    sd.baseMaterial.friction = d.friction;
    sd.baseMaterial.restitution = d.restitution;
    sd.isSensor = d.sensor;
    uint64_t category = d.category ? d.category :
        d.sensor ? kCategorySensor : d.motion == Motion::Static ? kCategoryStatic : d.motion == Motion::Dynamic ? kCategoryDynamic : kCategoryKinematic;
    sd.filter.categoryBits = category;
    sd.filter.maskBits = d.mask;
    sd.enableContactEvents = !d.sensor;
    sd.enableSensorEvents = d.sensor || d.motion != Motion::Static;
    b3ShapeId shape = b3_nullShapeId;
    b3MeshData* mesh = nullptr;
    switch (d.shape) {
    case ShapeKind::Box: {
        auto hull = b3MakeBoxHull(d.size.x / 2, d.size.y / 2, d.size.z / 2);
        shape = b3CreateHullShape(body, &sd, &hull.base);
        break;
    }
    case ShapeKind::Sphere: {
        b3Sphere sphere{{0, 0, 0}, d.radius};
        shape = b3CreateSphereShape(body, &sd, &sphere);
        break;
    }
    case ShapeKind::Capsule: {
        b3Capsule c{{0, -d.halfHeight, 0}, {0, d.halfHeight, 0}, d.radius};
        shape = b3CreateCapsuleShape(body, &sd, &c);
        break;
    }
    case ShapeKind::Mesh: {
        b3MeshDef md{};
        std::vector<b3Vec3> verts = d.vertices;
        std::vector<int32_t> idx = d.indices;
        md.vertices = verts.data();
        md.indices = idx.data();
        md.vertexCount = int(verts.size());
        md.triangleCount = int(idx.size() / 3);
        md.weldVertices = true;
        md.weldTolerance = 0.001f;
        md.identifyEdges = true;
        mesh = b3CreateMesh(&md, nullptr, 0);
        if (mesh) shape = b3CreateMeshShape(body, &sd, mesh, {1, 1, 1});
        break;
    }
    }
    if (!b3Shape_IsValid(shape)) {
        b3DestroyBody(body);
        if (mesh) b3DestroyMesh(mesh);
        if (index + 1 == slots_.size() && free_.empty()) slots_.pop_back(); else free_.push_front(index);
        return kNoBody;
    }
    s.id = body;
    s.mesh = mesh;
    s.user = d.userData;
    s.motion = d.motion;
    s.live = true;
    s.name.clear();
    ++live_;
    return h;
}

void Scene::release(Slot& s) {
    b3DestroyBody(s.id);
    if (s.mesh) b3DestroyMesh(s.mesh); // after its shape is gone
    if (!s.name.empty()) names_.erase(s.name);
    uint16_t next = uint16_t(s.generation + 1);
    s = Slot{};
    s.generation = next ? next : 1; // 0 never appears in a handle
    --live_;
}

bool Scene::destroy(BodyHandle h) {
    Slot* s = slot(h);
    if (!s) return false;
    for (auto it = sensorPairs_.begin(); it != sensorPairs_.end();) {
        if (it->first != h && it->second != h) { ++it; continue; }
        pendingEnds_.push_back({it->first, it->second, false});
        it = sensorPairs_.erase(it);
    }
    release(*s);
    free_.push_back((h & 0xFFFFu) - 1);
    return true;
}

Motion Scene::motion(BodyHandle h) const { auto* s = slot(h); return s ? s->motion : Motion::Static; }
b3Vec3 Scene::position(BodyHandle h) const { auto* s = slot(h); return s ? b3Body_GetPosition(s->id) : b3Vec3{0, 0, 0}; }
b3Quat Scene::rotation(BodyHandle h) const { auto* s = slot(h); return s ? b3Body_GetRotation(s->id) : b3Quat_identity; }
b3Vec3 Scene::linearVelocity(BodyHandle h) const { auto* s = slot(h); return s ? b3Body_GetLinearVelocity(s->id) : b3Vec3{0, 0, 0}; }
b3Vec3 Scene::angularVelocity(BodyHandle h) const { auto* s = slot(h); return s ? b3Body_GetAngularVelocity(s->id) : b3Vec3{0, 0, 0}; }
bool Scene::awake(BodyHandle h) const { auto* s = slot(h); return s && b3Body_IsAwake(s->id); }
uint32_t Scene::userData(BodyHandle h) const { auto* s = slot(h); return s ? s->user : 0; }
bool Scene::setUserData(BodyHandle h, uint32_t v) { auto* s = slot(h); if (!s) return false; s->user = v; return true; }

bool Scene::setTransform(BodyHandle h, b3Vec3 p, b3Quat q) {
    auto* s = slot(h);
    if (!s || !finite(p, kMaxCoord) || !normalizeQuat(q)) return false;
    b3Body_SetTransform(s->id, p, q);
    if (s->motion != Motion::Static) b3Body_SetAwake(s->id, true);
    return true;
}
bool Scene::moveKinematic(BodyHandle h, b3Vec3 p, b3Quat q) {
    auto* s = slot(h);
    if (!s || s->motion != Motion::Kinematic || !finite(p, kMaxCoord) || !normalizeQuat(q)) return false;
    b3Body_SetTargetTransform(s->id, b3WorldTransform{p, q}, dt(), true);
    return true;
}
bool Scene::setLinearVelocity(BodyHandle h, b3Vec3 v) {
    auto* s = slot(h);
    if (!s || s->motion == Motion::Static || !finite(v, kMaxSpeed)) return false;
    b3Body_SetLinearVelocity(s->id, v);
    return true;
}
bool Scene::setAngularVelocity(BodyHandle h, b3Vec3 w) {
    auto* s = slot(h);
    if (!s || s->motion == Motion::Static || !finite(w, kMaxSpeed)) return false;
    b3Body_SetAngularVelocity(s->id, w);
    return true;
}
bool Scene::applyImpulse(BodyHandle h, b3Vec3 j) {
    auto* s = slot(h);
    if (!s || s->motion != Motion::Dynamic || !finite(j, 1e7f)) return false;
    b3Body_ApplyLinearImpulseToCenter(s->id, j, true);
    return true;
}
bool Scene::applyImpulseAt(BodyHandle h, b3Vec3 j, b3Vec3 p) {
    auto* s = slot(h);
    if (!s || s->motion != Motion::Dynamic || !finite(j, 1e7f) || !finite(p, kMaxCoord)) return false;
    b3Body_ApplyLinearImpulse(s->id, j, p, true);
    return true;
}
bool Scene::applyForce(BodyHandle h, b3Vec3 f) {
    auto* s = slot(h);
    if (!s || s->motion != Motion::Dynamic || !finite(f, 1e9f)) return false;
    b3Body_ApplyForceToCenter(s->id, f, true);
    return true;
}
bool Scene::applyTorque(BodyHandle h, b3Vec3 t) {
    auto* s = slot(h);
    if (!s || s->motion != Motion::Dynamic || !finite(t, 1e9f)) return false;
    b3Body_ApplyTorque(s->id, t, true);
    return true;
}
bool Scene::setAwake(BodyHandle h, bool a) {
    auto* s = slot(h);
    if (!s || s->motion == Motion::Static) return false;
    b3Body_SetAwake(s->id, a);
    return true;
}

BodyHandle Scene::bodyOfShape(b3ShapeId shape) const {
    if (!b3Shape_IsValid(shape)) return kNoBody;
    BodyHandle h = handleOf(b3Shape_GetBody(shape));
    return valid(h) ? h : kNoBody;
}

void Scene::step() {
    b3World_Step(world_, dt(), config_.substeps);
    ++tick_;
    contactBegins_.clear();
    sensorEvents_.clear();
    sensorEvents_.swap(pendingEnds_);   // overlaps ended by destroy() since the last step
    moved_.clear();
    // Copy the transient Box3D events now: ids may die with the next mutation.
    auto contacts = b3World_GetContactEvents(world_);
    for (int i = 0; i < contacts.beginCount; ++i) {
        const auto& e = contacts.beginEvents[i];
        ContactEvent c{bodyOfShape(e.shapeIdA), bodyOfShape(e.shapeIdB), {0, 0, 0}, {0, 0, 0}};
        if (!c.a || !c.b) continue;
        if (b3Contact_IsValid(e.contactId)) {
            auto data = b3Contact_GetData(e.contactId);
            if (data.manifoldCount > 0 && data.manifolds[0].pointCount > 0) {
                c.normal = data.manifolds[0].normal;
                c.point = add(b3Body_GetWorldCenter(native(c.a)), data.manifolds[0].points[0].anchorA);
            }
        }
        contactBegins_.push_back(c);
    }
    auto sensors = b3World_GetSensorEvents(world_);
    for (int i = 0; i < sensors.beginCount; ++i) {
        SensorEvent e{bodyOfShape(sensors.beginEvents[i].sensorShapeId), bodyOfShape(sensors.beginEvents[i].visitorShapeId), true};
        if (e.sensor && e.visitor) { sensorPairs_.insert({e.sensor, e.visitor}); sensorEvents_.push_back(e); }
    }
    for (int i = 0; i < sensors.endCount; ++i) {
        // Destroyed shapes are not reported (and destroy() already ended their overlaps): only live pairs end here.
        SensorEvent e{bodyOfShape(sensors.endEvents[i].sensorShapeId), bodyOfShape(sensors.endEvents[i].visitorShapeId), false};
        if (e.sensor && e.visitor && sensorPairs_.erase({e.sensor, e.visitor})) sensorEvents_.push_back(e);
    }
    auto bodies = b3World_GetBodyEvents(world_);
    for (int i = 0; i < bodies.moveCount; ++i) {
        BodyHandle h = BodyHandle(uintptr_t(bodies.moveEvents[i].userData) & 0xFFFFFFFFu);
        if (valid(h)) moved_.push_back({h, bodies.moveEvents[i].fellAsleep});
    }
    std::sort(moved_.begin(), moved_.end(), [](const MoveEvent& a, const MoveEvent& b) { return a.body < b.body; });
}

bool Scene::acceptShape(b3ShapeId shape, const QueryFilter& filter) const {
    if (!filter.sensors && b3Shape_IsSensor(shape)) return false;
    BodyHandle h = bodyOfShape(shape);
    return h != kNoBody && h != filter.ignore;
}

RayHit Scene::raycast(b3Vec3 origin, b3Vec3 translation, const QueryFilter& filter) const {
    RayHit best;
    if (!finite(origin, kMaxCoord) || !finite(translation, 2 * kMaxCoord) || dot(translation, translation) <= 0) return best;
    struct Ctx { const Scene* scene; const QueryFilter* filter; RayHit* best; } ctx{this, &filter, &best};
    auto fcn = [](b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction, uint64_t, int, int, void* c) -> float {
        auto* x = static_cast<Ctx*>(c);
        if (!x->scene->acceptShape(shape, *x->filter)) return -1; // ignore, keep going
        BodyHandle h = x->scene->bodyOfShape(shape);
        if (fraction < x->best->fraction || (fraction == x->best->fraction && h < x->best->body) || x->best->body == kNoBody)
            *x->best = {h, point, normal, fraction};
        return fraction; // clip the ray
    };
    b3World_CastRay(world_, origin, translation, queryFilter(filter.mask), fcn, &ctx);
    return best;
}

RayHit Scene::sphereCast(b3Vec3 origin,b3Vec3 translation,float radius,const QueryFilter& filter) const {
    if(radius==0.f)return raycast(origin,translation,filter);
    RayHit best;
    if(!finite(origin,kMaxCoord)||!finite(translation,2*kMaxCoord)||
       !inRange(radius,0,10000)||dot(translation,translation)<=0)return best;
    const b3Vec3 center{0,0,0};
    const b3ShapeProxy proxy{&center,1,radius};
    struct Ctx {const Scene* scene;const QueryFilter* filter;RayHit* best;} ctx{this,&filter,&best};
    auto fcn=[](b3ShapeId shape,b3Pos point,b3Vec3 normal,float fraction,uint64_t,int,int,void* data)->float {
        auto* x=static_cast<Ctx*>(data);
        if(!x->scene->acceptShape(shape,*x->filter)||
           !inRange(fraction,0,1)||!finite(normal,2)||dot(normal,normal)<1e-10f)return -1;
        const auto h=x->scene->bodyOfShape(shape);
        if(x->best->body==kNoBody||fraction<x->best->fraction||
           (fraction==x->best->fraction&&h<x->best->body))
            *x->best={h,point,normal,fraction};
        return x->best->fraction;
    };
    b3World_CastShape(world_,origin,&proxy,translation,queryFilter(filter.mask),fcn,&ctx);
    return best;
}

std::vector<RayHit> Scene::raycastAll(b3Vec3 origin, b3Vec3 translation, const QueryFilter& filter) const {
    std::vector<RayHit> hits;
    if (!finite(origin, kMaxCoord) || !finite(translation, 2 * kMaxCoord) || dot(translation, translation) <= 0) return hits;
    struct Ctx { const Scene* scene; const QueryFilter* filter; std::vector<RayHit>* hits; } ctx{this, &filter, &hits};
    auto fcn = [](b3ShapeId shape, b3Pos point, b3Vec3 normal, float fraction, uint64_t, int, int, void* c) -> float {
        auto* x = static_cast<Ctx*>(c);
        if (x->scene->acceptShape(shape, *x->filter)) x->hits->push_back({x->scene->bodyOfShape(shape), point, normal, fraction});
        return 1; // all hits
    };
    b3World_CastRay(world_, origin, translation, queryFilter(filter.mask), fcn, &ctx);
    std::sort(hits.begin(), hits.end(), [](const RayHit& a, const RayHit& b) { return a.fraction != b.fraction ? a.fraction < b.fraction : a.body < b.body; });
    // One hit per body (compound / mesh shapes can report several)
    std::vector<RayHit> unique;
    for (auto& h : hits)
        if (std::none_of(unique.begin(), unique.end(), [&](const RayHit& u) { return u.body == h.body; })) unique.push_back(h);
    return unique;
}

std::vector<BodyHandle> Scene::overlap(b3Vec3 origin, const b3Vec3* points, int count, float radius, const QueryFilter& filter) const {
    std::vector<BodyHandle> out;
    b3ShapeProxy proxy{points, count, radius};
    struct Ctx { const Scene* scene; const QueryFilter* filter; std::vector<BodyHandle>* out; } ctx{this, &filter, &out};
    auto fcn = [](b3ShapeId shape, void* c) -> bool {
        auto* x = static_cast<Ctx*>(c);
        if (x->scene->acceptShape(shape, *x->filter)) x->out->push_back(x->scene->bodyOfShape(shape));
        return true;
    };
    b3World_OverlapShape(world_, origin, &proxy, queryFilter(filter.mask), fcn, &ctx);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<BodyHandle> Scene::overlapSphere(b3Vec3 c, float r, const QueryFilter& filter) const {
    if (!finite(c, kMaxCoord) || !inRange(r, 0, 10000)) return {};
    b3Vec3 p{0, 0, 0};
    return overlap(c, &p, 1, r, filter);
}

std::vector<BodyHandle> Scene::overlapBox(b3Vec3 c, b3Vec3 half, b3Quat q, const QueryFilter& filter) const {
    if (!finite(c, kMaxCoord) || !inRange(half.x, 0, 10000) || !inRange(half.y, 0, 10000) || !inRange(half.z, 0, 10000) || !normalizeQuat(q)) return {};
    b3Vec3 pts[8];
    for (int i = 0; i < 8; ++i)
        pts[i] = b3RotateVector(q, {i & 1 ? half.x : -half.x, i & 2 ? half.y : -half.y, i & 4 ? half.z : -half.z});
    return overlap(c, pts, 8, 0, filter);
}

std::vector<BodyHandle> Scene::overlapCapsule(b3Vec3 a, b3Vec3 b, float r, const QueryFilter& filter) const {
    if (!finite(a, kMaxCoord) || !finite(b, kMaxCoord) || !inRange(r, 0, 10000)) return {};
    b3Vec3 pts[2] = {{0, 0, 0}, sub(b, a)};
    return overlap(a, pts, 2, r, filter);
}

BodyHandle Scene::createCharacter(const CharacterParams& p, b3Vec3 feet, uint32_t userData) {
    if (!validParams(p) || !finite(feet, kMaxCoord) || live_ >= config_.maxBodies) return kNoBody;
    BodyDesc d;
    d.motion = Motion::Kinematic;
    d.shape = ShapeKind::Capsule;
    d.radius = p.radius;
    d.halfHeight = std::max(0.005f, p.height / 2 - p.radius);
    d.position = feet;
    d.category = kCategoryCharacter;
    d.mask = kCategoryQuery | kCategorySensor;
    d.userData = userData;
    BodyHandle h = create(d);
    if (!h) return kNoBody;
    // Body origin at the feet: move the capsule up so the proxy matches the controller capsule.
    b3ShapeId shape;
    b3Body_GetShapes(native(h), &shape, 1);
    b3Capsule c = characterCapsule(p);
    b3Shape_SetCapsule(shape, &c);
    b3Shape_EnableContactEvents(shape, false);
    return h;
}

bool Scene::moveCharacter(BodyHandle h, const CharacterState& st, bool teleport) {
    auto* s = slot(h);
    if (!s || s->motion != Motion::Kinematic || !finite(st.position, kMaxCoord)) return false;
    if (teleport) { b3Body_SetTransform(s->id, st.position, b3Quat_identity); b3Body_SetLinearVelocity(s->id, {0, 0, 0}); }
    else b3Body_SetTargetTransform(s->id, b3WorldTransform{st.position, b3Quat_identity}, dt(), true);
    return true;
}

bool Scene::moveCharacterOriented(BodyHandle h,const CharacterState& st,b3Quat orientation,bool teleport){
    auto* s=slot(h);
    if(!s||s->motion!=Motion::Kinematic||!finite(st.position,kMaxCoord)||!validCharacterOrientation(orientation))return false;
    if(teleport){b3Body_SetTransform(s->id,st.position,orientation);b3Body_SetLinearVelocity(s->id,{0,0,0});}
    else b3Body_SetTargetTransform(s->id,b3WorldTransform{st.position,orientation},dt(),true);
    return true;
}

void Scene::applyPushes(const std::vector<Push>& pushes) {
    for (const auto& p : pushes) {
        auto* s = slot(p.body);
        if (!s || s->motion != Motion::Dynamic || !finite(p.point, kMaxCoord) || !finite(p.direction, 2) || !inRange(p.speed, 0, kMaxSpeed) ||
            !inRange(p.maxImpulse, 0, 1e7f)) continue;
        float len = std::sqrt(dot(p.direction, p.direction));
        if (len < 1e-3f) continue;
        b3Vec3 dir = scale(p.direction, 1 / len);
        float rel = p.speed - dot(b3Body_GetWorldPointVelocity(s->id, p.point), dir);
        if (rel <= 0) continue;
        float impulse = std::min(rel * b3Body_GetMass(s->id), p.maxImpulse);
        b3Body_ApplyLinearImpulse(s->id, scale(dir, impulse), p.point, true);
    }
}
}
