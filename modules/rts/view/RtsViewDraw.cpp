// Drawing one object: model choice (variant, LOD, construction / damage stage), placement,
// baked or posed skinning, dissolve, selection outline, attachments on sockets, effect anchors;
// projectiles leaving muzzles; building ghosts and previews for portraits / editor.
#include "RtsView.h"

#include <algorithm>
#include <cmath>

namespace rts::game {

using namespace render;

float3 RtsView::nodeWorldPos(const EntityAnim& st, const ModelChoice& ch, int node, float3 offset) const {
    (void)ch;
    float3 local = node >= 0 && size_t(node) < st.world.size() ? st.world[size_t(node)].transformPoint(offset) : offset;
    return st.drawn.transformPoint(local);
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
    mat4 base;
    if (t.structure && L.shape && !stage) {
        // shapes are built on a 1x1 footprint: stretch to the footprint, height follows the larger side
        base = mat4::translate(p) * mat4::scale({L.sx * k, k * std::max(L.sx, L.sz) * 0.75f * grow, L.sz * k});
    } else if (t.structure) {
        base = mat4::translate(p) * mat4::rotateY(st.yaw) * mat4::scale({k, k * grow, k});
    } else {
        base = mat4::translate(p) * mat4::rotateY(st.yaw) * mat4::scale({k, k, k});
    }
    if (hasFx) base = base * mat4::scale({fx.scale(0), fx.scale(1), fx.scale(2)});
    if (placement) base = *placement * base;   // flat maps: untouched (bit-identical)
    ModelDraw d;
    d.model = show->model;
    d.world = base * st.objXf * show->fix;
    d.materials = &show->materials;
    d.nodeMaterial = show->nodeMaterial.empty() ? nullptr : &show->nodeMaterial;
    d.variant = show->variant;
    st.drawn = base * st.objXf * ch.fix;

    // ---- pose / skinning
    BakedSample bs;
    static std::vector<mat4> lodWorld;
    if (show == &ch) {
        if (ch.needsPose && !st.world.empty()) { d.nodeWorld = &st.world; d.pose = &st.pose; }
        if (ch.baked) {
            const auto b = st.anim.baseSample();
            bs = {b.clipA, b.clipB, b.timeA, b.timeB, b.w, b.loopA, b.loopB};
            d.baked = &bs;
        }
    } else if (show == lod) {
        auto map = [&](int c) { return c >= 0 && size_t(c) < lod->clipMap.size() ? lod->clipMap[size_t(c)] : -1; };
        if (lod->baked) {
            const auto b = st.anim.baseSample();
            bs = {map(b.clipA), map(b.clipB), b.timeA, b.timeB, b.w, b.loopA, b.loopB};
            d.baked = &bs;
        } else if (ch.needsPose && !st.world.empty()) { // same skeleton by node names
            lodWorld = lod->model->bindWorld;
            for (size_t n = 0; n < lodWorld.size() && n < lod->nodeMap.size(); ++n)
                if (lod->nodeMap[n] >= 0 && size_t(lod->nodeMap[n]) < st.world.size()) lodWorld[n] = st.world[size_t(lod->nodeMap[n])];
            d.nodeWorld = &lodWorld;
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
    if (hasFx) {
        using V = EntityFxValues;
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
    if (tintOverride) { d.tint = *tintOverride; d.flags = kDrawNoCastShadow; d.flash = 0; d.dissolve = 0; }
    if (outline_ && !st.dead && !tintOverride && (o.selected || o.hovered)) {
        const float3 c = o.selected ? (o.owner == 0 ? float3{0.9f, 0.9f, 0.35f} : float3{0.25f, 0.95f, 0.3f}) : float3{0.8f, 0.8f, 0.8f};
        d.outline = {c.x, c.y, c.z, 0.03f * std::max(1.0f, k)};
    }
    if (d.tint.w <= 0.0f) return;   // fully transparent (view.entity_fx opacity 0): nothing to draw, no anchors
    r.drawModel(d);

    // ---- attachments on sockets (weapons, cargo, banners)
    if (show == &ch || (stage && stage->choice.model == ch.model)) {
        const bool moving = !st.dead && (o.moving || st.speed > 0.15f);
        const uint32_t state = stateBits(st, o, moving);
        for (const auto& at : ch.attach) {
            if (at.when && !(state & at.when)) continue;
            if (at.carry != -2 && !(o.carry > 0 && (at.carry == -1 || at.carry == o.carryRes))) continue;
            ModelDraw ad = d;
            ad.model = at.model;
            ad.materials = &at.materials;
            ad.nodeWorld = nullptr; ad.pose = nullptr; ad.baked = nullptr; ad.variant = -1; ad.nodeMaterial = nullptr;
            const mat4 socket = at.node >= 0 && size_t(at.node) < st.world.size() ? st.world[size_t(at.node)] : mat4{};
            ad.world = st.drawn * socket * at.local;
            r.drawModel(ad);
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
        r.triangle(back - side, tip - side, tip + side, rgba(255, 180, 65, 180));
        r.triangle(back - side, tip + side, back + side, rgba(255, 180, 65, 180));
        r.line(back, tip, rgba(255, 249, 205, 255));
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
                if (length(ps.launch) > 3) ps.launch = {0, 0, 0}; // stale pose (teleport): do not fly from afar
            }
            events_.push_back({"launch", o.source, s.type, s.last.owner, o.pos + ps.launch});
        }
    } else {
        ps.launch = ps.launch * std::exp(-dt * 7.0f); // converge onto the simulated path
    }
    r.drawMesh(projectile_, mat4::translate(o.pos + ps.launch), {1, 0.95f, 0.8f, 1}, kDrawUnlit);
}

void RtsView::drawGhost(Renderer3D& r, uint16_t type, float3 pos, bool ok, const MapGrid& grid, double time) {
    if (type >= looks_.size()) return;
    static EntityAnim ghost;
    ghost.type = type;
    ghost.choice = 0;
    ghost.objXf = mat4{};
    ghost.world.clear();
    ghost.yaw = 0;
    SceneObject o;
    o.type = type;
    o.owner = 1;
    o.pos = pos;
    const float pulse = 0.4f + 0.08f * float(std::sin(time * 5.0));
    const float4 tint = ok ? float4{0.45f, 1.0f, 0.55f, pulse} : float4{1.0f, 0.35f, 0.3f, pulse};
    if (planets_.active()) { // planets: on the sphere, oriented to the local up
        mat4 place;
        if (planetPlacement(o, place)) drawObject(r, ghost, o, 0, 1, &tint, &place);
        return;
    }
    drawObject(r, ghost, o, heightAt(grid, pos.x, pos.z), 1, &tint);
}

void RtsView::drawPreview(Renderer3D& r, uint16_t type, uint8_t owner, double time, float yaw) {
    if (type >= looks_.size()) return;
    const uint64_t key = (1ull << 62) | (uint64_t(owner) << 16) | type;
    SceneObject o;
    o.type = type;
    o.owner = owner;
    o.facing = {std::cos(yaw), 0, -std::sin(yaw)};
    EntityAnim& st = stateFor(key, o);
    const float dt = st.lastSeenTime < 0 ? 0.0f : float(std::clamp(time - st.lastSeenTime, 0.0, 0.1));
    st.lastSeenTime = time;
    const bool lodWas = animLod_;
    animLod_ = false;
    animate(st, o, dt, nullptr);
    animLod_ = lodWas;
    drawObject(r, st, o, 0, 1);
}

} // namespace rts::game
