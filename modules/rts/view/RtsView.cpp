#include "RtsView.h"
#include "RtsRoadView.h"
#include "RtsShoreView.h"
#include "../../../engine/data/JsonCompose.h"
#include "../RtsModule.h"
#include "../RtsArsenal.h"
#include "../RtsTopDown.h"
#include "imgui.h"
#include "../../../engine/platform/Platform.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>

namespace aiverse::game {

using namespace render;

namespace {
float3 toF(const Vec3& v) { return {float(v.x.toDouble()), float(v.y.toDouble()), float(v.z.toDouble())}; }
} // namespace

RtsView::RtsView() : roads_(std::make_unique<RtsRoadView>()), shores_(std::make_unique<RtsShoreView>()) {}
RtsView::~RtsView() = default;
void RtsView::setRoadMap(const Json& map) { roads_->setMap(map); shores_->setMap(map); }

float4 RtsView::playerColor(int p) { return overlayPlayerColor(p); }   // palette shared with the overlays

bool RtsView::init(gfx::Device& dev, const Rules& rules, FileReader reader) {
#ifndef __EMSCRIPTEN__
    if (preparing_.valid()) preparing_.wait();
    preparing_ = {}; prepareType_ = -1; preparingResource_.clear();
#endif
    rules_ = &rules;
    dev_ = &dev;
    if (!fog_.ready()) fog_.init(dev);
    fieldView_.shutdown();
    fieldView_.init(dev);
    fieldDefsSeen_ = nullptr;
    read_ = reader ? std::move(reader) : FileReader([](const std::string& p, std::string& out) { return platform::readContentFile(p, out); });
    {   // fog of war look: rules "fogOfWar" (visual keys), game.json "fogOfWar" overrides
        std::string text, rulesPath = "rules.json";
        Json game, rj;
        try { if (read_("game.json", text)) { game = Json::parse(text); rulesPath = game.getStr("rules", "rules.json"); } } catch (...) {}
        try { rj = readComposedJson(rulesPath, [this](const std::string& p, std::string& o) { return read_(p, o); }); } catch (...) {}
        fog_.setLook(parseFogLook(rj["fogOfWar"], game["fogOfWar"]));
        spriteDefaults_ = parseSpriteDefaults(rj["sprites"], game["sprites"]);   // 2D: shadow mode, lit
        fogMeshDirty_ = true;
        // overlays: built-ins + rules "overlays" + game.json "overlays" (by id)
        overlays_.init(rules, rj["overlays"], game["overlays"], contentBase_);
        overlays_.initGpu(dev);
        roads_->init(dev, rj["roadTypes"], read_);
        shores_->init(dev, rj["shoreTypes"], read_);   // water shores (docs/water.md)
    }
    if (!models_.count("shape:generic")) buildShapes(dev);
    if (twoD_) { twoD_->shutdown(dev); twoD_.reset(); }
    looks_.clear();
    warnings_.clear();
    geometryScene_.shutdown(dev);
    try {
        std::string text;
        if (read_("game.json", text)) {
            const auto game = Json::parse(text);
            const auto file = game.getStr("geometryScene");
            if (!file.empty()) {
                if (!read_(file, text)) throw std::runtime_error("geometryScene: file not found: " + file);
                geometryScene_.load(dev, Json::parse(text), read_);
            }
        }
    } catch (const std::exception& e) { warnings_.push_back(e.what()); }

    {   // shared materials of the game (docs/models.md "Material library")
        materialLib_ = Json::object();
        std::string text;
        if (read_("materials.json", text)) {
            try { materialLib_ = Json::parse(text)["materials"]; }
            catch (const std::exception& e) { warnings_.push_back(std::string("materials.json: ") + e.what()); }
            if (!materialLib_.isObject()) materialLib_ = Json::object();
        }
    }
    for (auto& w : overlays_.set().warnings) warnings_.push_back(w);
    anims_.clear(); streamingChoices_.clear(); animationCache_.clear();
    tracers_.clear();
    looks_.resize(rules.types.size());
    actorRevealTypes_.resize(rules.types.size());
    for (size_t i = 0; i < rules.types.size(); ++i) {
        const auto& id = rules.types[i].id;
        actorRevealTypes_[i] = uint8_t(id.starts_with("city_interior_") || id == "shop_roof");
    }
    loadedTypes_.assign(rules.types.size(), false);
    requestedModels_.assign(rules.types.size(), {}); loadedModels_.assign(rules.types.size(), {});
    if(deferredLoading_)for(size_t t=0;t<rules.types.size();++t)
        requestedModels_[t]=rules.types[t].view["streaming"].getStr("coarseModel");
    if (!deferredLoading_) for (size_t i = 0; i < rules.types.size(); ++i) loadType(uint16_t(i));
    twoD_ = std::make_unique<Rts2DView>();
    if (!twoD_->init(dev, rules, contentBase_, spriteDefaults_, deferredLoading_)) twoD_.reset();
    lights_.init(rules);
    groundFx_.init(dev, rules, read_, contentBase_);
    terrain_.init(dev, rules, read_, contentBase_);
    planets_.init(dev, terrain_.look(), read_, contentBase_);
    return true;
}

void RtsView::shutdown(gfx::Device& dev) {
#ifndef __EMSCRIPTEN__
    if (preparing_.valid()) preparing_.wait();
    preparing_ = {}; prepareType_ = -1; preparingResource_.clear();
#endif
    geometryScene_.shutdown(dev);
    for (auto& [k, m] : models_) if (m) releaseModel(dev, *m);
    textureCache_->shutdown(dev);
    models_.clear(); looks_.clear(); actorRevealTypes_.clear(); loadedTypes_.clear(); anims_.clear(); streamingChoices_.clear(); animationCache_.clear(); tracers_.clear();
    for (Mesh* m : {&projectile_}) { dev.destroyBuffer(m->vb); dev.destroyBuffer(m->ib); *m = Mesh{}; }
    terrain_.shutdown(dev);
    fog_.shutdown();
    fieldView_.shutdown();
    hasTerrain_ = false;
    if (twoD_) twoD_->shutdown(dev);
    twoD_.reset();
    groundFx_.shutdown(dev);
    roads_->shutdown(dev);
    shores_->shutdown(dev);
    paintLayers_.shutdown();
    groundCanvas_.shutdown();
    planetGroundFx_.shutdown(dev);
    planets_.shutdown(dev);
    overlays_.shutdown(dev);
}

bool RtsView::spriteHit(const RTSCamera& cam, const SceneObject& o, const MapGrid& grid, float sx, float sy, float w, float h) const {
    return twoD_ && !o.isProjectile && twoD_->hasSprite(o.type) && twoD_->hit(cam, o, heightAt(grid, o.pos.x, o.pos.z), sx, sy, w, h);
}

float RtsView::selectionRadius(uint16_t type) const {
    const UnitType& t = rules_->types[type];
    if (t.structure) return std::max(t.footprintW, t.footprintH) * 0.62f;
    return float(t.radius.toDouble()) + 0.15f;
}

// heightAt / buildTerrain: RtsTerrain.cpp


// "groundShadow": the image's quad in the drawn model's frame (its rotation without scale / centring), at the object.
void RtsView::addGroundShadow(const TypeLook& L, const EntityAnim& st, float3 pos, float ground, const EntityFxValues* fx) {
    const auto& S = L.shadow;
    // view.entity_fx: the image scales with the model (shadow_scale) and fades with it (opacity * shadow)
    float kx = 1, ky = 1, kz = 1, fade = 1;
    if (fx) {
        kx = fx->v[EntityFxValues::kShadowScale]; ky = fx->v[EntityFxValues::kShadowScale + 1]; kz = fx->v[EntityFxValues::kShadowScale + 2];
        fade = fx->opacity() * fx->v[EntityFxValues::kShadow];
        if (fade <= 0 || kx <= 0 || kz <= 0) return;
    }
    uint32_t rgba = S.rgba;
    if (fade < 1) rgba = (rgba & 0xffffff00u) | uint32_t(std::lround(float(rgba & 0xffu) * fade));
    float3 ax = st.drawn.transformDir({1, 0, 0}), az = st.drawn.transformDir({0, 0, 1});
    ax.y = 0; az.y = 0;
    const float lx = length(ax), lz = length(az);
    if (lx < 1e-6f || lz < 1e-6f) return;
    ax = ax * (1.0f / lx); az = az * (1.0f / lz);
    const float3 c = float3{pos.x, ground + S.lift * ky, pos.z} + ax * (S.ox * kx) + az * (S.oz * kz);
    const float3 u = ax * (S.w * 0.5f * kx), v = az * (S.d * 0.5f * kz);   // image top (v = 0) towards -z of the model
    ViewGroundBatch* b = nullptr;
    for (auto& x : shadowDraws_) if (x.image == S.image) { b = &x; break; }
    if (!b) { shadowDraws_.push_back({}); b = &shadowDraws_.back(); b->image = S.image; b->layer = -1; }
    auto vert = [&](float su, float sv) {
        const float3 p = c + u * su + v * sv;
        const float tu = S.flipU ? (1 - su) * 0.5f : (su + 1) * 0.5f, tv = S.flipV ? (1 - sv) * 0.5f : (sv + 1) * 0.5f;
        b->verts.push_back({p.x, p.y, p.z, tu, tv, rgba});
    };
    vert(-1, -1); vert(1, -1); vert(1, 1);
    vert(-1, -1); vert(1, 1); vert(-1, 1);
}

void RtsView::drawGround(Renderer3D& r, const std::vector<ViewGroundBatch>& batches) {
    if (!dev_) return;
    groundMerged_.clear();
    for (auto& b : shadowDraws_) if (!b.verts.empty()) groundMerged_.push_back(b);
    if (groundMerged_.empty()) { groundCanvas_.draw(r, *dev_, batches, contentBase_); return; }
    groundMerged_.insert(groundMerged_.end(), batches.begin(), batches.end());
    groundCanvas_.draw(r, *dev_, groundMerged_, contentBase_);
}

void RtsView::draw(Renderer3D& r, const RtsScene& input, double time, const std::vector<uint8_t>* vision, const MapGrid& grid) {
    const bool traceView = std::getenv("AIVERSE_FRAME_TRACE") != nullptr;
    const auto viewStart = std::chrono::steady_clock::now();
    // Script spawns, reinforcements and restored entities need not exist at map load time.
    if (!streamingTypes_) {
        // loadType() may compare streaming model paths and check residency.
        // A scene often has thousands of objects but only a handful of types:
        // resolve each type at most once per frame, while still picking up
        // types introduced by spawning/editor edits on the following frame.
        std::vector<uint8_t> typeSeen(loadedTypes_.size(), 0);
        for (const auto& o : input.objects) {
            if (o.isProjectile || o.type >= typeSeen.size() || typeSeen[o.type]) continue;
            typeSeen[o.type] = 1;
            loadType(o.type);
        }
    }
    RtsScene resolved;
    const bool hierarchy=input.entityFx && !input.entityFx->hierarchy.parents.empty();
    if(hierarchy) {
        resolved=input;
        std::map<uint64_t,mat4> bases,effects;
        for(const auto& o:input.objects) {
            if(!o.entity.valid() || o.type>=looks_.size() || o.body!=-1)continue;
            const auto& look=looks_[o.type];EntityFxValues fx;input.entityFx->lookup(o.entity.packed(),fx);
            constexpr float deg=.017453292f;
            auto effect=mat4::translate({0,(look.air?look.flyHeight:0)+fx.v[EntityFxValues::kLift],0})
                *mat4::rotateZ(-fx.v[EntityFxValues::kPitch]*deg)*mat4::rotateX(-fx.v[EntityFxValues::kRoll]*deg)
                *mat4::scale({fx.scale(0),fx.scale(1),fx.scale(2)});
            effects[o.entity.packed()]=effect;
            auto external=input.entityFrames.find(o.entity.packed());
            bases[o.entity.packed()]=external!=input.entityFrames.end()?external->second:
                mat4::translate({o.pos.x,heightAt(grid,o.pos.x,o.pos.z),o.pos.z})
                *mat4::rotateY(std::atan2(-o.facing.z,o.facing.x))*effect;
        }
        auto frames=input.entityFx->hierarchy.resolve(bases,effects);
        for(auto& o:resolved.objects)if(input.entityFx->hierarchy.parents.count(o.entity.packed())) {
            auto it=frames.find(o.entity.packed());
            if(it==frames.end()){o.visible=false;continue;}
            o.parented=true;o.parentFrame=it->second;o.pos=it->second.transformPoint({0,0,0});
            o.facing=it->second.transformPoint({1,0,0})-o.pos;
        }
    }
    const RtsScene& s=hierarchy?resolved:input;
    fx_ = s.entityFx.get();   // view.entity_fx
    if (planets_.active()) { drawPlanets(r, s, time, vision, grid); return; }   // RtsPlanetView.cpp
    animationObstacles_.clear();animationSurfaceFrames_=std::make_shared<std::map<uint64_t,mat4>>();
    bool contacts=fx_&&!fx_->animations.empty();for(const auto& look:looks_)for(const auto& ch:look.models)contacts|=ch.graph&&ch.graph->hasContacts();
    for(size_t oi=0;contacts&&oi<s.objects.size();++oi){const auto& o=s.objects[oi];if(!o.visible||o.isProjectile||o.body!=-1||o.type>=looks_.size())continue;
        const auto& look=looks_[o.type];if(look.models.empty())continue;uint64_t key=o.entity.valid()?o.entity.packed():(uint64_t(1)<<63)|oi;auto state=anims_.find(key);
        size_t choice=state!=anims_.end()&&size_t(state->second.choice)<look.models.size()?size_t(state->second.choice):0;const ModelChoice* ch=&look.models[choice];
        const Stage* stage=nullptr;for(const auto& sg:look.stages){if(o.build<1&&look.construction==Construction::Stages&&sg.build>=0&&sg.build<=o.build&&(!stage||sg.build>stage->build))stage=&sg;if(o.build>=1&&sg.hp>=0&&o.hp<=sg.hp&&(!stage||sg.hp<stage->hp))stage=&sg;}if(stage)ch=&stage->choice;if(!ch->model)continue;
        EntityFxValues values;bool hasFx=fxFor(o,values);float lift=hasFx?values.v[EntityFxValues::kLift]:0;float3 position{o.pos.x,heightAt(grid,o.pos.x,o.pos.z)+(look.air?look.flyHeight:0)+lift,o.pos.z};
        float yaw=state!=anims_.end()?state->second.yaw:std::atan2(-o.facing.z,o.facing.x);float k=look.scale,grow=look.construction==Construction::Grow?std::clamp(o.build,.15f,1.f):1;
        mat4 base;if(look.structure&&look.shape&&!stage)base=mat4::translate(position)*mat4::scale({look.sx*k,k*std::max(look.sx,look.sz)*.75f*grow,look.sz*k});else base=mat4::translate(position)*mat4::rotateY(yaw)*mat4::scale({k,k*(look.structure?grow:1),k});
        if(hasFx){auto tilt=mat4::rotateZ(-values.v[EntityFxValues::kPitch]*.017453292f)*mat4::rotateX(-values.v[EntityFxValues::kRoll]*.017453292f);base=mat4::translate(position)*mat4::rotateY(yaw)*tilt*mat4::rotateY(-yaw)*mat4::translate(-position)*base*mat4::scale({values.scale(0),values.scale(1),values.scale(2)});}
        if(o.parented)base=o.parentFrame*mat4::scale({k,k*(look.structure?grow:1),k});
        if(state!=anims_.end())base=base*state->second.objXf;base=base*ch->fix;const auto& model=*ch->model;
        const auto& matrices=!stage&&state!=anims_.end()&&state->second.world.size()==model.nodes.size()?state->second.world:model.bindWorld;
        for(size_t node=0;node<model.nodes.size()&&node<matrices.size();++node){int mesh=model.nodes[node].mesh;if(mesh<0||size_t(mesh)>=model.meshes.size())continue;auto transform=base*matrices[node];const auto& primitives=model.meshes[size_t(mesh)].primitives;
            for(size_t part=0;part<primitives.size()&&part<256;++part){auto a=primitives[part].boundsMin,b=primitives[part].boundsMax;float3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
                for(float x:{a.x,b.x})for(float y:{a.y,b.y})for(float z:{a.z,b.z}){auto v=transform.transformPoint({x,y,z});lo={std::min(lo.x,v.x),std::min(lo.y,v.y),std::min(lo.z,v.z)};hi={std::max(hi.x,v.x),std::max(hi.y,v.y),std::max(hi.z,v.z)};}
                uint64_t id=key^((node+1)*0x9e3779b97f4a7c15ull)^((part+1)*0xbf58476d1ce4e5b9ull);id^=id>>30;id*=0xbf58476d1ce4e5b9ull;id^=id>>27;id=(id&0x3fffffffffffffffull)+2;
                animationObstacles_.push_back({&o,o.entity,lo,hi,a,b,id,transform});(*animationSurfaceFrames_)[id]=transform;
            }
        }
    }
    const auto afterSetup = std::chrono::steady_clock::now();
    geometryScene_.draw(r);
    const bool tiles = twoD_ && twoD_->hasTiles();
    if (twoD_) twoD_->beginFrame(r.camera(), time, r, &grid, rules_ ? float(rules_->levelHeight.toDouble()) : 1.5f);
    bars_.assign(s.objects.size(), {});
    if (hasTerrain_ && !tiles) terrain_.draw(r, grid, vision, groundFx_.drawsDig());
    if (hasTerrain_ && tiles) terrain_.drawWater(r);
    if (!tiles) groundFx_.draw(r, s, time, vision, grid, *this);
    // A deleted road network must flush the old surface too, otherwise
    // groundAt() and stationary placement caches can retain stale road heights.
    if (roads_->active() || roads_->needsRebuild())
        roads_->draw(r, [this, &grid](float x, float z) { return heightAt(grid, x, z); });
    if (shores_->active()) shores_->draw(r, [this, &grid](float x, float z) { return heightAt(grid, x, z); }, terrain_.look().waterOffset, grid.w, grid.h);
    paintLayers_.draw(r, *dev_, s.paintLayers, grid, *this, vision, time, s.viewer);
    if (tiles || !groundFx_.drawsLinks())
        for (auto& [a, b] : s.roads) r.quadXZ(a, b, 0.5f, rgba(196, 164, 112, 235), 0.04f);

    const auto afterGround = std::chrono::steady_clock::now();
    const float dt = lastTime_ < 0 ? 0.0f : float(std::clamp(time - lastTime_, 0.0, 0.1));
    lastTime_ = time;
    ++frame_;
    viewProj_ = r.viewProj();
    camPos_ = r.cameraPos();
    events_.clear();
    effects_.clear();
    handleEvents(s);
    for (auto& b : shadowDraws_) b.verts.clear();
    // objects the spline roads stand in for (roadNetwork.replaces: tags or type id prefixes) are not drawn
    std::vector<uint8_t> replacedByRoads;
    if (roads_->active() && !roads_->replaces().empty() && rules_) {
        replacedByRoads.assign(rules_->types.size(), 0);
        uint64_t mask = 0;
        for (const auto& name : roads_->replaces()) mask |= rules_->tagMask(name);
        for (size_t t = 0; t < rules_->types.size(); ++t) {
            bool hit = (rules_->types[t].tags & mask) != 0;
            for (const auto& name : roads_->replaces()) if (!hit && rules_->types[t].id.rfind(name, 0) == 0) hit = true;
            replacedByRoads[t] = hit;
        }
    }
    placementCacheHits_ = placementCacheMisses_ = 0;
    groundCacheHits_ = groundCacheMisses_ = 0;
    staticAnimateFastHits_ = 0;
    double objectStateMs = 0.0, objectAnimateMs = 0.0, objectDrawMs = 0.0, objectShadowMs = 0.0;
    size_t tracedObjects = 0;
    for (size_t i = 0; i < s.objects.size(); ++i) {
        const auto objectStart = traceView ? std::chrono::steady_clock::now() : viewStart;
        const SceneObject& o = s.objects[i];
        if (!o.isProjectile && o.type < replacedByRoads.size() && replacedByRoads[o.type]) continue;
        if (o.isProjectile) {
            if (o.visible && o.entity.valid()) drawProjectile(r, o.entity.packed(), o, dt);
            else if (o.visible) r.drawMesh(projectile_, mat4::translate(o.pos), {1, 0.95f, 0.8f, 1}, kDrawUnlit);
            continue;
        }
        if (twoD_ && twoD_->hasSprite(o.type)) { // 2D games: sprites instead of models
            float g = heightAt(grid, o.pos.x, o.pos.z);
            EntityFxValues sfx;
            twoD_->addObject(o, g, fxFor(o, sfx) ? &sfx : nullptr);
            bars_[i] = twoD_->barPoint(o, g);
            continue;   // selection rings: overlays
        }
        if (o.type >= looks_.size() || !typeLoaded(o.type)) continue;
        // editor placements have no entity: key them by position in the list
        const uint64_t key = o.entity.valid() ? o.entity.packed() : (1ull << 63) | uint64_t(i);
        EntityAnim& st = stateFor(key, o);
        st.last = o;
        if (traceView) objectStateMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - objectStart).count();
        if (!o.visible) {
            if (o.blip) {   // radar contact: a filled dot with a ring where the unit is, no model (the viewer doesn't know its type)
                const float gy = groundAt(grid, o.pos.x, o.pos.z) + 0.06f;
                constexpr int kSeg = 12;
                const float rd = 0.32f;
                for (int k = 0; k < kSeg; ++k) {
                    const float a0 = 6.2831853f * float(k) / kSeg, a1 = 6.2831853f * float(k + 1) / kSeg;
                    r.triangle({o.pos.x, gy, o.pos.z}, {o.pos.x + rd * std::cos(a0), gy, o.pos.z + rd * std::sin(a0)},
                               {o.pos.x + rd * std::cos(a1), gy, o.pos.z + rd * std::sin(a1)}, rgba(255, 60, 40, 220));
                }
                r.circleXZ({o.pos.x, gy, o.pos.z}, 0.6f, rgba(255, 70, 50, 150), 16, gy);
            }
            continue;
        }
        const TypeLook& L = looks_[o.type];
        const bool roadSurface = !rules_->types[o.type].structure && !L.air;
        static const bool cacheGround = [] {
            const char* setting = std::getenv("AIVERSE_RTS_GROUND_CACHE");
            return setting && std::strcmp(setting, "1") == 0;
        }();
        const bool groundEligible = cacheGround && !o.moving && !o.parented &&
            (L.structure || rules_->types[o.type].speed <= Fixed{});
        auto& cachedGround = st.ground;
        const uint64_t roadsGeneration = roads_ ? roads_->surfaceGeneration() : 0;
        const bool reuseGround = groundEligible && cachedGround.valid &&
            cachedGround.grid == &grid && cachedGround.terrainGeneration == terrainGeneration_ &&
            cachedGround.roadGeneration == roadsGeneration &&
            cachedGround.type == o.type && cachedGround.roadSurface == roadSurface &&
            cachedGround.pos.x == o.pos.x && cachedGround.pos.z == o.pos.z;
        const float ground = reuseGround ? cachedGround.height :
            (roadSurface ? groundAt(grid, o.pos.x, o.pos.z) : heightAt(grid, o.pos.x, o.pos.z));
        if (groundEligible) {
            if (reuseGround) ++groundCacheHits_;
            else {
                ++groundCacheMisses_;
                cachedGround.valid = true;
                cachedGround.grid = &grid;
                cachedGround.terrainGeneration = terrainGeneration_;
                cachedGround.roadGeneration = roadsGeneration;
                cachedGround.type = o.type;
                cachedGround.roadSurface = roadSurface;
                cachedGround.pos = o.pos;
                cachedGround.height = ground;
            }
        } else cachedGround.valid = false;
        const auto beforeAnimate = traceView ? std::chrono::steady_clock::now() : viewStart;
        animate(st, o, dt, &grid);
        const auto beforeDraw = traceView ? std::chrono::steady_clock::now() : viewStart;
        drawObject(r, st, o, ground, 1.0f);
        // Reuse the draw end timestamp as the shadow start timestamp.
        // Tracing must not issue another clock read for every scene object.
        const auto afterDraw = traceView ? std::chrono::steady_clock::now() : viewStart;
        if (traceView) {
            objectAnimateMs += std::chrono::duration<double, std::milli>(beforeDraw - beforeAnimate).count();
            objectDrawMs += std::chrono::duration<double, std::milli>(afterDraw - beforeDraw).count();
            ++tracedObjects;
        }
        // The model FX was handled by drawObject(). Query FX again only
        // when an additional ground or flyer shadow actually needs it.
        const bool groundShadow = !L.shadow.image.empty();
        if (groundShadow || L.flyer) {
            EntityFxValues fx;
            const bool hasFx = fxFor(o, fx);
            if (groundShadow) {
                addGroundShadow(L, st, o.pos, ground, hasFx ? &fx : nullptr);
            } else { // flyer blob, unless the type supplies an authored shadow
                const float k = hasFx ? fx.v[EntityFxValues::kShadowScale] : 1.0f;
                const float a = hasFx ? std::clamp(fx.opacity() * fx.v[EntityFxValues::kShadow], 0.0f, 1.0f) : 1.0f;
                if (a > 0 && k > 0) r.circleXZ({o.pos.x, ground, o.pos.z}, 0.35f * k, rgba(0, 0, 0, uint8_t(std::lround(90 * a))), 16, ground + 0.03f);
            }
            // No clock call for the common case with no additional shadow.
            if (traceView) objectShadowMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - afterDraw).count();
        }
    }
    const auto afterObjects = std::chrono::steady_clock::now();
    drawTracers(r, dt, &grid);
    // corpses (entity already removed) play their death and fade; stale states are dropped
    for (auto it = anims_.begin(); it != anims_.end();) {
        EntityAnim& st = it->second;
        if (st.seen == frame_ && !st.dead) { ++it; continue; }
        if ((!st.dead || !st.initialized) && !fxCorpse(it->first, st)) {
            it = st.seen + 2 < frame_ ? anims_.erase(it) : std::next(it);
            continue;
        }
        float adt = dt;
        st.deathAge = corpseAge(it->first, st.deathAge, dt, adt);
        if (st.type >= looks_.size() || st.deathAge > looks_[st.type].corpse) { it = anims_.erase(it); continue; }
        const TypeLook& L = looks_[st.type];
        if (st.seen != frame_ && st.last.visible) {
            SceneObject o = st.last;
            o.selected = o.hovered = false;
            animate(st, o, adt, &grid);
            const float g = heightAt(grid, o.pos.x, o.pos.z), fade = L.dissolveDeath ? 1.0f : L.corpseAlpha(st.deathAge);
            drawObject(r, st, o, g, fade);
            if (!L.shadow.image.empty()) {   // a corpse keeps its footprint image while it lies there (faded like the model)
                EntityFxValues fx;
                if (!fxFor(o, fx)) fx = {};
                fx.v[EntityFxValues::kShadow] *= fade;
                addGroundShadow(L, st, o.pos, g, &fx);
            }
        }
        ++it;
    }
    if (twoD_) {
        twoD_->endFrame(grid);
        twoD_->submit(r, grid, rules_ ? float(rules_->levelHeight.toDouble()) : 1.5f);
    }

    const auto afterAnimations = std::chrono::steady_clock::now();
    // grid fields (RtsFieldView): overlays under the fog sheet
    if (!s.fields.empty() && s.fieldDefs) {
        if (fieldDefsSeen_ != s.fieldDefs || fieldDefsCount_ != s.fieldDefs->size()) {
            fieldView_.setFields(*s.fieldDefs);
            fieldDefsSeen_ = s.fieldDefs; fieldDefsCount_ = s.fieldDefs->size();
        }
        const bool lowFields = r.materialQuality() == MaterialQuality::Low || r.shadowQuality() == ShadowQuality::Off;
        fieldView_.draw(r, s.fields, vision, grid, *this, time, lowFields, s.viewer);
    }
    // fog of war (RtsFog): faded, blurred field; a sheet draped over the ground, or the terrain shader
    // on heightfield maps (a sheet would cut the relief)
    if (vision && !vision->empty()) {
        if (fogMeshDirty_ && dev_) { fog_.build(*dev_, grid.w, grid.h, [&](float x, float z) { return heightAt(grid, x, z); }); fogMeshDirty_ = false; }
        const bool changed = fog_.update(*vision, grid.w, grid.h, time);
        const bool low = r.shadowQuality() == ShadowQuality::Off || r.shadowQuality() == ShadowQuality::Low;
        if (hasTerrain_ && !tiles && terrain_.shaderFog()) terrain_.setFog(&fog_.field(), fog_.look(), changed);
        else fog_.draw(r, time, low);
    }
    lights_.draw(r, s, time, vision, grid, *this);
    // selection / hover rings, move markers, placement footprint and game overlays (after the fog sheet)
    overlays_.drawWorld(r, s, time, vision, grid, *this);
    if (traceView) {
        static unsigned samples = 0;
        static double totals[5] = {};
        static double objectTotals[4] = {};
        static size_t objectCount = 0;
        static uint64_t placementHits = 0, placementMisses = 0;
        static uint64_t groundHits = 0, groundMisses = 0;
        static uint64_t staticFastHits = 0;
        const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b-a).count(); };
        totals[0] += ms(viewStart, afterSetup);
        totals[1] += ms(afterSetup, afterGround);
        totals[2] += ms(afterGround, afterObjects);
        totals[3] += ms(afterObjects, afterAnimations);
        totals[4] += ms(afterAnimations, std::chrono::steady_clock::now());
        objectTotals[0] += objectStateMs;
        objectTotals[1] += objectAnimateMs;
        objectTotals[2] += objectDrawMs;
        objectTotals[3] += objectShadowMs;
        objectCount += tracedObjects;
        placementHits += placementCacheHits_;
        placementMisses += placementCacheMisses_;
        groundHits += groundCacheHits_;
        groundMisses += groundCacheMisses_;
        staticFastHits += staticAnimateFastHits_;
        if (++samples == 120) {
            std::fprintf(stderr,
                "[view-stages] avg/120 setup=%.3f ground=%.3f objects=%.3f anim-cleanup=%.3f overlays=%.3f ms\n",
                totals[0]/samples, totals[1]/samples, totals[2]/samples,
                totals[3]/samples, totals[4]/samples);
            std::fprintf(stderr, "[view-objects] avg/120 state=%.3f animate=%.3f drawObject=%.3f shadow=%.3f ms objects/frame=%.1f\n",
                         objectTotals[0]/samples, objectTotals[1]/samples, objectTotals[2]/samples,
                         objectTotals[3]/samples, double(objectCount)/samples);
            if (placementHits + placementMisses)
                std::fprintf(stderr, "[rts-placement] avg/120 hits=%.1f misses=%.1f hit-rate=%.1f%%\n",
                    double(placementHits)/samples, double(placementMisses)/samples,
                    100.0 * double(placementHits)/double(placementHits+placementMisses));
            if (groundHits + groundMisses)
                std::fprintf(stderr,
                    "[rts-ground-cache] avg/120 hits=%.1f misses=%.1f hit-rate=%.1f%%\n",
                    double(groundHits)/samples, double(groundMisses)/samples,
                    100.0 * double(groundHits)/double(groundHits+groundMisses));
            if (staticFastHits)
                std::fprintf(stderr,
                    "[rts-static-animate] avg/120 fast-path=%.1f objects/frame\n",
                    double(staticFastHits) / samples);
            placementHits = placementMisses = groundHits = groundMisses = staticFastHits = 0;
            samples = 0;
            for (double& v : totals) v = 0.0;
            for (double& v : objectTotals) v = 0.0;
            objectCount = 0;
        }
    }
}

