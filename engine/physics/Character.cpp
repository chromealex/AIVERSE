#include "Character.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace aiverse::physics {
namespace {
constexpr float kSkin = 0.04f;        // ground probe reach below the capsule
constexpr float kMaxCoord = 100000;
constexpr int kMaxPlanes = 64;
constexpr int kIterations = 5;
constexpr float kTolerance = 0.0005f; // stop iterating below this move
constexpr float kTwoPi = 6.28318530718f;

b3Vec3 add(b3Vec3 a, b3Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
b3Vec3 sub(b3Vec3 a, b3Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
b3Vec3 scale(b3Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(b3Vec3 a, b3Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float lenXZ(b3Vec3 a) { return std::sqrt(a.x * a.x + a.z * a.z); }
bool finite(float x, float limit) { return std::isfinite(x) && std::abs(x) <= limit; }
bool finite(b3Vec3 v, float limit) { return finite(v.x, limit) && finite(v.y, limit) && finite(v.z, limit); }
bool inRange(float x, float lo, float hi) { return std::isfinite(x) && x >= lo && x <= hi; }

// `dynamic`: by the shape's filter category (kCategoryDynamic), not the body type, so a client's kinematic
// stand-ins of server-driven bodies (prediction proxies) behave like the dynamic bodies they stand for.
struct PlaneHit { b3Plane plane; b3Vec3 point; b3BodyId body; bool dynamic; };
bool dynamicShape(b3ShapeId shape) { return (b3Shape_GetFilter(shape).categoryBits & kCategoryDynamic) != 0; }
struct Planes { PlaneHit hit[kMaxPlanes]; int count = 0; };
struct Solved { b3CollisionPlane plane[kMaxPlanes]; int count = 0; };

struct CharacterFrame {
    b3Quat rotation=b3Quat_identity;
    bool identity=true;
    b3Vec3 world(b3Vec3 v)const{return identity?v:b3RotateVector(rotation,v);}
    b3Vec3 local(b3Vec3 v)const{
        return identity?v:b3RotateVector({-rotation.x,-rotation.y,-rotation.z,rotation.w},v);
    }
    b3Capsule worldCapsule(b3Capsule v)const{
        v.center1=world(v.center1);v.center2=world(v.center2);return v;
    }
};
struct Query {
    b3WorldId world;
    b3QueryFilter filter;
    BodyHandle self;
    b3Capsule capsule;
    float stepTop; // frame-local y: feet at step start + stepHeight
    CharacterFrame frame;
    bool allowGround=true;
};

bool accept(b3ShapeId shape, BodyHandle self) {
    if (b3Shape_IsSensor(shape)) return false;
    return self == kNoBody || Scene::handleOf(b3Shape_GetBody(shape)) != self;
}

constexpr float kDepenetration = 0.01f; // per plane solve (a step runs up to ~6): ~3 m/s apart, no launch

struct CollectCtx { Planes* planes; BodyHandle self; b3Vec3 pos; CharacterFrame frame; };
bool collectPlanes(b3ShapeId shape, const b3PlaneResult* r, int n, void* c) {
    auto* x = static_cast<CollectCtx*>(c);
    if (!accept(shape, x->self)) return true;
    b3BodyId body = b3Shape_GetBody(shape);
    const bool dynamic = dynamicShape(shape);
    const bool character = (b3Shape_GetFilter(shape).categoryBits & kCategoryCharacter) != 0;
    b3Vec3 axis = sub(x->frame.local(b3Body_GetPosition(body)), x->pos);   // a character proxy's origin is its feet
    for (int i = 0; i < n && x->planes->count < kMaxPlanes; ++i) {
        b3Plane plane = r[i].plane;
        plane.normal=x->frame.local(plane.normal);
        if (character) {
            // Characters spawned or pushed inside each other separate gradually: the penetration resolved per
            // solve is capped. On a shared axis the capsules give no separating direction; use one fixed by
            // the own handle (golden angle: two characters never share it).
            if (lenXZ(axis) < 0.02f && std::abs(axis.y) < 0.5f) {
                float a = float(x->self & 0xFFFFu) * 2.3999632f + float(x->self >> 16) * 0.7f;
                plane = {{std::cos(a), 0, std::sin(a)}, kDepenetration};
            }
            plane.offset = std::min(plane.offset, kDepenetration);
        }
        // Dynamic bodies have no contacts with the character proxy, so one can come to rest overlapping the
        // capsule from above (a box dropped on a head). Its downward planes would press the character into
        // the ground every step until it falls through: a character is never crushed by dynamic bodies.
        if (dynamic && plane.normal.y < -0.3f) continue;
        x->planes->hit[x->planes->count++] = {plane, x->frame.local(r[i].point), body, dynamic};
    }
    return x->planes->count < kMaxPlanes;
}
bool castFilter(b3ShapeId shape, void* c) { return accept(shape, *static_cast<BodyHandle*>(c)); }

void collide(const Query& q, b3Vec3 pos, const b3Capsule& capsule, Planes& out) {
    out.count = 0;
    CollectCtx ctx{&out, q.self, pos,q.frame};
    const auto oriented=q.frame.worldCapsule(capsule);
    b3World_CollideMover(q.world, q.frame.world(pos), &oriented, q.filter, collectPlanes, &ctx);
}

float cast(const Query& q, b3Vec3 pos, b3Vec3 delta) {
    if (dot(delta, delta) < 1e-12f) return 1;
    BodyHandle self = q.self;
    const auto oriented=q.frame.worldCapsule(q.capsule);
    return b3World_CastMover(q.world, q.frame.world(pos), &oriented, q.frame.world(delta), q.filter, castFilter, &self);
}

struct RayCtx { BodyHandle self; bool hit; b3Vec3 normal; float fraction; };
float rayFcn(b3ShapeId shape, b3Pos, b3Vec3 normal, float fraction, uint64_t, int, int, void* c) {
    auto* x = static_cast<RayCtx*>(c);
    if (!accept(shape, x->self)) return -1;
    if (fraction < x->fraction) *x = {x->self, true, normal, fraction};
    return fraction;
}

// A contact under the lower cap is ground when its plane is walkable, or when it is the edge of a
// walkable top (a step or a ledge corner): a short ray just past the edge finds that top.
bool walkable(const Query& q, b3Vec3 pos, const PlaneHit& h, float cosMax, b3Vec3* surface) {
    if (!q.allowGround || h.point.y > q.capsule.center1.y + 1e-4f || h.plane.normal.y <= 0) return false;
    if (h.plane.normal.y >= cosMax) { if (surface) *surface = h.plane.normal; return true; }
    if (pos.y + h.point.y > q.stepTop) return false;
    float l = lenXZ(h.point);
    b3Vec3 out = l > 1e-4f ? b3Vec3{h.point.x / l * 0.02f, 0, h.point.z / l * 0.02f} : b3Vec3{0, 0, 0};
    b3Vec3 origin = add(add(pos, h.point), add(out, b3Vec3{0, 0.05f, 0}));
    RayCtx ctx{q.self, false, {0, 0, 0}, 2};
    b3World_CastRay(q.world, q.frame.world(origin), q.frame.world({0, -0.1f, 0}), q.filter, rayFcn, &ctx);
    ctx.normal=q.frame.local(ctx.normal);
    if (!ctx.hit || ctx.normal.y < cosMax) return false;
    if (surface) *surface = ctx.normal;
    return true;
}

struct Ground { bool found = false; b3BodyId body = b3_nullBodyId; b3Vec3 point{0, 0, 0}, normal{0, 1, 0}; };

// Walkable surface within kSkin under the lower cap.
Ground probe(const Query& q, b3Vec3 pos, float cosMax) {
    if(!q.allowGround)return {};
    Query wide = q;
    wide.capsule.radius += kSkin;
    Planes planes;
    collide(wide, pos, wide.capsule, planes);
    Ground g;
    float best = -1;
    for (int i = 0; i < planes.count; ++i) {
        const auto& h = planes.hit[i];
        b3Vec3 n;
        if (h.plane.normal.y <= best || !walkable(wide, pos, h, cosMax, &n)) continue;
        best = h.plane.normal.y;
        g = {true, h.body, add(pos, h.point), n};
    }
    return g;
}

enum class Mode { Walk, Fall };

// Collision plane for the solver. Walk: ground pushes straight up (no creep down slopes) and does not
// clip the walk, steep upward planes become vertical walls so walking never climbs them. Fall: true
// planes, so gravity slides down steep slopes and ceilings stop jumps.
b3CollisionPlane solverPlane(const Query& q, b3Vec3 pos, const PlaneHit& h, Mode mode, float cosMax) {
    b3CollisionPlane cp{h.plane, FLT_MAX, 0, true};
    if (mode == Mode::Fall || !q.allowGround) return cp;
    float ny = h.plane.normal.y;
    if (ny > 0 && walkable(q, pos, h, cosMax, nullptr)) {
        cp.plane = {{0, 1, 0}, ny >= cosMax ? h.plane.offset / ny : 0.0f};
        cp.clipVelocity = false;
    } else if (ny > 0 && ny < cosMax) {
        float l = lenXZ(h.plane.normal); // > sin(maxSlope) > 0
        cp.plane = {{h.plane.normal.x / l, 0, h.plane.normal.z / l}, std::min(h.plane.offset / l, h.plane.offset * 10)};
    }
    return cp;
}

// Moves by up to delta, sliding along the collision planes (Box2D/3D mover loop).
// `solved` keeps the last solve for velocity clipping.
template <class OnPlane>
b3Vec3 slide(const Query& q, b3Vec3 pos, b3Vec3 delta, Mode mode, float cosMax, Solved& solved, OnPlane&& onPlane) {
    b3Vec3 target = add(pos, delta);
    Planes planes;
    solved.count = 0;
    for (int it = 0; it < kIterations; ++it) {
        collide(q, pos, q.capsule, planes);
        solved.count = planes.count;
        for (int i = 0; i < planes.count; ++i) {
            solved.plane[i] = solverPlane(q, pos, planes.hit[i], mode, cosMax);
            onPlane(planes.hit[i], solved.plane[i].plane.normal);
        }
        b3PlaneSolverResult r = b3SolvePlanes(sub(target, pos), solved.plane, solved.count);
        float f = cast(q, pos, r.delta);
        b3Vec3 moved = scale(r.delta, f);
        pos = add(pos, moved);
        if (dot(moved, moved) < kTolerance * kTolerance) break;
    }
    return pos;
}
}

bool validParams(const CharacterParams& p) {
    return inRange(p.radius, 0.05f, 10) && inRange(p.height, 2 * p.radius + 0.01f, 50) && inRange(p.walkSpeed, 0, 100) &&
           inRange(p.runSpeed, 0, 100) && inRange(p.acceleration, 0, 1e6f) && inRange(p.airControl, 0, 1) && inRange(p.jumpSpeed, 0, 100) &&
           inRange(p.gravity, 0, 1000) && inRange(p.maxSlope, 0, 1.5607963f) && inRange(p.stepHeight, 0, p.height) &&
           inRange(p.pushImpulseLimit, 0, 1e7f) && inRange(p.maxSpeed, 0.1f, 1000);
}

b3Capsule characterCapsule(const CharacterParams& p) {
    return {{0, p.radius, 0}, {0, std::max(p.radius + 0.005f, p.height - p.radius), 0}, p.radius};
}

std::vector<uint8_t> CharacterInput::pack() const {
    auto q8 = [](float v) { return uint8_t(int8_t(std::lround(std::clamp(std::isfinite(v) ? v : 0.0f, -1.0f, 1.0f) * 127))); };
    float y = std::isfinite(yaw) ? std::fmod(yaw, kTwoPi) : 0.0f;
    if (y < 0) y += kTwoPi;
    uint32_t qy = uint32_t(std::lround(y / kTwoPi * 65536.0f)) & 0xFFFFu;
    return {q8(moveX), q8(moveZ), uint8_t(qy & 0xFF), uint8_t(qy >> 8), uint8_t((jump ? 1 : 0) | (run ? 2 : 0))};
}

CharacterInput CharacterInput::unpack(const uint8_t* data, size_t size) {
    CharacterInput in;
    if (!data || size < kPackedSize) return in;
    auto d8 = [](uint8_t b) { return float(std::max<int>(int8_t(b), -127)) / 127.0f; };
    in.moveX = d8(data[0]);
    in.moveZ = d8(data[1]);
    in.yaw = float(uint32_t(data[2]) | (uint32_t(data[3]) << 8)) * (kTwoPi / 65536.0f);
    in.jump = data[4] & 1;
    in.run = data[4] & 2;
    return in;
}

static void stepCharacterFrame(b3WorldId world, const CharacterParams& p, CharacterState& st, const CharacterInput& in, float dt, Pushes* out,
                   BodyHandle self,const CharacterFrame& frame,bool allowGround) {
    if (out) out->clear();
    st.groundTurn = 0;
    if (!b3World_IsValid(world) || !validParams(p) || !finite(st.position, kMaxCoord) || !inRange(dt, 1e-5f, 0.1f)) {
        st.velocity = {0, 0, 0};
        return;
    }
    if (!finite(st.velocity, p.maxSpeed * 2)) st.velocity = {0, 0, 0};
    const float cosMax = std::cos(p.maxSlope);
    const b3Vec3 start = st.position;
    const Query q{world, {kCategoryQuery, p.mask, 0, nullptr}, self, characterCapsule(p), start.y + std::max(0.0f, p.stepHeight - 0.01f),frame,allowGround};

    // Ground at the start: walkable surface and its velocity (moving platforms carry the character).
    Ground g0 = probe(q, start, cosMax);
    bool grounded = g0.found;
    b3Vec3 platform{0, 0, 0}, spinCarry{0, 0, 0};
    float turn = 0;
    if (grounded && b3Body_GetType(g0.body) != b3_staticBody) {
        platform = frame.local(b3Body_GetWorldPointVelocity(g0.body, frame.world(g0.point)));
        b3Vec3 w = frame.local(b3Body_GetAngularVelocity(g0.body));
        float wl = std::sqrt(dot(w, w));
        if (finite(w, 100) && wl * dt > 1e-6f) {
            // Rotating ground: carry the feet along the arc (the point velocity alone drifts outwards) and turn
            // the yaw with the ground's spin about +Y.
            b3Vec3 c = frame.local(b3Body_GetWorldCenter(g0.body)), v = frame.local(b3Body_GetLinearVelocity(g0.body));
            b3Vec3 r = b3RotateVector(b3MakeQuatFromAxisAngle(scale(w, 1 / wl), wl * dt), sub(start, c));
            spinCarry = sub(add(add(c, scale(v, dt)), r), start);
            turn = w.y * dt;
        }
    }
    if (!finite(platform, 1000) || !finite(spinCarry, 100)) { platform = {0, 0, 0}; spinCarry = {0, 0, 0}; turn = 0; }

    // Wanted horizontal velocity from the input, relative to yaw.
    float yaw = std::isfinite(in.yaw) ? in.yaw : 0.0f;
    st.yaw = yaw;
    float mx = std::isfinite(in.moveX) ? std::clamp(in.moveX, -1.0f, 1.0f) : 0.0f;
    float mz = std::isfinite(in.moveZ) ? std::clamp(in.moveZ, -1.0f, 1.0f) : 0.0f;
    float s = std::sin(yaw), c = std::cos(yaw);
    b3Vec3 wish{mx * c - mz * s, 0, -mx * s - mz * c}; // right = (cos, 0, -sin), forward = (-sin, 0, -cos)
    float wl = lenXZ(wish);
    if (wl > 1) wish = scale(wish, 1 / wl);
    b3Vec3 desired = scale(wish, in.run ? p.runSpeed : p.walkSpeed);

    b3Vec3 vh{st.velocity.x, 0, st.velocity.z};
    float vy = st.velocity.y;
    b3Vec3 dv = sub(desired, vh);
    float dvl = lenXZ(dv), maxDv = p.acceleration * (grounded ? 1.0f : p.airControl) * dt;
    if (dvl > maxDv) dv = scale(dv, maxDv / dvl);
    vh = add(vh, dv);

    bool jumped = false;
    if (grounded && in.jump && p.jumpSpeed > 0) {
        jumped = true;
        vy = p.jumpSpeed + std::max(0.0f, platform.y);
        vh = add(vh, b3Vec3{platform.x, 0, platform.z}); // keep the platform's motion in the air
        grounded = false;
    }
    // Airborne: exact parabola (average of the start and end velocities over the step).
    float fall = 0;
    if (!grounded) { fall = (vy - 0.5f * p.gravity * dt) * dt; vy -= p.gravity * dt; }
    else vy = 0;
    const b3Vec3 carry = !grounded ? b3Vec3{0, 0, 0} : turn != 0 ? spinCarry : scale(platform, dt);

    // Pass 1: walk (horizontal velocity + platform carry); steep surfaces act as walls.
    std::vector<Push> pushes;
    auto onPlane = [&](const PlaneHit& h, b3Vec3 n) {
        if (!out || !h.dynamic) return;
        float nl = lenXZ(n);
        if (nl < 0.3f) return;
        b3Vec3 dir{-n.x / nl, 0, -n.z / nl};
        float speed = std::max(dot(desired, dir), dot(vh, dir));
        if (speed < 0.05f) return;
        BodyHandle hb = Scene::handleOf(h.body);
        if (hb == kNoBody) return;
        for (auto& e : pushes)
            if (e.body == hb) { if (speed > e.speed) e = {hb, add(st.position, h.point), dir, speed, p.pushImpulseLimit}; return; }
        pushes.push_back({hb, add(st.position, h.point), dir, speed, p.pushImpulseLimit});
    };
    b3Vec3 walk = scale(vh, dt);
    if (grounded) walk.y = -(g0.normal.x * walk.x + g0.normal.z * walk.z) / g0.normal.y; // follow the ground slope
    walk = add(walk, carry);
    Solved solved;
    b3Vec3 pos = slide(q, start, walk, Mode::Walk, cosMax, solved, onPlane);
    b3Vec3 clipped = b3ClipVector(vh, solved.plane, solved.count);
    vh = {clipped.x, 0, clipped.z};

    // Step up: blocked while walking on the ground -> lift by stepHeight, walk, drop back down.
    float wantXZ = lenXZ(walk), gotXZ = lenXZ(sub(pos, start));
    if (g0.found && !jumped && p.stepHeight > 0 && wantXZ > 1e-4f && gotXZ < 0.9f * wantXZ) {
        b3Vec3 up{0, p.stepHeight, 0};
        b3Vec3 p1 = add(start, scale(up, cast(q, start, up)));
        Solved s2;
        b3Vec3 p2 = slide(q, p1, walk, Mode::Walk, cosMax, s2, [](const PlaneHit&, b3Vec3) {});
        b3Vec3 down{0, -(p1.y - start.y) - kSkin, 0};
        b3Vec3 p3 = add(p2, scale(down, cast(q, p2, down)));
        // Only a real climb counts: ending higher and further along the walk. A lifted pass that merely slides
        // further along an obstacle's face (a box being pushed) must not win, or the choice flips on millimetres
        // and keeps the full walking speed against the obstacle.
        b3Vec3 dir = scale(walk, 1 / wantXZ);
        float gotAlong = dot(sub(pos, start), {dir.x, 0, dir.z}), stepAlong = dot(sub(p3, start), {dir.x, 0, dir.z});
        if (p3.y > pos.y + 0.01f && stepAlong > gotAlong + 1e-4f && probe(q, p3, cosMax).found) {
            pos = p3;
            vh = {st.velocity.x + dv.x, 0, st.velocity.z + dv.z}; // the step does not stop the walk
        }
    }

    // Pass 2: vertical (gravity / jump) against the true planes: ceilings stop it, steep slopes slide.
    if (fall != 0) {
        Solved sv;
        pos = slide(q, pos, b3Vec3{0, fall, 0}, Mode::Fall, cosMax, sv, [](const PlaneHit&, b3Vec3) {});
        b3Vec3 v = b3ClipVector(b3Vec3{0, vy, 0}, sv.plane, sv.count);
        vy = v.y;
        if (!probe(q, pos, cosMax).found) vh = add(vh, b3Vec3{v.x, 0, v.z}); // sliding down a steep slope
    }

    // Stick to the ground walking down slopes and steps (not after a jump).
    if (g0.found && !jumped) {
        b3Vec3 down{0, -(p.stepHeight + kSkin), 0};
        float f = cast(q, pos, down);
        b3Vec3 cand = add(pos, scale(down, f));
        if (f < 1 && probe(q, cand, cosMax).found) pos = cand;
    }
    // The cast may stop slightly inside the surface: settle out of it (straight up on ground).
    {
        Planes planes;
        Solved settle;
        collide(q, pos, q.capsule, planes);
        settle.count = planes.count;
        for (int i = 0; i < planes.count; ++i) settle.plane[i] = solverPlane(q, pos, planes.hit[i], Mode::Walk, cosMax);
        b3Vec3 d = b3SolvePlanes({0, 0, 0}, settle.plane, settle.count).delta;
        pos = add(pos, scale(d, cast(q, pos, d)));
    }

    Ground g1 = probe(q, pos, cosMax);
    if (g1.found && vy < 0) vy = 0;
    if (g0.found && !g1.found && !jumped) { // walked off a moving platform: keep its motion
        vh = add(vh, b3Vec3{platform.x, 0, platform.z});
        vy += platform.y;
    }
    b3Vec3 v{vh.x, vy, vh.z};
    float vl = std::sqrt(dot(v, v));
    if (vl > p.maxSpeed) v = scale(v, p.maxSpeed / vl);

    st.position = finite(pos, kMaxCoord) ? pos : start;
    st.velocity = finite(v, 1000) ? v : b3Vec3{0, 0, 0};
    st.grounded = g1.found;
    st.ground = g1.found ? Scene::handleOf(g1.body) : kNoBody;
    st.groundTurn = grounded ? turn : 0;
    st.yaw = yaw + st.groundTurn;
    if (out) {
        std::sort(pushes.begin(), pushes.end(), [](const Push& a, const Push& b) { return a.body < b.body; });
        *out = std::move(pushes);
    }
}

bool validCharacterOrientation(b3Quat q){
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::isfinite(n)&&std::abs(n-1.f)<.001f;
}
void stepCharacter(b3WorldId world,const CharacterParams& p,CharacterState& st,
                   const CharacterInput& in,float dt,Pushes* out,BodyHandle self){
    stepCharacterFrame(world,p,st,in,dt,out,self,CharacterFrame{},true);
}
bool stepCharacterOriented(b3WorldId world,const CharacterParams& p,CharacterState& st,
        const CharacterInput& in,float dt,b3Quat orientation,bool allowGround,Pushes* out,BodyHandle self){
    if(out)out->clear();
    if(!validCharacterOrientation(orientation)||!b3World_IsValid(world)||!validParams(p)||
       !finite(st.position,kMaxCoord)||!inRange(dt,1e-5f,.1f))return false;
    const float inv=1.f/std::sqrt(orientation.x*orientation.x+orientation.y*orientation.y+orientation.z*orientation.z+orientation.w*orientation.w);
    orientation={orientation.x*inv,orientation.y*inv,orientation.z*inv,orientation.w*inv};
    const CharacterFrame frame{orientation,orientation.x==0&&orientation.y==0&&orientation.z==0&&orientation.w==1};
    auto local=st;
    local.position=frame.local(st.position);local.velocity=frame.local(st.velocity);
    if(!finite(local.position,kMaxCoord))return false;
    Pushes pushes;
    stepCharacterFrame(world,p,local,in,dt,out?&pushes:nullptr,self,frame,allowGround);
    local.position=frame.world(local.position);local.velocity=frame.world(local.velocity);
    if(!finite(local.position,kMaxCoord)||!finite(local.velocity,1000))return false;
    for(auto& push:pushes){push.point=frame.world(push.point);push.direction=frame.world(push.direction);}
    st=local;
    if(out)*out=std::move(pushes);
    return true;
}
}
