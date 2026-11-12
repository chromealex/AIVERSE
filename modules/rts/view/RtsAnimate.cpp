// Per-object animation state: smoothed facing and speed, unit state -> clips on the Animator
// layers (blend spaces, additive clips), sim events (attack, hit, spawn, death) -> one-shot clips
// and procedural envelopes, clip events, foot IK, animation LOD, corpses that outlive their
// entity. Presentation only; reads the scene, never the simulation.
#include "RtsView.h"
#include "engine/render/AnimationLod.h"
#include "../RtsModule.h"

#include <algorithm>
#include <cmath>

namespace aiverse::game {

using namespace render;

std::shared_ptr<const AnimationEnvironment> RtsView::animationEnvironment(const SceneObject& o,const EntityAnim& st,const ModelChoice& ch,const MapGrid& grid){
    auto environment=std::make_shared<AnimationEnvironment>();const auto& look=looks_[o.type];
    EntityFxValues fx;bool hasFx=fxFor(o,fx);float lift=hasFx?fx.v[EntityFxValues::kLift]:0;
    auto pos=float3{o.pos.x,heightAt(grid,o.pos.x,o.pos.z)+(look.air?look.flyHeight:0)+lift,o.pos.z};
    auto tilt=hasFx?mat4::rotateZ(-fx.v[EntityFxValues::kPitch]*.017453292f)*mat4::rotateX(-fx.v[EntityFxValues::kRoll]*.017453292f):mat4{};
    float grow=look.structure&&look.construction==Construction::Grow?std::clamp(o.build,.15f,1.f):1;
    environment->modelToWorld=mat4::translate(pos)*mat4::rotateY(st.yaw)*tilt*mat4::scale({look.scale,look.scale*grow,look.scale});
    if(hasFx)environment->modelToWorld=environment->modelToWorld*mat4::scale({fx.scale(0),fx.scale(1),fx.scale(2)});
    environment->modelToWorld=environment->modelToWorld*ch.fix;
    auto locomotion=st.graph.locomotionReport();auto queryCenter=locomotion.active?locomotion.position:pos;
    std::vector<AnimationObstacle> boxes;float reach=std::max(2.f,length(ch.model->boundsMax-ch.model->boundsMin)*look.scale*2);
    for(const auto& b:animationObstacles_){if(b.object==&o||(o.entity.valid()&&b.entity==o.entity))continue;auto nearest=float3{std::clamp(queryCenter.x,b.lo.x,b.hi.x),std::clamp(queryCenter.y,b.lo.y,b.hi.y),std::clamp(queryCenter.z,b.lo.z,b.hi.z)};
        if(length(nearest-queryCenter)>reach)continue;boxes.push_back(b);}
    std::stable_sort(boxes.begin(),boxes.end(),[&](const auto& a,const auto& b){return length((a.lo+a.hi)*.5f-queryCenter)<length((b.lo+b.hi)*.5f-queryCenter);});if(boxes.size()>64)boxes.resize(64);
    auto ground=[this,&grid](float3 p){return heightAt(grid,p.x,p.z);};
    auto normal=[ground](float3 p){constexpr float e=.03f;return normalize(float3{ground(p+float3{-e,0,0})-ground(p+float3{e,0,0}),2*e,ground(p+float3{0,0,-e})-ground(p+float3{0,0,e})});};
    environment->sphereContact=[boxes,ground,normal](float3 center,float radius,IKHit& hit){
        bool found=ikSpherePlane(center,radius,{center.x,ground(center),center.z},normal(center),hit);if(found)hit.surface=1;
        for(const auto& b:boxes){IKHit h;if(ikSphereOBB(center,radius,b.localMin,b.localMax,b.transform,h)&&(!found||h.depth>hit.depth)){hit=h;hit.surface=b.surface;found=true;}}return found;};
    environment->raycast=[boxes,ground,normal](float3 start,float3 end,IKHit& hit){
        bool found=false;float best=2;auto delta=end-start;float previous=start.y-ground(start);
        for(int i=1;i<=32;++i){float t=float(i)/32;auto p=start+delta*t;float d=p.y-ground(p);if(previous>=0&&d<=0){float lo=float(i-1)/32,hi=t;for(int j=0;j<12;++j){float mid=(lo+hi)*.5f;auto q=start+delta*mid;if(q.y>=ground(q))lo=mid;else hi=mid;}best=(lo+hi)*.5f;auto q=start+delta*best;hit={q,normal(q),0,1};found=true;break;}previous=d;}
        for(const auto& box:boxes){IKHit h;if(ikRayBox(start,end,box.localMin,box.localMax,box.transform,h)){float t=dot(h.point-start,delta)/std::max(dot(delta,delta),1e-9f);if(t<best){best=t;hit=h;hit.surface=box.surface;found=true;}}}
        return found;};
    auto frames=animationSurfaceFrames_;environment->surfaceTransform=[frames](uint64_t id,mat4& frame){if(id==1){frame={};return true;}if(!frames)return false;auto it=frames->find(id);if(it==frames->end())return false;frame=it->second;return true;};
    if(animationEnvironmentQuery_)animationEnvironmentQuery_(o.entity,*environment);return environment;
}

namespace {
constexpr float kPi = 3.14159265358979f;
float wrap(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}
float yawOf(float3 f) { return std::atan2(-f.z, f.x); }
uint64_t mix64(uint64_t x) { // splitmix: stable per-object variety (variant, phase, clip pick)
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
bool fresh(float age, float dt) { return age <= dt + 1e-4f; }
} // namespace

void RtsView::handleEvents(const RtsScene& s) {
    if (s.tick == lastTick_) return;
    lastTick_ = s.tick;
    std::map<uint64_t, const SceneObject*> objects;
    for (const auto& o : s.objects) if (!o.isProjectile) objects.emplace(o.entity.packed(), &o);
    for (const auto& ev : s.events) {
        if (!ev.a.valid()) continue;
        const uint64_t key = ev.a.packed();
        auto it = anims_.find(key);
        switch (ev.type) {
            case kEvAttack: {
                auto source = objects.find(key), target = objects.find(ev.b.packed());
                if (source == objects.end() || target == objects.end()) break;
                const auto& a = *source->second; const auto& b = *target->second;
                if (!a.visible || !b.visible || a.type >= looks_.size() || a.body != b.body) break;
                EntityAnim& st = anims_[key];
                st.eventAge[kProcEvAttack] = 0;
                float3 dir = b.pos - a.pos;
                if (a.body >= 0) {
                    int body; float3 ground, up, east, south; float lift;
                    if (placeObject(a, body, ground, up, east, south, lift))
                        dir = {dot(dir, east), dot(dir, up), dot(dir, south)};
                }
                st.attackDirection = dir;
                if (looks_[a.type].tracer && tracers_.size() < 512) tracers_.push_back({a, b});
                break;
            }
            case kEvHit: if (it != anims_.end()) it->second.eventAge[kProcEvHit] = 0; break;
            case kEvAbility: if (it != anims_.end()) it->second.castAge = 0; break;
            case kEvSpawned: case kEvTrainDone: {
                EntityAnim& st = anims_[key]; // may arrive before the object is first drawn
                st.eventAge[kProcEvSpawn] = 0;
                st.seen = frame_;
                break;
            }
            case kEvDied:
                if (it != anims_.end() && !it->second.dead) {
                    EntityAnim& st = it->second;
                    st.dead = true;
                    st.deathAge = 0;
                    st.eventAge[kProcEvDeath] = 0;
                    if (st.initialized) events_.push_back({"death", ev.a, st.type, st.last.owner, st.last.pos});
                }
                break;
            default: break;
        }
    }
}

RtsView::EntityAnim& RtsView::stateFor(uint64_t key, const SceneObject& o) {
    EntityAnim& st = anims_[key];
    if (!st.initialized || st.type != o.type) {
        const TypeLook& L = looks_[o.type];
        const uint64_t h = mix64(key);
        st.type = o.type;
        st.choice = int(h % L.models.size());
        st.anim.setModel(L.models[size_t(st.choice)].model);
        st.graph.reset(L.models[size_t(st.choice)].graph);
        st.yaw = yawOf(o.facing);
        st.lastPos = o.pos;
        st.phase = float((h >> 20) & 0xffff) / 65536.0f;
        st.baseKind = -1;
        st.rt.clear();
        st.poseValid = false;
        st.initialized = true;
        if (st.eventAge[kProcEvSpawn] < 1) events_.push_back({"spawn", o.entity, o.type, o.owner, o.pos});
    }
    st.seen = frame_;
    return st;
}

uint32_t RtsView::stateBits(const EntityAnim& st, const SceneObject& o, bool moving) const {
    const TypeLook& L = looks_[o.type];
    uint32_t state = 0;
    if (st.dead) state |= kProcDead;
    if (moving) state |= kProcMoving;
    if (st.eventAge[kProcEvAttack] < 1.5f || (o.order == uint8_t(OrderType::Attack) + 1 && o.hasTarget)) state |= kProcAttacking;
    if (!moving && o.order == uint8_t(OrderType::Gather) + 1) state |= kProcGathering;
    if (!moving && (o.order == uint8_t(OrderType::Build) + 1 || o.order == uint8_t(OrderType::Repair) + 1)) state |= kProcBuilding;
    if (o.carry > 0) state |= kProcCarrying;
    if (o.build < 1) state |= kProcConstructing;
    if (o.producing) state |= kProcProducing;
    if (L.air) state |= kProcAir;
    if (!(state & (kProcDead | kProcMoving | kProcAttacking | kProcGathering | kProcBuilding | kProcConstructing | kProcProducing))) state |= kProcIdle;
    return state;
}

void RtsView::emitClipEvents(EntityAnim& st, const ModelChoice& ch, const SceneObject& o) {
    for (int l = 0; l < Animator::kLayers; ++l) {
        const int clip = st.anim.clip(l);
        const float t = st.anim.time(l);
        if (clip >= 0 && clip == st.prevClip[l]) {
            const float t0 = st.prevTime[l];
            for (int k = 0; k < kAnimCount; ++k) {
                const ClipSpec& cs = ch.clips[k];
                if (cs.events.empty() || std::find(cs.clips.begin(), cs.clips.end(), clip) == cs.clips.end()) continue;
                for (const auto& [at, name] : cs.events) {
                    const bool crossed = t >= t0 ? (at > t0 && at <= t) : (at > t0 || at <= t); // wrapped loop
                    if (crossed) events_.push_back({name, o.entity, o.type, o.owner, o.pos});
                }
            }
        }
        st.prevClip[l] = clip;
        st.prevTime[l] = t;
    }
}

void RtsView::animate(EntityAnim& st, const SceneObject& o, float dt, const MapGrid* grid, const float3* viewPos) {
    const TypeLook& L = looks_[o.type];
    const ModelChoice& ch = L.models[size_t(st.choice)];
    // ---- kinematics from the interpolated positions
    float3 d = o.pos - st.lastPos;
    d.y = 0;
    float dist = length(d);
    if (dist > 4.0f) dist = 0; // teleport (garrison exit, editor move)
    st.lastPos = o.pos;
    st.distance += dist;
    if (dt > 0) {
        const float inst = dist / dt;
        st.accel = (inst - st.speed) / dt;
        st.speed += (inst - st.speed) * std::min(1.0f, dt * 12.0f);
    }
    if (!st.dead) {
        // Script attack cues have no Combat component: remember their shot direction.
        const bool aiming = !L.structure && st.eventAge[kProcEvAttack] < 1.5f &&
            st.attackDirection.x * st.attackDirection.x + st.attackDirection.z * st.attackDirection.z > 1e-6f;
        const float diff = wrap(yawOf(aiming ? st.attackDirection : o.facing) - st.yaw);
        const float step = aiming && fresh(st.eventAge[kProcEvAttack], dt) ? 1e9f : (L.turnSpeed > 0 ? L.turnSpeed * dt : 1e9f);
        const float turn = std::clamp(diff, -step, step);
        st.yaw = wrap(st.yaw + turn);
        st.turnRate += ((dt > 0 ? turn / dt : 0) - st.turnRate) * std::min(1.0f, dt * 8.0f);
    }
    for (float& a : st.eventAge) a += dt;
    st.castAge += dt;
    if (st.dead) st.eventAge[kProcEvDeath] = st.deathAge;

    const bool moving = !st.dead && (o.moving || st.speed > 0.15f);
    const uint32_t state = stateBits(st, o, moving);

    // ---- animation LOD: off screen -> clips only keep time; far away -> pose updated less often
    bool updatePose = true;
    if (animLod_ && st.poseValid) {
        const float3 p = viewPos ? *viewPos : o.pos;
        const float interval = projectedAnimationLod(viewProj_, p + float3{0,0.5f,0}, p + float3{0,1.5f,0}).interval;
        st.lodAccum += dt;
        if (st.lodAccum < interval) updatePose = false;
        else st.lodAccum = 0;
    }

    // ---- clips
    Animator& A = st.anim;
    const Model& m = *ch.model;
    auto program=ch.graph;
    EntityAnimationEntry* control=nullptr;
    if(fx_){auto found=fx_->animations.find(o.entity.packed());if(found!=fx_->animations.end())control=&found->second;}
    if(control&&control->graph){
        if(!ch.programmableAnimation)control->error="type view.programmableAnimation must be enabled";
        else {
            auto& cache=animationCache_[&m];auto found=cache.find(control->graph);
            if(found==cache.end()){
                if(cache.size()>=64)cache.erase(cache.begin());
                AnimationCached compiled;
                try{auto traits=animationTraits_;for(const auto& [name,trait]:control->graph->traits)traits[name]=trait;compiled.graph=AnimationGraph::compile(m,control->graph->definition,animationOperations_,traits);}
                catch(const std::exception& e){compiled.error=e.what();}
                found=cache.emplace(control->graph,std::move(compiled)).first;
            }
            control->error=found->second.error;
            if(found->second.graph)program=found->second.graph;
        }
    }
    if(st.graph.graph()!=program.get()){st.graph.reset(program);st.poseValid=false;updatePose=true;}
    if (program) {
        // Rebuild defaults each frame so removing a Lua override restores automatic inputs.
        auto& p = st.graph.parameters();
        p = program->defaults();
        p.set("speed", st.speed); p.set("moving", float(moving)); p.set("dead", float(st.dead));
        p.set("attacking", float((state & kProcAttacking) != 0));
        p.set("hasTarget", float(o.hasTarget));
        {   // imported controllers (docs/unity-animation.md): body-relative move direction, event ages
            const float3 f{std::cos(st.yaw), 0, -std::sin(st.yaw)}, r{-f.z, 0, f.x};
            const float len = length(d);
            const float3 u = moving && len > 1e-5f ? d * (1.f / len) : float3{0, 0, 0};
            p.set("moveX", dot(u, r)); p.set("moveZ", dot(u, f));
            p.set("attackAge", std::min(st.eventAge[kProcEvAttack], 1000.f));
            p.set("hitAge", std::min(st.eventAge[kProcEvHit], 1000.f));
            p.set("spawnAge", std::min(st.eventAge[kProcEvSpawn], 1000.f));
        }
        if (fx_) {
            auto it = fx_->animations.find(o.entity.packed());
            if (it != fx_->animations.end()) {
                for (const auto& [key, value] : it->second.parameters.values) p.set(key, value);
                for (const auto& [key, value] : it->second.parameters.vectors) p.set(key, value);
            }
        }
        if(st.graph.hasContacts()&&grid&&o.body==-1)p.environment=animationEnvironment(o,st,ch,*grid);
        st.graph.update(dt);
        if (updatePose || st.pose.empty()) st.graph.evaluate(st.pose);
        if(control){control->state=st.graph.state();control->contacts=st.graph.contactReport();control->locomotion=st.graph.locomotionReport();if(!st.graph.healthy())control->error=st.graph.error();}
    } else if (!m.clips.empty()) {
        auto pick = [&](int kind) { const auto& v = ch.clips[kind].clips; return v[size_t(mix64(uint64_t(st.distance * 7) + uint64_t(kind)) % v.size())]; };
        auto play = [&](int kind, bool restart) {
            const ClipSpec& cs = ch.clips[kind];
            Animator::Play p;
            p.loop = cs.loop; p.speed = cs.speed; p.fade = cs.fade; p.restart = restart;
            A.play(cs.layer, pick(kind), p);
            A.setMask(cs.layer, cs.layer > 0 ? cs.mask : -1);
        };
        // one-shots from events
        if (fresh(st.eventAge[kProcEvAttack], dt) && ch.hasClip[kAnimAttack] && !st.dead) {
            if (ch.clips[kAnimAttack].layer > 0) play(kAnimAttack, true);
            else if (!moving) { play(kAnimAttack, true); st.baseKind = kAnimAttack; }
        }
        if (fresh(st.castAge, dt) && ch.hasClip[kAnimCast] && !st.dead) {
            play(kAnimCast, true);
            if (ch.clips[kAnimCast].layer == 0) st.baseKind = kAnimCast;
        }
        if (fresh(st.eventAge[kProcEvHit], dt) && ch.hasClip[kAnimHit] && !st.dead && ch.clips[kAnimHit].layer > 0) play(kAnimHit, true);
        if (fresh(st.eventAge[kProcEvSpawn], dt) && ch.hasClip[kAnimSpawn]) {
            play(kAnimSpawn, true);
            if (ch.clips[kAnimSpawn].layer == 0) st.baseKind = kAnimSpawn;
        }
        for (int l = 1; l < Animator::kLayers; ++l) if (A.clip(l) >= 0 && A.finished(l)) A.stop(l, 0.15f);
        // base layer: a running one-shot (attack / spawn / cast) finishes first, unless the unit moves
        const bool oneShot = st.baseKind == kAnimAttack || st.baseKind == kAnimSpawn || st.baseKind == kAnimCast;
        if (st.dead) {
            for(int l=1;l<Animator::kLayers;++l) A.stop(l,0);
            if(!ch.hasClip[kAnimDie] && st.baseKind!=-1){A.stop(0,.08f);st.baseKind=-1;}
            if (ch.hasClip[kAnimDie] && st.baseKind != kAnimDie) { play(kAnimDie, true); st.baseKind = kAnimDie; }
        } else if (!(oneShot && !A.finished(0) && !moving)) {
            int kind = kAnimIdle;
            if ((state & kProcConstructing) && ch.hasClip[kAnimConstruct]) kind = kAnimConstruct;
            else if (moving) kind = (state & kProcCarrying) && ch.hasClip[kAnimCarry] ? kAnimCarry : kAnimMove;
            else if (state & kProcGathering) kind = kAnimGather;
            else if (state & kProcBuilding) kind = ch.hasClip[kAnimBuild] ? kAnimBuild : kAnimGather;
            else if (state & kProcProducing) kind = kAnimWork;
            if (!ch.hasClip[kind]) kind = kind == kAnimCarry ? kAnimMove : kAnimIdle;
            if (!ch.hasClip[kind]) {
                if (st.baseKind != -1) { A.stop(0, 0.25f); st.baseKind = -1; }
            } else if (kind != st.baseKind) {
                play(kind, false);
                st.baseKind = kind;
            }
            const ClipSpec& cs = ch.clips[kind];
            if (ch.hasClip[kind] && (kind == kAnimMove || kind == kAnimCarry)) {
                if (cs.blend.size() >= 2) { // blend space: pick the bracket around the current speed
                    size_t hi = 1;
                    while (hi + 1 < cs.blend.size() && st.speed > cs.blend[hi].speed) ++hi;
                    const auto& a = cs.blend[hi - 1];
                    const auto& b = cs.blend[hi];
                    const float w = std::clamp((st.speed - a.speed) / std::max(b.speed - a.speed, 1e-3f), 0.0f, 1.0f);
                    if (A.clip(0) != a.clip) { Animator::Play p; p.fade = cs.fade; A.play(0, a.clip, p); }
                    A.setBlend(0, b.clip, w);
                    const float ref = a.speed + (b.speed - a.speed) * w;
                    A.setSpeed(0, cs.speed * std::clamp(st.speed / std::max(ref, 1e-3f), 0.3f, 2.5f));
                } else if (cs.speedRef > 0) {
                    A.setSpeed(0, cs.speed * std::clamp(st.speed / cs.speedRef, 0.3f, 2.5f));
                }
            } else {
                A.setBlend(0, -1, 0);
            }
        }
        for (size_t i = 0; i < ch.additive.size(); ++i) {
            const AdditiveSpec& ad = ch.additive[i];
            A.setAdditive(int(i), ad.clip, (ad.when == 0 || (state & ad.when)) ? ad.weight : 0.0f);
        }
        A.update(dt);
        emitClipEvents(st, ch, o);
        if (ch.needsPose && updatePose) A.evaluate(st.pose);
    } else if (ch.needsPose && (updatePose || st.pose.empty())) {
        bindPose(m, st.pose);
    }

    // ---- procedural
    ProcInputs in;
    in.time = float(lastTime_);
    in.dt = dt;
    in.speed = st.speed;
    in.distance = st.distance;
    in.turnRate = st.turnRate;
    in.accel = st.accel;
    in.phase = st.phase;
    in.state = state;
    for (int i = 0; i < kProcEvCount; ++i) in.eventAge[i] = st.eventAge[i];
    if (!st.dead) in.eventAge[kProcEvDeath] = 1e9f;
    if (o.hasTarget) {
        float3 to = o.target - o.pos;
        in.hasTarget = true;
        in.aimYaw = wrap(std::atan2(-to.z, to.x) - st.yaw);
        in.aimPitch = std::atan2(to.y, std::max(0.01f, std::sqrt(to.x * to.x + to.z * to.z)));
    }
    if (grid && grid->w > 0) { // terrain normal around the object, rotated into object space
        const float e = 0.6f;
        const float hx = heightAt(*grid, o.pos.x + e, o.pos.z) - heightAt(*grid, o.pos.x - e, o.pos.z);
        const float hz = heightAt(*grid, o.pos.x, o.pos.z + e) - heightAt(*grid, o.pos.x, o.pos.z - e);
        const float3 n = normalize(float3{-hx, 2 * e, -hz});
        in.groundNormal = mat4::rotateY(-st.yaw).transformDir(n);
    }
    st.objXf = mat4{};
    if (!ch.procs.empty() && !st.graph.hasContacts()) {
        // node modifiers act on the pose: skip them with the pose when LOD postponed it
        static Pose none; // a postponed pose must not take the node modifiers twice
        applyProcedural(ch.procs, in, st.rt, ch.needsPose && updatePose ? st.pose : none, st.objXf);
    }
    if (ch.needsPose && updatePose) {
        worldMatrices(m, st.pose, st.world);
        // foot IK: plant two-bone legs on the terrain under each foot (ramps, cliffs, slopes)
        if (grid && grid->w > 0 && !ch.legs.empty() && !moving && !st.graph.hasContacts()) {
            const float scale = L.scale, ground = heightAt(*grid, o.pos.x, o.pos.z);
            const mat4 toWorld = mat4::translate({o.pos.x, ground, o.pos.z}) * mat4::rotateY(st.yaw) * mat4::scale({scale, scale, scale}) * ch.fix;
            const mat4 toModel = toWorld.inverse();
            for (const auto& leg : ch.legs) {
                const float3 foot = {st.world[size_t(leg.foot)].at(0, 3), st.world[size_t(leg.foot)].at(1, 3), st.world[size_t(leg.foot)].at(2, 3)};
                float3 fw = toWorld.transformPoint(foot);
                const float delta = heightAt(*grid, fw.x, fw.z) - ground;
                if (std::fabs(delta) < 0.01f) continue;
                fw.y += delta;
                const float3 target = toModel.transformPoint(fw);
                const float3 knee = {st.world[size_t(leg.knee)].at(0, 3), st.world[size_t(leg.knee)].at(1, 3), st.world[size_t(leg.knee)].at(2, 3)};
                solveTwoBoneIK(m, st.pose, st.world, leg.hip, leg.knee, leg.foot, target, knee + float3{0, 0, 0.5f});
            }
        }
        st.poseValid = true;
    }
}

} // namespace aiverse::game