void RtsView::drawOverlays(const RTSCamera& cam, const RtsScene& s, float w, float h, float ox, float oy, void* drawList) {
    // bars, garrison counts, labels, damage numbers: RtsOverlays (built-in overlays reproduce the old look)
    overlays_.drawScreen(cam, s, w, h, ox, oy, drawList, *this);
}

bool RtsView::overlayTop(const RTSCamera& cam, const SceneObject& o, size_t oi, float w, float h, float2& sp) const {
    if (!rules_ || o.type >= rules_->types.size() || o.type >= looks_.size()) return false;
    const UnitType& t = rules_->types[o.type];
    if (planets_.active()) // anchors computed by drawPlanets (view world; hidden behind the planet / far away)
        return oi < bars_.size() && oi < barOk_.size() && barOk_[oi] && cam.worldToScreen(bars_[oi], w, h, sp);
    if (twoD_ && twoD_->hasSprite(o.type) && oi < bars_.size()) {
        if (!cam.worldToScreen(bars_[oi], w, h, sp)) return false;
        sp.y -= 4;
        return true;
    }
    float top = t.structure ? 1.6f * looks_[o.type].scale * std::max(1.0f, std::max(t.footprintW, t.footprintH) * 0.6f) : 1.2f * looks_[o.type].scale;
    if (looks_[o.type].air) top += 2.4f;
    return cam.worldToScreen({o.pos.x, top, o.pos.z}, w, h, sp);
}

