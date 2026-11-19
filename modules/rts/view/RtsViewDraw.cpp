// Drawing one object: model choice (variant, LOD, construction / damage stage), placement,
// baked or posed skinning, dissolve, selection outline, attachments on sockets, effect anchors;
// projectiles leaving muzzles; building ghosts and previews for portraits / editor.
#include "RtsView.h"
#include "engine/render/CameraModelSnapshot.h"
#include "RtsPreviewRenderProvider.h"
#include "RtsPreviewResourceProvider.h"
#include "engine/view/View.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace aiverse::game {

using namespace render;

float3 RtsView::nodeWorldPos(const EntityAnim& st, const ModelChoice& ch, int node, float3 offset) const {
    (void)ch;
    float3 local = node >= 0 && size_t(node) < st.world.size() ? st.world[size_t(node)].transformPoint(offset) : offset;
    return st.drawn.transformPoint(local);
}

bool RtsView::muzzleOf(Entity e, float3& pos, float3& dir) const {
    if (!e.valid()) return false;
    auto it = anims_.find(e.packed());
    if (it == anims_.end() || !it->second.initialized || it->second.dead || it->second.type >= looks_.size()) return false;
    const EntityAnim& s = it->second;
    const TypeLook& L = looks_[s.type];
    if (s.choice < 0 || size_t(s.choice) >= L.models.size()) return false;
    const ModelChoice& ch = L.models[size_t(s.choice)];
    if (ch.muzzle < 0) return false;
    pos = nodeWorldPos(s, ch, ch.muzzle, ch.muzzleOffset);
    const float3 f = s.last.facing;
    const float fl = std::sqrt(f.x * f.x + f.z * f.z);
    dir = fl > 1e-4f ? float3{f.x / fl, 0, f.z / fl} : float3{0, 0, 1};
    return true;
}

