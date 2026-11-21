// Per-object animation state: smoothed facing and speed, unit state -> clips on the Animator
// layers (blend spaces, additive clips), sim events (attack, hit, spawn, death) -> one-shot clips
// and procedural envelopes, clip events, foot IK, animation LOD, corpses that outlive their
// entity. Presentation only; reads the scene, never the simulation.
#include "RtsView.h"
#include "engine/render/AnimationLod.h"
#include "engine/view/View.h"
#include "engine/view/AppearanceAuthoringService.h"
#include "../RtsModule.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace aiverse::game {

using namespace render;

std::shared_ptr<const AnimationEnvironment> RtsView::animationEnvironment(const SceneObject& o,const EntityAnim& st,const ModelChoice& ch,const MapGrid& grid){
    auto environment=std::make_shared<AnimationEnvironment>();const auto& look=looks_[o.type];
    EntityFxValues fx;bool hasFx=fxFor(o,fx);float lift=hasFx?fx.v[EntityFxValues::kLift]:0;
    auto pos=float3{o.pos.x,(!look.structure&&!look.air?groundAt(grid,o.pos.x,o.pos.z):heightAt(grid,o.pos.x,o.pos.z))+(look.air?look.flyHeight:0)+lift,o.pos.z};
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
    auto ground=[this,&grid](float3 p){return groundAt(grid,p.x,p.z);};
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
    // Most simulation ticks have no attack events. Avoid allocating and
    // populating a scene-wide lookup for events that never query it.
    std::unordered_map<uint64_t, const SceneObject*> objects;
    bool objectsIndexed = false;
    for (const auto& ev : s.events) {
        if (!ev.a.valid()) continue;
        const uint64_t key = ev.a.packed();
        auto it = anims_.find(key);
        switch (ev.type) {
            case kEvAttack: {
                if (!objectsIndexed) {
                    objects.reserve(s.objects.size());
                    for (const auto& o : s.objects)
                        if (!o.isProjectile) objects.emplace(o.entity.packed(), &o);
                    objectsIndexed = true;
                }
                auto source = objects.find(key), target = objects.find(ev.b.packed());
                if (!ev.b.valid() && source != objects.end() && source->second->visible) { anims_[key].eventAge[kProcEvAttack] = 0; break; }   // free fire (arsenal): no target
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
        auto resumed=reloadPlayback_.find(key);
        const bool compatibleReload=resumed!=reloadPlayback_.end() &&
            resumed->second.type==o.type &&
            frame_>=resumed->second.capturedFrame &&
            frame_-resumed->second.capturedFrame<=120;
        st.type = o.type;
        st.choice = int(h % L.models.size());
        auto saved = streamingChoices_.find(key);
        if (saved != streamingChoices_.end() && saved->second.first == o.type &&
            saved->second.second>=0 &&
            size_t(saved->second.second)<L.models.size())
            st.choice = saved->second.second;
        else if (L.nextVariant < L.pendingVariants.size())
            streamingChoices_[key] = {o.type, st.choice};
        if(compatibleReload && resumed->second.choice>=0 &&
           size_t(resumed->second.choice)<L.models.size())
            st.choice=resumed->second.choice;
        st.anim.setModel(L.models[size_t(st.choice)].model);
        st.graph.reset(L.models[size_t(st.choice)].graph);
        st.yaw = yawOf(o.facing);
        st.lastPos = o.pos;
        st.phase = float((h >> 20) & 0xffff) / 65536.0f;
        st.baseKind = -1;
        st.rt.clear();
        st.poseValid = false;
        st.initialized = true;
        if(resumed!=reloadPlayback_.end()) {
            // Consume once: an entity recycled with a different generation,
            // missing for >120 frames, or changed type must not inherit
            // playback. Compatible reimports restore only scalar clocks,
            // never obsolete Model/rig/pose/native solver resources.
            auto previous=std::move(resumed->second);
            reloadPlayback_.erase(resumed);
            if(compatibleReload) {
                std::copy(std::begin(previous.eventAge),
                          std::end(previous.eventAge),
                          std::begin(st.eventAge));
                st.castAge=previous.castAge;
                st.dead=previous.dead;
                st.deathAge=previous.deathAge;
                st.phase=previous.phase;
                st.yaw=previous.yaw;
                st.speed=previous.speed;
                st.lastPos=previous.lastPos;
                if(!previous.graph.state.empty())
                    st.pendingReloadPlayback=std::move(previous.graph);
                // The new Model may have reordered clips or changed its
                // animation duration. Restore by authored name + normalized
                // phase only when the same semantic base kind still maps to
                // that clip. Never transfer old Animator layer indices,
                // masks, crossfade/IK or stale Model pointers.
                const auto& choice=L.models[size_t(st.choice)];
                const auto* model=choice.model;
                if(!st.graph.graph() && model &&
                   !previous.baseClip.empty() &&
                   previous.baseKind>=0 &&
                   previous.baseKind<kAnimCount &&
                   choice.hasClip[previous.baseKind] &&
                   choice.clips[previous.baseKind].loop==previous.baseLoop &&
                   std::isfinite(previous.basePhase) &&
                   previous.basePhase>=0.f && previous.basePhase<=1.f) {
                    const int clip=model->findClip(previous.baseClip);
                    if(clip>=0 && size_t(clip)<model->clips.size() &&
                       model->clips[size_t(clip)].duration>0.f &&
                       std::find(
                           choice.clips[previous.baseKind].clips.begin(),
                           choice.clips[previous.baseKind].clips.end(),clip)!=
                           choice.clips[previous.baseKind].clips.end()) {
                        Animator::Play play;
                        play.fade=0;
                        play.restart=true;
                        play.loop=previous.baseLoop;
                        play.speed=choice.clips[previous.baseKind].speed;
                        st.anim.play(0,clip,play);
                        if(st.anim.seek(0,previous.basePhase*
                                       model->clips[size_t(clip)].duration))
                            st.baseKind=previous.baseKind;
                    }
                }
            }
        }
        // Source hot reload is not a gameplay spawn. No duplicate
        // presentation "spawn" event is emitted on a valid rebind.
        if(!compatibleReload && st.eventAge[kProcEvSpawn] < 1)
            events_.push_back({"spawn", o.entity, o.type, o.owner, o.pos});
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
    // A finished, stationary visual without clips, graph, procedural effects,
    // pose, or programmable overrides has no animation work to evaluate.
    // Keep hit/spawn/death event clocks advancing for the normal colour path.
    // This is opt-in until its contribution to large RTS scenes is measured.
    static const bool staticFastPath = [] {
        const char* flag = std::getenv("AIVERSE_RTS_STATIC_ANIMATE_FAST");
        return flag && std::strcmp(flag, "1") == 0;
    }();
    if (staticFastPath && ch.model &&
        (L.structure || rules_->types[o.type].speed <= Fixed{}) &&
        !st.dead && !o.moving && o.build >= 1.0f &&
        ch.staticAnimateEligible && !st.graph.graph() &&
        st.anim.clip(0) < 0 &&
        !o.parented && (!fx_ || (fx_->animations.empty() && fx_->entries.empty())) &&
        o.pos.x == st.lastPos.x && o.pos.y == st.lastPos.y &&
        o.pos.z == st.lastPos.z &&
        std::fabs(wrap(yawOf(o.facing) - st.yaw)) <= 1.0e-6f) {
        // Preserve the original smoothing state even if a script starts
        // moving this object later. Zero translation/turn means the fast
        // update can skip square roots and complex animation evaluation.
        if (dt > 0.0f) {
            st.accel = -st.speed / dt;
            st.speed += (-st.speed) * std::min(1.0f, dt * 12.0f);
            st.turnRate += (-st.turnRate) * std::min(1.0f, dt * 8.0f);
        }
        for (float& age : st.eventAge) age += dt;
        st.castAge += dt;
        st.objXf = mat4{};
        ++staticAnimateFastHits_;
        return;
    }
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

    // The same entity animation entry is used for locomotion, stride and
    // programmable animation. Resolve it once instead of three map lookups
    // per visible object (especially costly in large editor scenes).
    EntityAnimationEntry* control = nullptr;
    if (fx_ && !fx_->animations.empty()) {
        auto found = fx_->animations.find(o.entity.packed());
        if (found != fx_->animations.end()) control = &found->second;
    }
    float locomotionSpeed = -1.f;
    float kingstonStride = 1.f;
    if (control) {
        const auto& values = control->parameters.values;
        auto locomotion = values.find("locomotionSpeed");
        if (locomotion != values.end()) locomotionSpeed = locomotion->second;
        auto stride = values.find("strideScale");
        if (stride != values.end()) kingstonStride = std::clamp(stride->second, .7f, 1.f);
    }
    if (locomotionSpeed >= 0.f) st.speed = locomotionSpeed;
    const bool moving = !st.dead && (locomotionSpeed >= 0.f ? st.speed > .015f : (o.moving || st.speed > .15f));
    // Plain scenery has no graph, clip or procedural consumer of the state
    // mask. Avoid inspecting its action/production/target flags each frame.
    const Model& m = *ch.model;
    const bool needsState = ch.graph || (control && control->graph) ||
        !m.clips.empty() || !ch.procs.empty();
    const uint32_t state = needsState ? stateBits(st, o, moving) : 0;

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
    auto program=ch.graph;
    if(control&&control->graph){
        if(!ch.programmableAnimation)control->error="type view.programmableAnimation must be enabled";
        else {
            auto& cache=animationCache_[&m];auto found=cache.find(control->graph);
            if(found==cache.end()){
                if(cache.size()>=64)cache.erase(cache.begin());
                AnimationCached compiled;
                try{auto traits=animationTraits_;for(const auto& [name,trait]:control->graph->traits)traits[name]=trait;compiled.graph=view::AppearanceAuthoringService::compileGraph(
                    m,control->graph->definition,read_,animationOperations_,traits);}
                catch(const std::exception& e){compiled.error=e.what();}
                found=cache.emplace(control->graph,std::move(compiled)).first;
            }
            control->error=found->second.error;
            if(found->second.graph)program=found->second.graph;
        }
    }
    // Defer the ownership handoff until the generic pose has actually
    // passed validation below. Otherwise an invalid View pose switches to
    // legacy animation without restarting its stale clocks in this frame.
    const bool wasGenericViewPose=st.wasGenericViewPose;
    if(st.graph.graph()!=program.get()){
        // A compatible authoring/Lua graph replacement must not restart an
        // ongoing attack/move/death clip. Preserve only names/phase (never
        // old rig/native state/Model pointers). The generic ECS source has
        // its own sole animation clock and must not be seeded from legacy.
        if(!st.genericViewPose && !wasGenericViewPose)
            (void)st.graph.rebindPlayback(program);
        else st.graph.reset(program);
        st.poseValid=false;
        updatePose=true;
    }
    if(st.genericViewPose && genericView_) {
        const Entity presentation=genericView_->presentationEntity(st.genericViewId);
        const auto* evaluated=genericView_->presentation().tryGetConst<
            ::aiverse::lib::animation::AnimationPoseComponent>(presentation);
        if(evaluated && evaluated->valid && evaluated->local.size()==m.nodes.size()) {
            st.pose=evaluated->local;
            st.world=evaluated->matrices;
            st.poseValid=true;
        } else {
            // An invalid or pending pose falls back to legacy Graph updates.
            st.genericViewPose=false;
        }
    }
    if(!st.genericViewPose && wasGenericViewPose) {
        // Reactivating the legacy renderer is a fresh animation owner.
        // Reset both clocks: the graph instance and the imported clip
        // Animator must not resume pre-MotionWeave playback time.
        st.graph.reset(program);
        A.setModel(nullptr);
        A.setModel(&m);
        st.baseKind=-1;
        st.poseValid=false;
        updatePose=true;
    }
    st.wasGenericViewPose=st.genericViewPose;
    if(st.pendingReloadPlayback) {
        // An entity may select a programmable/Lua override instead of its
        // TypeLook default graph. Restore ONLY after the final program has
        // been selected, and never seed the independent generic ECS writer.
        if(program && !st.genericViewPose && st.graph.graph()==program.get()) {
            try {
                (void)st.graph.restorePlayback(*st.pendingReloadPlayback);
            }catch(...) {
                // Optional cosmetic clock recovery must not break a newly
                // published and GPU-safe Model/graph frame.
            }
        }
        st.pendingReloadPlayback.reset();
    }
    if (program && !st.genericViewPose) {
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
        if (fx_ && !fx_->animations.empty()) {
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
    } else if (!st.genericViewPose && !m.clips.empty()) {
        // MotionWeave already advanced this frame and supplied local/world
        // poses. Never tick the legacy Animator for the same source.
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
                    A.setSpeed(0, cs.speed / kingstonStride * (locomotionSpeed >= 0.f ? std::max(0.f, st.speed / std::max(ref, 1e-3f)) : std::clamp(st.speed / std::max(ref, 1e-3f), 0.3f, 2.5f)));
                } else if (cs.speedRef > 0) {
                    A.setSpeed(0, cs.speed / kingstonStride * (locomotionSpeed >= 0.f ? std::max(0.f, st.speed / cs.speedRef) : std::clamp(st.speed / cs.speedRef, 0.3f, 2.5f)));
                }
            } else {
                A.setBlend(0, -1, 0);
            }
        }
        for (size_t i = 0; i < ch.additive.size(); ++i) {
            const AdditiveSpec& ad = ch.additive[i];
            A.setAdditive(int(i), ad.clip, (ad.when == 0 || (state & ad.when)) ? ad.weight : 0.0f);
        }
        // Game-provided weapon intent is read-only presentation, before pose evaluation.
        if(control&&!st.dead){
            auto parameter=[&](const char* name,float fallback){auto it=control->parameters.values.find(name);return it==control->parameters.values.end()?fallback:it->second;};
            int weapon=int(parameter("kingstonWeapon",-1));
            if(weapon>=0&&weapon<4){
                const char* suffix[]={"Pistol","Smg","Shotgun","Rocket"};
                const bool aiming=parameter("kingstonAiming",0)>0;
                const bool shooting=st.eventAge[kProcEvAttack]<.22f;
                std::string clipName=std::string(shooting?"Shoot":aiming?"Aim":"Hold")+suffix[weapon];
                int clip=m.findClip(clipName);
                if(clip>=0){Animator::Play spec;spec.loop=!shooting;spec.fade=.12f;spec.restart=shooting&&fresh(st.eventAge[kProcEvAttack],dt);
                    A.play(1,clip,spec);A.setMask(1,m.findNode("Spine"));}
            }
        }
        A.update(dt);
        emitClipEvents(st, ch, o);
        if (ch.needsPose && updatePose) A.evaluate(st.pose);
    } else if (!st.genericViewPose && ch.needsPose && (updatePose || st.pose.empty())) {
        bindPose(m, st.pose);
    }

    // Kingston weapon overlay follows the selected arsenal item, including fists.
    if(!st.dead&&!st.pose.empty())for(const auto&wp:ch.weaponPoses)if(wp.weapon==o.heldWeapon){
        int clip=o.weaponAim&&wp.aim>=0?wp.aim:wp.hold;float age=0;
        if(wp.attack>=0&&st.eventAge[kProcEvAttack]<m.clips[wp.attack].duration){clip=wp.attack;age=st.eventAge[kProcEvAttack];}
        if(clip>=0){auto mask=subtreeMask(m,ch.weaponMask);sampleClip(m.clips[clip],age,st.pose,1.f,&mask);}break;
    }
    // Most scenery has no procedural modifiers. Prepare inputs only when
    // applyProcedural will consume them; always reset the object transform.
    st.objXf = mat4{};
    if (!ch.procs.empty() && !st.graph.hasContacts()) {
        ProcInputs in;
        in.time = float(lastTime_);
        in.dt = dt;
        in.speed = st.speed;
        in.distance = st.distance;
        in.turnRate = st.turnRate;
        in.accel = st.accel;
        in.steer = o.steer;
        in.phase = st.phase;
        in.state = state;
        for (int i = 0; i < kProcEvCount; ++i) in.eventAge[i] = st.eventAge[i];
        if (!st.dead) in.eventAge[kProcEvDeath] = 1e9f;
        if (o.hasTarget) {
            const float3 to = o.target - o.pos;
            in.hasTarget = true;
            in.aimYaw = wrap(std::atan2(-to.z, to.x) - st.yaw);
            in.aimPitch = std::atan2(to.y, std::max(0.01f, std::sqrt(to.x * to.x + to.z * to.z)));
        }
        if (grid && grid->w > 0) {
            const float e = 0.6f;
            const float hx = heightAt(*grid, o.pos.x + e, o.pos.z) - heightAt(*grid, o.pos.x - e, o.pos.z);
            const float hz = heightAt(*grid, o.pos.x, o.pos.z + e) - heightAt(*grid, o.pos.x, o.pos.z - e);
            const float3 n = normalize(float3{-hx, 2 * e, -hz});
            in.groundNormal = mat4::rotateY(-st.yaw).transformDir(n);
        }
        // A postponed pose must not take the node modifiers twice.
        static Pose none;
        applyProcedural(ch.procs, in, st.rt, ch.needsPose && updatePose ? st.pose : none, st.objXf);
    }
    if (ch.needsPose && updatePose) {
        // Kingston corner stride: shorten leg excursion without slowing the actor.
        // The animation entry was already resolved above; reuse the same
        // stride override for pose correction rather than searching again.
        const float stride = kingstonStride;
        if(moving&&stride<1.f)for(const auto&leg:ch.legs)for(int node:{leg.hip,leg.knee})st.pose[node].r=slerp(m.nodes[node].r,st.pose[node].r,stride);
        if(!st.genericViewPose)
            worldMatrices(m, st.pose, st.world);
        // foot IK: plant two-bone legs on the terrain under each foot (ramps, cliffs, slopes)
        if (grid && grid->w > 0 && !ch.legs.empty() && (!moving || locomotionSpeed >= 0.f) && !st.graph.hasContacts()) {
            auto support=animationEnvironment(o,st,ch,*grid);
            const mat4 contactWorld=support->modelToWorld;
            const mat4 toModel = contactWorld.inverse();
            for (const auto& leg : ch.legs) {
                const float3 foot = {st.world[size_t(leg.foot)].at(0, 3), st.world[size_t(leg.foot)].at(1, 3), st.world[size_t(leg.foot)].at(2, 3)};
                float3 fw = contactWorld.transformPoint(foot);
                const auto& bind=m.bindWorld[size_t(leg.foot)];
                const float rest=contactWorld.transformPoint({bind.at(0,3),bind.at(1,3),bind.at(2,3)}).y;
                if(moving && fw.y>rest+.045f)continue; // retain swing clearance
                IKHit hit;
                if(!support->raycast(fw+float3{0,.16f,0},fw-float3{0,.25f,0},hit)||hit.normal.y<.6f)continue;
                const float sole=std::max(.008f,rest-contactWorld.transformPoint({0,0,0}).y);
                const float delta=std::clamp(hit.point.y+std::min(sole,.035f)-fw.y,-.12f,.12f);
                if(std::fabs(delta)<.002f)continue;
                fw.y+=delta;
                const float3 target = toModel.transformPoint(fw);
                const float3 knee = {st.world[size_t(leg.knee)].at(0, 3), st.world[size_t(leg.knee)].at(1, 3), st.world[size_t(leg.knee)].at(2, 3)};
                solveTwoBoneIK(m, st.pose, st.world, leg.hip, leg.knee, leg.foot, target, knee + float3{0, 0, 0.5f});
            }
        }
        st.poseValid = true;
    }
}

} // namespace aiverse::game