void sceneFromSim(Simulation& sim, RtsScene& out, float alpha, uint8_t viewer) {
    World& w = sim.world();
    auto* m = dynamic_cast<RtsModule*>(&sim.module());
    if (!m) return;
    const Rules& R = m->rules();
    GameState& gs = m->state(sim);
    out.objects.clear();
    out.entityFrames.clear();
    out.roads.clear();
    out.events = sim.events();
    out.tick = sim.tick();
    if (++out.facingFrame % 256 == 0)   // forget the facing of units gone from the scene
        for (auto it = out.facingTracks.begin(); it != out.facingTracks.end();)
            it = out.facingFrame - it->second.seen > 256 ? out.facingTracks.erase(it) : std::next(it);
    out.viewer = viewer;
    out.lanes = R.lanes.enabled ? &gs.lanes : nullptr;
    out.laneSignature = gs.laneSignature;
    out.seconds = double(sim.tick()) / double(std::max(sim.tickRate(), 1u));
    out.fields.clear();
    out.fieldDefs = nullptr;
    if (GridFields* gf = m->gridFields(); gf && gf->active())
        if (const GridFieldStore* fs = sim.world().resource<GridFieldStore>()) {
            out.fieldDefs = &gf->defs();
            for (size_t i = 0; i < fs->fields.size(); ++i) {
                const auto& f = fs->fields[i];
                out.fields.push_back({int(i), f.w, f.h, gf->defs()[i].resolution, f.layers, f.v.data(), f.version});
            }
        }
    if (const Surface* surf = gs.surface.get()) { sceneFromSphere(sim, *m, *surf, out, alpha, viewer); return; }   // RtsPlanetView.cpp
    const Driving* drivingCars = m->driving() && m->driving()->active() ? m->driving() : nullptr;
    // The view configuration is shared by every entity of a type. Avoid
    // repeating JSON object lookups for thousands of identical scenery types.
    std::vector<uint8_t> territoryTeamTypes(R.types.size(), 0);
    for (size_t type = 0; type < R.types.size(); ++type)
        territoryTeamTypes[type] = uint8_t(R.types[type].view.getBool("teamFromTerritory", false));
    auto* arsenal = m->arsenal();
    auto* topDown = m->topDown();
    // radar / sonar contacts of the viewer outside its sight: drawn as blips (RtsSensors.h)
    std::vector<Entity> blips;
    if (Sensors* sn = m->sensors(); sn && viewer != 0) { blips = sn->blips(sim, viewer); std::sort(blips.begin(), blips.end(), [](Entity a, Entity b) { return a.index < b.index; }); }
    w.each<Unit, Owner, Transform>([&](Entity e, Unit& u, Owner& o, Transform& t) {
        if (u.flags & kUnitInCargo) return;   // inside a transport (agent-mech)
        SceneObject so;
        so.entity = e; so.type = u.type; so.owner = o.player;
        if (arsenal) if (auto* hand = arsenal->hand(sim, o.player))
            if (hand->hero == e && hand->current >= 0 && size_t(hand->current) < arsenal->rules().weapons.size()) {
                so.heldWeapon = arsenal->rules().weapons[hand->current].weapon;
                if (topDown) if (auto* pilots = topDown->store(sim))
                    if (o.player < pilots->pilots.size())
                        so.weaponAim = pilots->pilots[o.player].input.aim || pilots->pilots[o.player].input.fire;
            }
        float3 a = toF(t.prevPos), b = toF(t.pos);
        so.pos = lerp(a, b, alpha);
        so.facing = toF(u.facing);
        {   // interpolate the facing between the last two ticks (t.prevPos -> t.pos has the same alpha)
            auto [it, fresh] = out.facingTracks.try_emplace(e.packed());
            RtsScene::FacingTrack& ft = it->second;
            if (fresh || (t.prevPos == t.pos && ft.tick != sim.tick())) { ft.prev = ft.cur = so.facing; }   // new, or teleported / standing
            else if (ft.tick != sim.tick()) { ft.prev = ft.cur; ft.cur = so.facing; }
            ft.tick = sim.tick(); ft.seen = out.facingFrame;
            // Stationary structures and unchanged facings already have the
            // correct direction. Avoid interpolation, sqrt and division for
            // the common static RTS path, without changing turning behaviour.
            if (ft.prev.x != ft.cur.x || ft.prev.y != ft.cur.y || ft.prev.z != ft.cur.z) {
                const float3 f{ft.prev.x + (ft.cur.x - ft.prev.x) * alpha, ft.prev.y + (ft.cur.y - ft.prev.y) * alpha, ft.prev.z + (ft.cur.z - ft.prev.z) * alpha};
                const float fl = std::sqrt(f.x * f.x + f.z * f.z);
                if (fl > 1e-3f && ft.prev.x * ft.cur.x + ft.prev.z * ft.cur.z > 0) so.facing = {f.x / fl, f.y, f.z / fl};   // a flip over 90 degrees snaps
            }
        }
        const UnitType& ut = R.types[u.type];
        if (territoryTeamTypes[u.type]) {
            const int x = int(t.pos.x.floorInt()), z = int(t.pos.z.floorInt());
            if (gs.grid.inside(x, z)) {
                const size_t cell = size_t(gs.grid.index(x, z));
                if (cell < gs.grid.territory.size()) so.teamOwner = gs.grid.territory[cell];
            }
        }
        so.hp = ut.maxHp.raw > 0 ? float((u.hp / ut.maxHp).toDouble()) : 1.0f;
        if (!gs.routes.walks.empty()) so.fade = float(m->routeAlpha(sim, e).toDouble());   // route end fade
        if (auto* c = w.tryGet<Construction>(e)) so.build = float(c->progress.toDouble());
        so.unpowered = (u.flags & kUnitDisabled) || (o.player < gs.players.size() && gs.players[o.player].productionMult < Fixed::fromInt(1));
        if (auto* g = w.tryGet<Garrison>(e)) so.garrison = g->count;
        if (auto* tp = w.tryGet<Transport>(e); tp && !tp->cargo.empty()) so.garrison = int(tp->cargo.size());   // passengers
        if (auto* c = w.tryGet<Cargo>(e)) { so.carry = float(c->amount.toDouble()); so.carryRes = c->res; }
        if (auto* od = w.tryGet<Orders>(e); od && !od->queue.empty()) so.order = uint8_t(od->queue.front().type) + 1;
        if (auto* mb = w.tryGet<Mobile>(e)) so.moving = mb->moving;
        if (drivingCars) if (const DrivingCar* car = drivingCars->car(sim, e)) so.steer = float(car->st.steer.toDouble()) * 6.2831853f;
        if (auto* pr = w.tryGet<Production>(e)) so.producing = !pr->queue.empty();
        if (auto* cb = w.tryGet<Combat>(e); cb && cb->target.valid() && w.alive(cb->target)) {
            if (auto* tt = w.tryGet<Transform>(cb->target)) { so.hasTarget = true; so.target = lerp(toF(tt->prevPos), toF(tt->pos), alpha); }
        }
        if (so.moving) so.action = kActMove;
        else if (so.hasTarget) so.action = kActAttack;
        else if (so.order) {
            OrderType ot = OrderType(so.order - 1);
            if (ot == OrderType::Gather || ot == OrderType::Build || ot == OrderType::Repair) so.action = kActWork;
        }
        if (viewer != 0 && o.player != viewer && !sim.allied(o.player, viewer) && !m->canSee(sim, e, viewer)) so.visible = false;   // cloaked, not detected
        else if (viewer != 0 && o.player != viewer && !sim.allied(o.player, viewer)) {
            // enemies only when visible; neutral structures (resources) once explored
            int x = int(t.pos.x.floorInt()), y = int(t.pos.z.floorInt());
            uint8_t v = gs.grid.inside(x, y) ? gs.vision[viewer][size_t(gs.grid.index(x, y))] : 0;
            so.visible = ut.structure && o.player == 0 ? v >= 1 : v == 2;
        }
        if (so.visible && ut.trap >= 0 && viewer != 0 && o.player != viewer && !sim.allied(o.player, viewer) && w.has<TrapState>(e) &&
            (m->statusFlags(sim, e) & kStInvisible) && !(m->statusFlags(sim, e) & kStRevealed)) so.visible = false;   // hidden trap
        if (!so.visible && !blips.empty()) {
            auto it = std::lower_bound(blips.begin(), blips.end(), e, [](Entity a, Entity b) { return a.index < b.index; });
            so.blip = it != blips.end() && *it == e;
        }
        out.objects.push_back(std::move(so));
    });
    // bodies of native modules (kHookScene): drawn as unit types
    if (m->native().has(kHookScene)) {
        std::vector<NativeModule::Body> bodies;
        m->native().sceneBodies(*m, sim, bodies);
        for (auto& b : bodies) {
            if (b.type >= R.types.size()) continue;
            SceneObject so;
            so.type = b.type; so.owner = 0;
            so.pos = toF(b.pos);
            const float3 f = toF(b.facing);
            const float fl = std::sqrt(f.x * f.x + f.z * f.z);
            if (fl > 1e-4f) so.facing = {f.x / fl, 0, f.z / fl};
            so.moving = b.moving && fl > 1e-4f;
            if (viewer != 0) {
                int x = int(b.pos.x.floorInt()), y = int(b.pos.z.floorInt());
                so.visible = gs.grid.inside(x, y) && gs.vision[viewer][size_t(gs.grid.index(x, y))] == 2;
            }
            out.objects.push_back(std::move(so));
        }
    }
    w.each<Projectile, Transform>([&](Entity e, Projectile& pr, Transform& t) {
        SceneObject so;
        so.entity = e; so.isProjectile = true; so.source = pr.source;
        if (auto* su = w.tryGet<Unit>(pr.source)) { so.shooterType = su->type; if (pr.sourceType == su->type) so.shotWeapon = pr.weapon; } // picks the projectile's light and size
        so.pos = lerp(toF(t.prevPos), toF(t.pos), alpha);
        if (viewer != 0) {
            int x = int(t.pos.x.floorInt()), y = int(t.pos.z.floorInt());
            so.visible = gs.grid.inside(x, y) && gs.vision[viewer][size_t(gs.grid.index(x, y))] == 2;
        }
        out.objects.push_back(std::move(so));
    });
    for (size_t i = 0; i < gs.roads.nodes.size(); ++i)
        for (uint16_t j : gs.roads.adj[i]) {
            if (j <= i) continue;
            auto* ta = w.tryGet<Transform>(gs.roads.nodes[i]);
            auto* tb = w.tryGet<Transform>(gs.roads.nodes[j]);
            if (ta && tb) out.roads.push_back({toF(ta->pos), toF(tb->pos)});
        }
}

} // namespace aiverse::game