void RtsView::drawObject(Renderer3D& r, EntityAnim& st, const SceneObject& o, float ground, float alpha, const float4* tintOverride, const mat4* placement) {
    const UnitType& t = rules_->types[o.type];
    const TypeLook& L = looks_[o.type];
    const ModelChoice& ch = L.models[size_t(st.choice)];

    // ---- which model: stage (construction / damage) > LOD > the object's variant
    const ModelChoice* show = &ch;
    const Stage* stage = nullptr;
    for (const auto& sg : L.stages) {
        if (o.build < 1 && L.construction == Construction::Stages && sg.build >= 0 && sg.build <= o.build && (!stage || sg.build > stage->build)) stage = &sg;
        if (o.build >= 1 && sg.hp >= 0 && o.hp <= sg.hp && (!stage || sg.hp < stage->hp)) stage = &sg;
    }
    const ModelChoice* lod = nullptr;
    if (!stage && st.choice == 0 && !L.lods.empty() && !tintOverride) {
        const float camDist = length(camPos_ - (placement ? placement->transformPoint({0, 0, 0}) : o.pos));
        for (const auto& lc : L.lods) if (camDist >= lc.lodDistance) lod = &lc;
    }
    if (stage) show = &stage->choice;
    else if (lod) show = lod;

    // ---- placement
    const float k = L.scale;
    // view.entity_fx: scale about the object's ground point in the model's axes (base ends in a diagonal scale, so
    // the extra diagonal factor commutes with it), lift along the object's up, opacity / colour below
    EntityFxValues fx;
    const bool hasFx = !tintOverride && fxFor(o, fx);
    const float lift = hasFx ? fx.v[EntityFxValues::kLift] : 0.0f;
    // planets: the object's frame (local up = +Y) replaces the flat translation
    const float3 p = placement ? float3{0, (L.air ? L.flyHeight : 0.0f) + lift, 0} : float3{o.pos.x, ground + (L.air ? L.flyHeight : 0.0f) + lift, o.pos.z};
    const float grow = L.construction == Construction::Grow ? std::clamp(o.build, 0.15f, 1.0f) : 1.0f;
    // Only completely static rigid placements can reuse a cached world
    // transform. Exclude procedural animation, authored clips, hierarchy,
    // time/FX variants, planets, construction and LOD switches.
    static const bool cacheEnabled = [] {
        const char* setting = std::getenv("AIVERSE_RTS_PLACEMENT_CACHE");
        return setting && std::strcmp(setting, "1") == 0;
    }();
    const mat4 identity{};
    const bool immutablePlacement = cacheEnabled &&
        !hasFx && !tintOverride && !placement &&
        !o.parented && !st.dead && !o.moving && o.build >= 1.0f &&
        (t.structure || t.speed <= Fixed{}) && show == &ch &&
        L.stages.empty() && L.lods.empty() &&
        ch.staticPlacementEligible &&
        std::memcmp(st.objXf.m, identity.m, sizeof(identity.m)) == 0;
    auto& cached = st.placement;
    const bool reusePlacement = immutablePlacement && cached.valid &&
        cached.model == show->model &&
        cached.pos.x == o.pos.x && cached.pos.y == o.pos.y &&
        cached.pos.z == o.pos.z && cached.ground == ground &&
        cached.yaw == st.yaw && cached.scale == k &&
        cached.sx == L.sx && cached.sz == L.sz &&
        cached.flyHeight == L.flyHeight && cached.air == L.air &&
        cached.structure == t.structure && cached.shape == L.shape &&
        std::memcmp(cached.fix.m, ch.fix.m, sizeof(ch.fix.m)) == 0;
    if (immutablePlacement) {
        if (reusePlacement) ++placementCacheHits_;
        else ++placementCacheMisses_;
    }
    if (!immutablePlacement) cached.valid = false;
    mat4 base;
    if (!reusePlacement) {
    if (t.structure && L.shape && !stage) {
        // shapes are built on a 1x1 footprint: stretch to the footprint, height follows the larger side
        base = mat4::translate(p) * mat4::scale({L.sx * k, k * std::max(L.sx, L.sz) * 0.75f * grow, L.sz * k});
    } else if (t.structure) {
        base = mat4::translate(p) * mat4::rotateY(st.yaw) * mat4::scale({k, k * grow, k});
    } else {
        base = mat4::translate(p) * mat4::rotateY(st.yaw) * mat4::scale({k, k, k});
    }
    if (hasFx && (fx.v[EntityFxValues::kPitch] != 0.0f || fx.v[EntityFxValues::kRoll] != 0.0f)) {
        // view.entity_fx pitch / roll (degrees) in the faced frame (forward +X, up +Y, right +Z), roll first, then
        // pitch, then the facing (Unity's Euler(pitch, yaw, roll) order): pitch > 0 lowers the nose, roll > 0 raises
        // the right side; about the model's origin (lift included), so the model keeps its place
        constexpr float kDeg = 0.017453292f;
        const mat4 tilt = mat4::rotateZ(-fx.v[EntityFxValues::kPitch] * kDeg) * mat4::rotateX(-fx.v[EntityFxValues::kRoll] * kDeg);
        base = mat4::translate(p) * mat4::rotateY(st.yaw) * tilt * mat4::rotateY(-st.yaw) * mat4::translate(-p) * base;
    }
    if (hasFx) base = base * mat4::scale({fx.scale(0), fx.scale(1), fx.scale(2)});
    if (o.parented && !placement) base = o.parentFrame * mat4::scale({k, k * (t.structure ? grow : 1.f), k});
    if (placement) base = *placement * base;   // flat maps: untouched (bit-identical)
    }
    ModelDraw d;
    // Stable identity for persistent scene staging; editor placements,
    // previews and attachments use the existing transient draw path.
    d.persistentId = o.persistentKey ? o.persistentKey :
        (o.entity.valid() ? o.entity.packed() : 0);
    d.model = show->model;
    if (!tintOverride && o.type < actorRevealTypes_.size() && actorRevealTypes_[o.type]) d.flags |= kDrawActorReveal;
    // Hot-reloaded choices pin an immutable published appearance snapshot.
    // The owning TypeLook keeps it alive across this synchronous submission.
    const auto& appearance=show->appearanceSnapshot;
    d.geometry = appearance ? appearance->geometry : show->geometry;
    d.world = reusePlacement ? cached.world : base * st.objXf * show->fix;
    d.materials = appearance ? &appearance->materials : &show->materials;
    const auto* assigned=appearance ? &appearance->nodeMaterial : &show->nodeMaterial;
    d.nodeMaterial = assigned->empty() ? nullptr : assigned;
    d.variant = appearance ? appearance->variant : show->variant;
    st.drawn = reusePlacement ? cached.drawn : base * st.objXf * ch.fix;
    if (immutablePlacement && !reusePlacement) {
        cached.pos = o.pos;
        cached.ground = ground;
        cached.yaw = st.yaw;
        cached.scale = k;
        cached.sx = L.sx;
        cached.sz = L.sz;
        cached.flyHeight = L.flyHeight;
        cached.air = L.air;
        cached.structure = t.structure;
        cached.shape = L.shape;
        cached.model = show->model;
        cached.fix = ch.fix;
        cached.world = d.world;
        cached.drawn = st.drawn;
        cached.valid = true;
    }

    // ---- pose / skinning
    BakedSample bs;
    auto& lodWorld=lodWorldScratch_;
    auto& lodRetargetPose=lodRetargetPoseScratch_;
    if (show == &ch) {
        if (ch.needsPose && !st.world.empty()) { d.nodeWorld = &st.world; d.pose = &st.pose; }
        if (ch.baked) {
            const auto b = st.anim.baseSample();
            bs = {b.clipA, b.clipB, b.timeA, b.timeB, b.w, b.loopA, b.loopB};
            d.baked = &bs;
        }
    } else if (show == lod) {
        auto map = [&](int c) { return c >= 0 && size_t(c) < lod->clipMap.size() ? lod->clipMap[size_t(c)] : -1; };
        if (lod->baked && !st.graph.graph()) {
            const auto b = st.anim.baseSample();
            bs = {map(b.clipA), map(b.clipB), b.timeA, b.timeB, b.w, b.loopA, b.loopB};
            d.baked = &bs;
        } else if (ch.needsPose && !st.world.empty()) {
            // Reuse the exact same explicit retarget rules as native
            // aiverse::View. New target-local matrices respect its authored
            // bind pose instead of copying unrelated source world matrices.
            if(ch.model && lod->compiledPoseRetarget) {
                // Use the plan's precomputed validation/target bind data.
                // A stale source or visual generation must NEVER fall back
                // to copying old node matrices into a replacement skeleton.
                if(st.pose.size()==ch.model->nodes.size() &&
                   ::aiverse::view::retargetCompiledModelPose(
                       *ch.model,*lod->model,
                       ch.modelGeneration,lod->modelGeneration,
                       std::span<const NodePose>(
                           st.pose.data(),st.pose.size()),
                       *lod->compiledPoseRetarget,lodRetargetPose,lodWorld)) {
                    d.pose=&lodRetargetPose;
                    d.nodeWorld=&lodWorld;
                }else {
                    d.pose=nullptr;
                    d.nodeWorld=nullptr; // safe bind pose on stale asset
                    ++legacyLodFallbackStats_.rejectedCompiledPlans;
                    ++legacyLodFallbackStats_.bindPoseFallbacks;
                }
            }else if(legacyLodFallbackMode_==
                    LegacyLodFallbackMode::PreserveWorldMatrices) {
                // Compatibility fallback ONLY for legacy LODs that have
                // never received a compiled plan. Keep the historical
                // visual default until real editor/GPU parity is verified.
                lodWorld=lod->model->bindWorld;
                for(size_t n=0;n<lodWorld.size() && n<lod->nodeMap.size();++n)
                    if(lod->nodeMap[n]>=0 &&
                       size_t(lod->nodeMap[n])<st.world.size())
                        lodWorld[n]=st.world[size_t(lod->nodeMap[n])];
                d.nodeWorld=&lodWorld;
                ++legacyLodFallbackStats_.worldMatrixFallbacks;
            }else {
                // Migration mode for projects that require strict
                // protection against unmatched bone/skeleton layouts.
                // The source animation continues; the distant asset
                // remains at its own bind pose.
                d.pose=nullptr;
                d.nodeWorld=nullptr;
                ++legacyLodFallbackStats_.bindPoseFallbacks;
            }
        }
    } else if (stage && stage->choice.model == ch.model && ch.needsPose && !st.world.empty()) {
        d.nodeWorld = &st.world;
    }

    // ---- colours and effects
    const float4 pc = playerColor(o.colorOwner());
    d.team = {pc.x * pc.x, pc.y * pc.y, pc.z * pc.z};
    d.tint = {L.tint.x, L.tint.y, L.tint.z, alpha};
    d.flash = st.eventAge[kProcEvHit] < 0.3f ? 0.3f * std::exp(-st.eventAge[kProcEvHit] * 12.0f) : 0.0f;
    if (o.build < 1.0f && L.construction == Construction::Grow) { // under construction: washed out
        d.tint = {d.tint.x * 0.75f, d.tint.y * 0.75f, d.tint.z * 0.75f, alpha};
        d.flash += 0.06f;
    }
    if (L.construction == Construction::Dissolve && o.build < 1) d.dissolve = 1.0f - std::clamp(o.build, 0.0f, 1.0f) * 0.97f;
    if (L.dissolveSpawn && st.eventAge[kProcEvSpawn] < 0.8f) d.dissolve = std::max(d.dissolve, 1.0f - st.eventAge[kProcEvSpawn] / 0.8f);
    if (L.dissolveDeath && st.dead) {
        d.dissolve = std::max(d.dissolve, std::clamp((st.deathAge - (L.corpse - 1.2f)) / 1.1f, 0.0f, 1.0f));
        d.tint.w = 1;
    }
    if (o.fade < 0.999f) { d.tint.w *= std::max(0.0f, o.fade); d.flags |= kDrawFadeDepth; }   // route end fade
    if (hasFx) {
        using V = EntityFxValues;
        if (fx.v[V::kKeepShadow] > .5f) d.flags |= kDrawKeepShadow;
        d.tint = {d.tint.x * fx.v[V::kTint], d.tint.y * fx.v[V::kTint + 1], d.tint.z * fx.v[V::kTint + 2], d.tint.w * fx.opacity()};
        const float m = fx.v[V::kTeamMix + 3];
        if (m > 0) {   // team colour towards rgb in display space (Unity's Color.Lerp of _BaseColor), then linear like pc
            const float3 c{pc.x + (fx.v[V::kTeamMix] - pc.x) * m, pc.y + (fx.v[V::kTeamMix + 1] - pc.y) * m, pc.z + (fx.v[V::kTeamMix + 2] - pc.z) * m};
            d.team = {c.x * c.x, c.y * c.y, c.z * c.z};
        }
        const float g = fx.v[V::kTeamGlow];
        d.teamGlow = g * g;   // emission = team colour * glow (display space) -> linear
        if (d.tint.w < 0.99f) d.flags |= kDrawFadeDepth;   // a faded solid: in front of the ground layers drawn later
    }
    if (L.transparent) d.flags |= kDrawTransparent;
    if (tintOverride) { d.tint = *tintOverride; d.flags = kDrawNoCastShadow; d.flash = 0; d.dissolve = 0; }
    // The editor explicitly enables mesh outlines for its selection, independent
    // of game overlay configuration. Hover remains controlled by game overlays.
    const bool sel = o.selected && (outline_ || overlays_.outlineSelected()), hov = o.hovered && overlays_.outlineHovered();
    if (outline_ && !st.dead && !tintOverride && (sel || hov)) {
        const float3 c = sel ? float3{0.25f, 0.62f, 1.0f} : float3{0.8f, 0.8f, 0.8f};
        d.outline = {c.x, c.y, c.z, 0.03f * std::max(1.0f, k)};
    }
    if (d.tint.w <= 0.0f && !(d.flags & kDrawKeepShadow)) return;   // fully transparent (view.entity_fx opacity 0): nothing to draw, no anchors
    // immobile, finished, plain placement: scenery the light baker captures / lightmaps (agent-lightbake)
    if ((t.structure || t.speed <= Fixed{}) && !L.air && !st.dead && o.build >= 1 && !hasFx && !tintOverride && !placement) d.flags |= kDrawStatic;
    if(st.genericViewPose && genericView_ && !tintOverride && !placement &&
       show==&ch) {
        // This is an adapter: the RTS module builds its authored draw packet,
        // while the engine View owns generic model+pose submission.
        // Never submit twice or mutate an already-updated pose here.
        ::aiverse::view::Object packet;
        packet.id=st.genericViewId;
        // Mirror the source identity published to genericView_->update().
        // Without generation + publication epoch, submitOne rejects its
        // already evaluated MotionWeave pose and draws the bind pose.
        packet.generation=(uint64_t(o.type)<<32)|uint32_t(st.choice);
        packet.animationModelGeneration=ch.modelGeneration;
        packet.draw=d;
        packet.graph=ch.graph;
        if(cameraPreparedCapture_)
            cameraPreparedCapture_->recordPrepared(*genericView_,packet);
        genericView_->submitOne(r,packet);
    } else {
        // All other model classes (stages, LODs, baked skinning and
        // procedurally posed objects) submit through the SAME generic
        // View path, without running a second AnimationGraph.
        ::aiverse::View::submitModel(r,d);
    }

    // ---- attachments on sockets (weapons, cargo, banners)
    // No attachment or effect can consume the state mask on plain models.
    // Avoid stateBits() and its event/locomotion checks for this common case.
    if ((show == &ch || (stage && stage->choice.model == ch.model)) &&
        (!ch.attach.empty() || (!tintOverride && !show->fx.empty()))) {
        const bool moving = !st.dead && (o.moving || st.speed > 0.15f);
        const uint32_t state = stateBits(st, o, moving);
        for (const auto& at : ch.attach) {
            if(!at.weapon.empty()&&(st.dead||at.weapon!=o.heldWeapon))continue;
            if (at.when && !(state & at.when)) continue;
            if (at.carry != -2 && !(o.carry > 0 && (at.carry == -1 || at.carry == o.carryRes))) continue;
            ModelDraw ad = d;
            ad.persistentId = 0; // attachments retain legacy transient identity
            ad.model = at.model;
            ad.geometry = {};
            ad.materials = &at.materials;
            ad.nodeWorld = nullptr; ad.pose = nullptr; ad.baked = nullptr; ad.variant = -1; ad.nodeMaterial = nullptr;
            ad.world = {}; // the child's own transform is local to the socket
            ModelDraw parentAnchor = d;
            parentAnchor.world = st.drawn; // source model, not this frame's selected visual LOD
            ModelDraw attached;
            if(ch.model && ::aiverse::view::composeAttachment(
                    parentAnchor,*ch.model,
                    std::span<const mat4>(st.world.data(),st.world.size()),
                    ad,at.node,at.local,attached))
                ::aiverse::View::submitModel(r,attached);
        }
        // ---- effect anchors
        if (!tintOverride)
            for (const auto& f : show->fx) {
                if (f.when && !(state & f.when)) continue;
                if (o.hp > f.hpBelow) continue;
                effects_.push_back({f.effect, o.entity, o.type, o.owner, nodeWorldPos(st, ch, f.node, f.offset),
                                    f.intensity * (f.hpBelow <= 1 ? std::clamp(1.0f - o.hp / std::max(f.hpBelow, 0.01f), 0.2f, 1.0f) : 1.0f)});
            }
    }
}

void RtsView::drawTracers(Renderer3D& r, float dt, const MapGrid* grid) {
    for (auto& shot : tracers_) {
        float3 a = objectViewPos(shot.from, .7f), b = objectViewPos(shot.to, .65f);
        if (grid) {
            a.y += heightAt(*grid, a.x, a.z);
            b.y += heightAt(*grid, b.x, b.z);
        }
        const float distance = length(b - a);
        if (distance < .01f) { shot.age = 1; continue; }
        const float duration = std::clamp(distance / 30.0f, .09f, .24f);
        const float head = std::clamp((shot.age + .025f) / duration, 0.0f, 1.0f);
        const float tail = std::max(0.0f, head - std::min(.4f, .9f / distance));
        const float3 tip = lerp(a, b, head), back = lerp(a, b, tail);
        float3 side = cross(b - a, camPos_ - tip);
        if (length(side) < 1e-5f) side = {0, 1, 0};
        side = normalize(side) * .035f;
        ::aiverse::View::submitTriangle(r,{
            back-side,tip-side,tip+side,rgba(255,180,65,180)});
        ::aiverse::View::submitTriangle(r,{
            back-side,tip+side,back+side,rgba(255,180,65,180)});
        ::aiverse::View::submitLine(r,{back,tip,rgba(255,249,205,255)});
        shot.age += dt;
        if (shot.age >= duration) shot.age = 1;
    }
    tracers_.erase(std::remove_if(tracers_.begin(), tracers_.end(), [](const Tracer& s) { return s.age >= 1; }), tracers_.end());
}

void RtsView::drawProjectile(Renderer3D& r, uint64_t key, const SceneObject& o, float dt) {
    EntityAnim& ps = anims_[key];
    ps.seen = frame_;
    if (!ps.launched) {
        ps.launched = true;
        ps.launch = {0, 0, 0};
        auto src = o.source.valid() ? anims_.find(o.source.packed()) : anims_.end();
        if (src != anims_.end() && src->second.initialized && src->second.type < looks_.size()) {
            const EntityAnim& s = src->second;
            const ModelChoice& ch = looks_[s.type].models[size_t(s.choice)];
            if (ch.muzzle >= 0) {
                const float3 muzzle = nodeWorldPos(s, ch, ch.muzzle, ch.muzzleOffset);
                ps.launch = muzzle - o.pos;
                if (length(ps.launch) > 8) ps.launch = {0, 0, 0}; // stale pose (teleport): do not fly from afar
            }
            events_.push_back({"launch", o.source, s.type, s.last.owner, o.pos + ps.launch});
        }
    } else {
        // converge onto the simulated path across the ground; the height stays: a shot flies at the height of the
        // muzzle it left (the simulation moves projectiles just above the ground)
        const float k = std::exp(-dt * 7.0f);
        ps.launch = {ps.launch.x * k, ps.launch.y, ps.launch.z * k};
    }
    float size = 1;
    if (o.shooterType < looks_.size()) {
        const TypeLook& SL = looks_[o.shooterType];
        size = o.shotWeapon >= 0 && size_t(o.shotWeapon) < SL.projectileScale.size() ? SL.projectileScale[size_t(o.shotWeapon)] : SL.projectileScaleDefault;
    }
    if(size>0) {
        ::aiverse::view::MeshObject packet;
        packet.mesh=&projectile_;
        packet.world=mat4::translate(o.pos+ps.launch)*
                     mat4::scale({size,size,size});
        packet.color={1,0.95f,0.8f,1};
        packet.flags=kDrawUnlit;
        ::aiverse::View::submitMesh(r,packet);
    }
}

bool RtsView::resolveStatic(uint16_t type,uint8_t owner,
                            view_adapter::StaticPreviewModel& output) {
    if(!rules_ || type>=looks_.size() || type>=rules_->types.size())
        return false;
    loadType(type);
    return view_adapter::resolveStaticPreviewModel(
        looks_[type],rules_->types[type],playerColor(owner),output);
}

void RtsView::drawGhost(Renderer3D& r, uint16_t type, float3 pos, bool ok, const MapGrid& grid, double time) {
    // Flat-ground, rigid ghost with a single resolved model: the host
    // supplies model assets and placement height, while View owns model
    // submission. Complex animation/planet/attachments retain legacy.
    if(!planets_.active() && type<looks_.size()) {
        loadType(type);
        const auto& look=looks_[type];
        view_adapter::StaticPreviewModel spec;
        if(view_adapter::resolveStaticPreviewModel(
               look,rules_->types[type],playerColor(1),spec)) {
            const auto description=
                view_adapter::makeGhostPresentation(type,pos,ok,time);
            ::aiverse::view::Object packet;
            if(view_adapter::buildStaticGhostPacket(
                   spec,description.position,
                   heightAt(grid,pos.x,pos.z),
                   description.tint,packet)) {
                view_adapter::submitStaticPreview(r,packet);
                return;
            }
        }
    }
    view_adapter::RtsPreviewRenderProvider::submitGhost(type,pos,ok,time,
        [&](uint16_t requested) {
            if(requested>=looks_.size())return false;
            loadType(requested);
            return true;
        },
        [&](const view_adapter::GhostPresentation& description) {
            // The temporary state is owned by this host; the provider
            // knows nothing about ECS, model residency or planet placement.
            auto& ghost=ghostAnim_;
            ghost.type=description.type;
            ghost.choice=0;
            ghost.objXf=mat4{};
            ghost.world.clear();
            ghost.yaw=0;
            SceneObject o;
            o.type=description.type;
            o.owner=description.owner;
            o.pos=description.position;
            const float4 tint=description.tint;
            if(planets_.active()) {
                mat4 place;
                if(planetPlacement(o,place))
                    drawObject(r,ghost,o,0,1,&tint,&place);
            } else {
                drawObject(r,ghost,o,heightAt(grid,o.pos.x,o.pos.z),1,&tint);
            }
        });
}

void RtsView::drawPreview(Renderer3D& r, uint16_t type, uint8_t owner, double time, float yaw) {
    // Truly static single-variant previews can render through View without
    // allocating EntityAnim, a GraphInstance or advancing the old Animator.
    // All specialized resources and animations retain the legacy fallback.
    if(view_adapter::submitResolvedPreview(*this,r,type,owner,yaw))
        return;
    EntityAnim* current=nullptr;
    SceneObject object;
    view_adapter::RtsPreviewRenderProvider::submitModelPreview(
        type,owner,time,yaw,
        [&](uint16_t requested) {
            if(requested>=looks_.size())return false;
            loadType(requested);
            return true;
        },
        [&](const view_adapter::ModelPreviewPresentation& description) {
            object.type=description.type;
            object.owner=description.owner;
            object.facing=description.facing;
            current=&stateFor(description.sourceKey,object);
            return current->lastSeenTime;
        },
        [&](const view_adapter::ModelPreviewPresentation&,float dt) {
            current->lastSeenTime=time;
            const bool lodWas=animLod_;
            animLod_=false;
            animate(*current,object,dt,nullptr);
            animLod_=lodWas;
            drawObject(r,*current,object,0,1);
        });
}

} // namespace aiverse::game
