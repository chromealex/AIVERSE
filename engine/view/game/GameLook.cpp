// "view" blocks of types: model loading (with shape fallback), placement fix-up, material
// overrides, clip mapping and procedural animation presets. Problems become warnings with the
// type id, never errors: a broken model must not stop a game from starting.
#include "GameView.h"
#include "engine/view/ModelPoseRetarget.h"
#include "engine/view/ModelPublicationEpoch.h"
#include "GameModelStreaming.h"
#include "engine/view/AppearanceAuthoringService.h"
#include "engine/view/AppearanceChoiceAuthoring.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

namespace aiverse::game {

using namespace render;

namespace {
constexpr float kDeg = 3.14159265358979f / 180.0f;
// Keep one appearance of each initial model. Remaining material/body variants are appended later.
std::vector<size_t> initialModelChoices(const Json& view) {
    const auto& choices = view["model"].items();
    const auto limit = view["streaming"].getInt("initialModels", 0);
    std::vector<size_t> result;
    std::vector<std::string> paths;
    for (size_t i = 0; i < choices.size(); ++i) {
        const auto path = choices[i].isString() ? choices[i].str() : choices[i].getStr("file");
        if (limit > 0) {
            if (paths.size() >= size_t(limit) || std::find(paths.begin(), paths.end(), path) != paths.end()) continue;
            paths.push_back(path);
        }
        result.push_back(i);
    }
    return result;
}

float num(const Json& j, float def) { return j.isNumber() ? float(j.toDouble(def)) : def; }
float3 vec3(const Json& j, float3 def) {
    return j.size() >= 3 ? float3{num(j[0], def.x), num(j[1], def.y), num(j[2], def.z)} : def;
}
std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
float srgb(float c) { return c * c; } // the engine's ~sRGB -> linear convention (lit shader squares colours)

uint32_t stateBit(const std::string& n) {
    if (n == "idle") return kProcIdle;
    if (n == "moving" || n == "move") return kProcMoving;
    if (n == "attacking" || n == "attack") return kProcAttacking;
    if (n == "gathering" || n == "gather") return kProcGathering;
    if (n == "building" || n == "build") return kProcBuilding;
    if (n == "carrying" || n == "carry") return kProcCarrying;
    if (n == "constructing" || n == "construct") return kProcConstructing;
    if (n == "producing" || n == "produce" || n == "work") return kProcProducing;
    if (n == "dead" || n == "die") return kProcDead;
    if (n == "air" || n == "flying") return kProcAir;
    return 0;
}
int eventIndex(const std::string& n) {
    if (n == "attack") return kProcEvAttack;
    if (n == "hit") return kProcEvHit;
    if (n == "spawn") return kProcEvSpawn;
    if (n == "death" || n == "die") return kProcEvDeath;
    return kProcEvNone;
}

// clip-name candidates for automatic mapping when a model has no "animations" block
const char* const kAuto[GameView::kAnimCount][6] = {
    {"idle", "stand", "breath", nullptr}, {"walk", "run", "move", "locomotion", nullptr},
    {"attack", "shoot", "fire", "slash", "punch", nullptr}, {"gather", "harvest", "mine", "chop", "work", nullptr},
    {"build", "hammer", "repair", "work", nullptr}, {"carry", nullptr},
    {"death", "die", "dead", nullptr}, {"spawn", "appear", "birth", nullptr},
    {"hit", "hurt", "damage", nullptr}, {"working", "produce", "work", nullptr},
    {"construct", nullptr}, {"cast", "spell", nullptr},
};

} // namespace

Json GameView::resolveMaterial(const UnitType& t, const Json& ref, int depth) {
    return view::AppearanceAuthoringService::resolveMaterial(
        materialLib_,ref,[this,&t](const std::string& message) {
            warn(t,message);
        },depth);
}

void GameView::applyMaterial(gfx::Device& dev, const UnitType& t,
                            Model& model, Material& material,const Json& ref) {
    // The host decides source reader, GPU texture insertion and warning
    // scope; generic authoring resolves all material fields consistently
    // for RTS/FPS/editor/native projects without their genre rules.
    view::AppearanceAuthoringService::applyMaterial(
        material,materialLib_,ref,
        [&](const std::string& path) {
            return loadTexture(dev,t,model,path);
        },
        [this,&t](const std::string& message) {warn(t,message);});
}

const char* GameView::animName(int k) {
    static const char* names[kAnimCount] = {"idle", "move", "attack", "gather", "build", "carry", "die", "spawn", "hit", "work", "construct", "cast"};
    return k >= 0 && k < kAnimCount ? names[k] : "?";
}

void GameView::warn(const UnitType& t, const std::string& msg) {
    std::string w = "type '" + t.id + "': " + msg;
    if (std::find(warnings_.begin(), warnings_.end(), w) != warnings_.end()) return;
    std::fprintf(stderr, "view: %s\n", w.c_str());
    warnings_.push_back(std::move(w));
}

const Model* GameView::modelOf(uint16_t type, size_t choice) const {
    return type < looks_.size() && choice < looks_[type].models.size() ? looks_[type].models[choice].model : nullptr;
}

GameView::TypeLook::GroundShadow GameView::parseGroundShadow(const Json& gs) {
    TypeLook::GroundShadow S;
    if (!gs.isObject() || !gs["image"].isString()) return S;
    S.image = gs["image"].str();
    S.w = num(gs["size"][0], 1); S.d = num(gs["size"][1], S.w);
    S.ox = num(gs["offset"][0], 0); S.oz = num(gs["offset"][1], 0);
    S.lift = num(gs["lift"], 0.05f);
    const Json& sc = gs["color"];
    auto b = [&](int i, float def) { return uint32_t(std::lround(std::clamp(num(sc[i], def), 0.0f, 1.0f) * 255)); };
    S.rgba = sc.size() >= 3 ? (b(0, 0) << 24) | (b(1, 0) << 16) | (b(2, 0) << 8) | b(3, 1) : 0x0000008cu;
    S.flipU = gs["flip"][0].boolean(false); S.flipV = gs["flip"][1].boolean(false);
    if (!(S.w > 0 && S.d > 0) || !std::isfinite(S.w) || !std::isfinite(S.d)) S.image.clear();
    return S;
}

void GameView::buildLook(gfx::Device& dev, const UnitType& t, TypeLook& L) {
    const Json& v = t.view;
    L.structure = t.structure;
    L.air = t.layer & kLayerAir;
    // a flyer gets a shadow blob to read its height by; a prop that only uses the air layer to stay out of the way
    // (it never moves: a stop marker, a support) gets none - it showed as a black ring on the ground
    L.flyer = L.air && t.speed.raw > 0;
    L.scale = num(v["scale"], 1);
    const Json& c = v["color"];
    L.tint = c.size() >= 3 ? float4{srgb(num(c[0], 1)), srgb(num(c[1], 1)), srgb(num(c[2], 1)), 1} : float4{1, 1, 1, 1};
    L.sx = t.structure ? float(t.footprintW) : 1.0f;
    L.sz = t.structure ? float(t.footprintH) : 1.0f;
    L.corpse = num(v["corpse"], t.structure ? 3.0f : 2.5f);
    L.corpseFade = std::max(0.0f, num(v["corpseFade"], 0.6f));
    L.turnSpeed = num(v["turnSpeed"], t.structure ? 0.0f : 10.0f);
    L.tracer = v.getBool("tracer", false);
    if (const Json& ps = v["projectileScale"]; ps.isNumber()) L.projectileScaleDefault = std::max(0.0f, num(ps, 1));
    else if (ps.isObject()) {   // {"default": 0.3, "<weapon id>": 1}: bullets small, rockets big
        L.projectileScaleDefault = std::max(0.0f, num(ps["default"], 1));
        L.projectileScale.assign(t.weapons.size(), L.projectileScaleDefault);
        for (const auto& [wid, x] : ps.members()) {
            if (wid == "default") continue;
            bool found = false;
            for (size_t i = 0; i < t.weapons.size(); ++i) if (t.weapons[i].id == wid) { L.projectileScale[i] = std::max(0.0f, num(x, 1)); found = true; }
            if (!found) warn(t, "projectileScale: the type has no weapon '" + wid + "'");
        }
    }
    L.flyHeight = num(v["flyHeight"], 2.4f);
    // "groundShadow": {"image", "size": [w, d] (world units), "offset": [x, z] (model frame, world units), "color": [r, g,
    // b, a] (0..1, multiplies the image), "lift", "flip": [u, v]}: a flat image on the ground under the model, turning
    // with it (image u along the model's +x, its top towards the model's -z unless flipped). docs/scripting.md
    L.shadow = parseGroundShadow(v["groundShadow"]);
    L.unitSpeed = float(t.speed.toDouble());
    // "model": "a.glb" | ["a.glb", "b.vox"] (one picked per object) | entries {"file": "b.vox", ...}
    // whose other keys replace the view keys for that model (different nodes, materials, height)
    std::vector<std::pair<std::string, Json>> entries;
    // A choice needs appearance defaults, not a copy of the entire variant list.
    // Large character palettes otherwise duplicate O(choices squared) JSON data.
    Json choiceDefaults = v;
    choiceDefaults.erase("model");
    if (v["model"].isString()) entries.emplace_back(v["model"].str(), v);
    const auto initialChoices = initialModelChoices(v);
    for (size_t index = 0; index < v["model"].items().size(); ++index) {
        if (deferredLoading_ && deferAppearanceVariants_ && std::find(initialChoices.begin(), initialChoices.end(), index) == initialChoices.end()) {
            L.pendingVariants.push_back(index); continue;
        }
        const auto& p = v["model"][index];
        if (p.isString()) entries.emplace_back(p.str(), choiceDefaults);
        else if (p.isObject()) {
            Json merged = choiceDefaults;
            for (const auto& [k, val] : p.members()) if (k != "file") merged.set(k, val);
            if (p.getStr("file").empty()) warn(t, "model entry without \"file\"");
            else entries.emplace_back(p.getStr("file"), std::move(merged));
        }
    }
    std::vector<Json> views;   // the view block of each loaded model
    for (const auto& [p, view] : entries) {
        ModelChoice ch;
        if (loadChoice(dev, t, view, p, ch)) { L.models.push_back(std::move(ch)); views.push_back(view); }
    }
    L.shape = L.models.empty();
    if (L.shape) {
        std::string shape = v.getStr("shape", t.structure ? (t.resourceNode >= 0 ? "mine" : "structure") : "generic");
        if (!resources_.contains("shape:" + shape)) {
            warn(t, "unknown shape '" + shape + "'");
            shape = t.structure ? "structure" : "generic";
        }
        ModelChoice ch;
        ch.model = resources_.find("shape:" + shape);
        ch.materials = ch.model->materials;
        configureChoice(dev, t, v, ch, true);
        L.models.push_back(std::move(ch));
    }
    auto procedural = [&](ModelChoice& ch, const Json& vv) {
        if (!vv.has("procedural")) defaultProcedural(t, L, ch);
        else {
            parseProcedural(t, vv["procedural"], ch);
            if (vv.getBool("proceduralDefaults")) defaultProcedural(t, L, ch);
        }
    };
    for (size_t i = 0; i < L.models.size(); ++i) {
        const Json& vv = i < views.size() ? views[i] : v;
        procedural(L.models[i], vv);
        configureExtras(dev, t, vv, L.models[i]);
        finishChoice(t, L.models[i]);
    }
    // ---- LOD: farther models share the clips (by name) and nodes (by name) of models[0]
    if (!L.shape) {
        for (const auto& lj : v["lod"].items()) {
            ModelChoice lc;
            if (!loadChoice(dev, t, v, lj.getStr("model"), lc)) continue;
            lc.lodDistance = num(lj["distance"], 30);
            const Model& base = *L.models[0].model;
            for (const auto& c0 : base.clips) lc.clipMap.push_back(lc.model->findClip(c0.name));
            // Every genre uses the same name-based authoring helper;
            // the resulting explicit source-to-visual map is compiled once.
            auto retarget=std::make_shared<view::ModelPoseRetarget>(
                view::makeNamedModelPoseRetarget(base,*lc.model));
            lc.nodeMap=retarget->sourceNodeForVisual; // historical fallback only
            if(auto plan=view::compileModelPoseRetarget(
                   base,*lc.model,*retarget,
                   L.models[0].modelGeneration,lc.modelGeneration)) {
                lc.poseRetarget=std::move(retarget);
                lc.compiledPoseRetarget=std::move(plan);
            }else {
                // Only author-time failure diagnostics, never per crowd draw.
                // Keep the older, uncompiled LOD behavior for compatibility
                // until the editor's model visual parity suite is verified.
                const auto report=view::describeModelPoseRetarget(
                    base,*lc.model,*retarget,
                    L.models[0].modelGeneration,lc.modelGeneration);
                warn(t,"LOD '" + lc.model->path + "' retarget rejected: " +
                    view::modelPoseRetargetIssueName(report.issue) +
                    " (visual node " + std::to_string(report.targetNode) +
                    ", matched " + std::to_string(report.mappedNodes) +
                    ", unbound " + std::to_string(report.unboundNodes) + ")");
            }
            for (int k = 0; k < kAnimCount; ++k) { lc.clips[k] = L.models[0].clips[k]; lc.hasClip[k] = L.models[0].hasClip[k]; }
            procedural(lc, v);
            configureExtras(dev, t, v, lc);
            lc.attach.clear(); lc.fx.clear(); lc.legs.clear(); lc.additive.clear(); // far away: no sockets, effects, IK
            finishChoice(t, lc);
            L.lods.push_back(std::move(lc));
        }
        std::sort(L.lods.begin(), L.lods.end(), [](const ModelChoice& x, const ModelChoice& y) { return x.lodDistance < y.lodDistance; });
    }
    // ---- stages: construction progress or damage swap the model / material variant
    bool buildStages = false;
    for (const auto& sj : v["stages"].items()) {
        Stage st;
        st.build = sj.has("build") ? num(sj["build"], 0) : -1;
        st.hp = sj.has("hp") ? num(sj["hp"], 1) : -1;
        if (st.build < 0 && st.hp < 0) { warn(t, "stages: each stage needs \"build\" or \"hp\""); continue; }
        if (sj.has("model")) {
            if (!loadChoice(dev, t, v, sj.getStr("model"), st.choice)) continue;
            procedural(st.choice, v);
        } else {
            st.choice = L.models[0];
        }
        if (sj.has("variant")) {
            st.choice.variant = st.choice.model->findVariant(sj.getStr("variant"));
            if (st.choice.variant < 0) warn(t, "stages: variant '" + sj.getStr("variant") + "' not found in " + st.choice.model->path);
        }
        if (sj.has("model")) { // another model: node-specific parts of the type's view do not apply to it
            auto& pr = st.choice.procs;
            pr.erase(std::remove_if(pr.begin(), pr.end(), [&](const ProcAnim& pa) { return !pa.nodeName.empty() && st.choice.model->findNode(pa.nodeName) < 0; }), pr.end());
        } else {
            configureExtras(dev, t, v, st.choice);
        }
        configureExtras(dev, t, sj, st.choice);
        finishChoice(t, st.choice);
        buildStages |= st.build >= 0;
        L.stages.push_back(std::move(st));
    }
    const std::string cons = v.getStr("construction", buildStages ? "stages" : "grow");
    L.construction = cons == "dissolve" ? Construction::Dissolve : cons == "stages" ? Construction::Stages : Construction::Grow;
    if (cons != "grow" && cons != "dissolve" && cons != "stages") warn(t, "construction must be grow, dissolve or stages");
    const bool dis = v.getBool("dissolve", false);
    L.dissolveSpawn = v.getBool("dissolveSpawn", dis);
    L.dissolveDeath = v.getBool("dissolveDeath", dis);
    L.transparent = v.getBool("transparent", false);   // whole model drawn as render::kDrawTransparent
}

static uint32_t parseWhen(const Json& when, const std::function<void(const std::string&)>& bad) {
    std::vector<std::string> states;
    if (when.isString()) states.push_back(when.str());
    for (const auto& w : when.items()) states.push_back(w.str());
    uint32_t bits = 0;
    for (const auto& st : states) {
        uint32_t b = stateBit(st);
        if (!b) bad(st);
        bits |= b;
    }
    return bits;
}

bool GameView::prepareType(uint16_t type, bool allVariants) {
#ifndef __EMSCRIPTEN__
    if (!asyncReader_ || !rules_ || type >= loadedTypes_.size() || (typeCurrent(type) && !allVariants)) return true;
    const auto& original = rules_->types[type];
    UnitType coarse; const UnitType* selected=&original;
    const auto path=requestedModels_[type].empty()?original.view["streaming"].getStr("detailModel"):requestedModels_[type];
    if(!path.empty()){coarse=original;coarse.view.set("model",Json::string(path));selected=&coarse;}
    const auto& t = *selected;
    GeometryPolicy policy;
    try { policy = parseGeometryPolicy(t.view["geometry"]); } catch (const std::exception&) {}
    const bool geometry = policy.clusters || policy.shadowClusters || policy.occluder;
    if (!resources_.preparation().matches(int(type),allVariants)) {
        resources_.preparation().begin(int(type),allVariants);
        const auto discover=[&](const Json& authored) {
            view_adapter::discoverModelPaths(authored,geometry,
                [&](const std::string& key) {
                    return resources_.contains(key);
                },
                [&](const std::string& path,const std::string& key) {
                    resources_.preparation().appendPath(path,key);
                });
        };
        if (!allVariants && deferredLoading_ && deferAppearanceVariants_ && t.view["model"].isArray() && t.view["streaming"].getInt("initialModels", 0) > 0) {
            Json initialView = t.view, choices = Json::array();
            for (size_t index : initialModelChoices(t.view)) choices.push(t.view["model"][index]);
            initialView.set("model", std::move(choices));
            discover(initialView);
        } else discover(t.view);
    }
    if (resources_.preparation().busy()) {
        if (!resources_.preparation().ready()) return false;
        auto result = resources_.preparation().take();
        for (const auto& message : result.warnings) warn(t, message);
        // A runtime spawn can have synchronously cached the same file while the worker ran.
        const auto& key = resources_.preparation().current().second;
        try {
            const bool published=resources_.publishPrepared(
                *dev_,key,std::move(result));
            if(!published && !resources_.contains(key))
                warn(t,"async Model upload failed for "+key);
        }catch(const std::exception& e) {
            // publishPrepared has already queued any partially uploaded
            // Model for GPU-safe retirement; do not retry the same worker
            // result or leave preparation stalled on its failed key.
            warn(t,std::string("async Model publish: ")+e.what());
        }
        resources_.preparation().advance();
    }
    if (resources_.preparation().hasRemaining()) {
        const auto path = resources_.preparation().current().first;
        const auto reader = asyncReader_;
        const auto textures = resources_.textures();
        const bool prepareSkin = t.view.getStr("skinning", "auto") != "palette" && !t.view.has("animationGraph") && !t.view.getBool("programmableAnimation");
        resources_.preparation().launch(path,[path, reader, geometry, prepareSkin, textures] {
            return view_adapter::prepareModelCpu(
                path,reader,textures,{geometry,prepareSkin});
        });
        return false;
    }
    resources_.preparation().clearResource();
#else
    (void)type; (void)allVariants;
#endif
    return true;
}

size_t GameView::pendingModelVariants() const {
    size_t n = 0;
    for (const auto& look : looks_) n += look.pendingVariants.size() - look.nextVariant;
    return n;
}

void GameView::streamModelVariants(double budgetMs) {
    if (!rules_ || !dev_ || budgetMs <= 0) return;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(int64_t(budgetMs * 1000));
    for (size_t type = 0; type < looks_.size(); ++type) {
        auto& look = looks_[type];
        if (!typeLoaded(uint16_t(type)) || look.nextVariant == look.pendingVariants.size()) continue;
        if (!prepareType(uint16_t(type), true)) return;
        const auto& t = rules_->types[type];
        Json defaults = t.view; defaults.erase("model");
        while (look.nextVariant < look.pendingVariants.size()) {
            const auto index = look.pendingVariants[look.nextVariant++];
            const auto& entry = t.view["model"][index];
            const auto path = entry.isString() ? entry.str() : entry.getStr("file");
            Json view = defaults;
            if (entry.isObject()) for (const auto& [key, value] : entry.members()) if (key != "file") view.set(key, value);
            ModelChoice choice;
            if (loadChoice(*dev_, t, view, path, choice)) {
                if (!view.has("procedural")) defaultProcedural(t, look, choice);
                else { parseProcedural(t, view["procedural"], choice); if (view.getBool("proceduralDefaults")) defaultProcedural(t, look, choice); }
                configureExtras(*dev_, t, view, choice);
                finishChoice(t, choice);
                // Append only: EntityAnim::choice, its graph and model stay valid for existing people.
                look.models.push_back(std::move(choice));
            }
            if (look.nextVariant < look.pendingVariants.size() && std::chrono::steady_clock::now() >= deadline) return;
        }
        look.pendingVariants.clear(); look.nextVariant = 0;
#ifndef __EMSCRIPTEN__
        for (const auto& path : resources_.preparation().paths()) {
            auto it = resources_.models().find(path.second);
            if (it != resources_.models().end() && it->second) std::vector<float>().swap(it->second->baked.pending);
        }
        resources_.preparation().reset();
#endif
        if (std::chrono::steady_clock::now() >= deadline) return;
    }
}

bool GameView::warmPipelines(render::Renderer3D& renderer, uint16_t type) {
    if (!typeLoaded(type)) return false;
    bool ready=true;
    const auto& look=looks_[type];
    for(const auto& choice:look.models) if(choice.model) ready=renderer.warmModelPipelines(*choice.model,&choice.materials) && ready;
    for(const auto& choice:look.lods) if(choice.model) ready=renderer.warmModelPipelines(*choice.model,&choice.materials) && ready;
    return ready;
}

void GameView::trimTypes(const std::vector<bool>& keep, bool compact) {
    if (!dev_ || keep.size()!=looks_.size()) return;
#ifndef __EMSCRIPTEN__
    if (resources_.preparation().active()) return; // prepared models do not yet have looks
#endif
    bool changed=compact;
    for (size_t t=0;t<keep.size();++t) if (!keep[t] && loadedTypes_[t]) {
        looks_[t]=TypeLook{}; loadedTypes_[t]=false; changed=true;
    }
    if (!changed) return;
    // Detach MotionWeave presentation sources of unloaded types BEFORE their
    // Models are released below: MotionWeaveSourceBridge::sync would
    // otherwise snapshot the player of a retired Model on the next frame.
    std::vector<uint64_t> sourceIds;
    for (const auto& [id,animation] : anims_) {
        (void)id;
        if (animation.type<keep.size() && !keep[animation.type] &&
            animation.genericViewId!=0)
            sourceIds.push_back(animation.genericViewId);
    }
    if (genericView_ && !sourceIds.empty())
        genericView_->forgetSources(sourceIds);
    for (auto it=anims_.begin();it!=anims_.end();) {
        if (!keep[it->second.type]) it=anims_.erase(it); else ++it;
    }
    // Large projects can keep hundreds of model assets. A hash set makes
    // reclamation linear instead of repeatedly scanning the entire keep list.
    std::unordered_set<const render::Model*> used;
    used.reserve(resources_.models().size());
    auto visit=[&](const ModelChoice& c){
        if(c.model)used.insert(c.model);
        for(const auto& a:c.attach)if(a.model)used.insert(a.model);
    };
    for(const auto& look:looks_) {
        for(const auto& c:look.models)visit(c);
        for(const auto& c:look.lods)visit(c);
        for(const auto& s:look.stages)visit(s.choice);
    }
    const size_t released=resources_.trimUnused(*dev_,used,
        [this](const render::Model* model) {
            animationCache_.erase(model);
        });
    if(std::getenv("AIVERSE_STREAM_TRACE"))std::fprintf(stderr,"[residency] released=%zu models=%zu textures=%zu\n",released,resources_.models().size(),resources_.textures()->gpuCount());
}

// A hot reload is a new GPU/Model publication, not a new gameplay spawn.
// Capture only named playback clocks and presentation event ages; old
// Graph, Animator, pose, rig and Model* must be destroyed before eviction.
// Bounded even if the host repeatedly reloads sources for unseen entities.
void GameView::rememberReloadPlayback(uint64_t key,const EntityAnim& state) noexcept {
    // State preservation is best-effort, never a prerequisite for a safe
    // source/graph transaction. Allocation failure falls back to restart
    // rather than interrupting committed GPU/publication cleanup.
    try {
    constexpr size_t kMaxPending=8192;
    constexpr uint64_t kMaxAgeFrames=120;
    if(reloadPlayback_.size()>=kMaxPending &&
       !reloadPlayback_.contains(key)) {
        // Preserve a strict memory bound across repeated source imports
        // when some deleted entities never return to consume their state.
        for(auto it=reloadPlayback_.begin();it!=reloadPlayback_.end();) {
            if(frame_>=it->second.capturedFrame &&
               frame_-it->second.capturedFrame>kMaxAgeFrames)
                it=reloadPlayback_.erase(it);
            else ++it;
        }
        if(reloadPlayback_.size()>=kMaxPending)return;
    }
    ReloadPlayback snapshot;
    snapshot.type=state.type;
    snapshot.choice=state.choice;
    snapshot.capturedFrame=frame_;
    if(!state.genericViewPose && !state.wasGenericViewPose) {
        snapshot.graph=state.graph.snapshotPlayback();
        // Legacy Animator publishes no independent ECS graph clock.
        // Capture ONLY a stable, unblended base clip; cross-fades and
        // secondary layers must restart safely on different model rigs.
        const auto* oldModel=state.anim.model();
        const int oldClip=state.anim.clip(0);
        const auto sampled=state.anim.baseSample();
        if(oldModel && state.baseKind>=0 && state.baseKind<kAnimCount &&
           oldClip>=0 && size_t(oldClip)<oldModel->clips.size() &&
           sampled.clipA==oldClip && sampled.clipB<0 &&
           oldModel->clips[size_t(oldClip)].duration>0.f &&
           std::isfinite(state.anim.time(0))) {
            snapshot.baseClip=oldModel->clips[size_t(oldClip)].name;
            snapshot.basePhase=std::clamp(
                state.anim.time(0)/
                    oldModel->clips[size_t(oldClip)].duration,
                0.f,1.f);
            snapshot.baseLoop=sampled.loopA;
            snapshot.baseKind=state.baseKind;
        }
    }
    std::copy(std::begin(state.eventAge),std::end(state.eventAge),
              std::begin(snapshot.eventAge));
    snapshot.castAge=state.castAge;
    snapshot.deathAge=state.deathAge;
    snapshot.phase=state.phase;
    snapshot.yaw=state.yaw;
    snapshot.speed=state.speed;
    snapshot.lastPos=state.lastPos;
    snapshot.dead=state.dead;
    reloadPlayback_.insert_or_assign(key,std::move(snapshot));
    }catch(...) {
        // Graph/Model source invalidation remains safe with no snapshot.
    }
}

// Invalidate model-source dependencies as one render-thread transaction.
// No affected Model/Graph pointers may survive the release barrier. Keep
// unaffected TypeLooks and MotionWeave players alive to preserve their clocks.
size_t GameView::reloadModelSources(const std::vector<std::string>& sources) {
    if(!dev_ || !rules_ || sources.empty())return 0;
    std::unordered_set<std::string> changed;
    for(const auto& source:sources)
        if(!source.empty())
            changed.insert(view_adapter::GameModelResourceCache::sourceForKey(source));
    std::vector<uint16_t> rebuild;
    const size_t invalidated=resources_.invalidateSources(*dev_,sources,
        [&](const std::vector<view_adapter::GameModelResourceCache::AffectedModel>& affected) {
            std::unordered_set<const render::Model*> stale;
            for(const auto& resource:affected)
                if(resource.model)stale.insert(resource.model);
            const auto choiceUsesStale=[&](const ModelChoice& choice) {
                if(stale.contains(choice.model))return true;
                for(const auto& attachment:choice.attach)
                    if(stale.contains(attachment.model))return true;
                return false;
            };
            std::vector<bool> typeAffected(looks_.size(),false);
            for(size_t i=0;i<looks_.size();++i) {
                if(!loadedTypes_[i])continue;
                const auto& look=looks_[i];
                bool hit=false;
                for(const auto& choice:look.models)hit |= choiceUsesStale(choice);
                for(const auto& choice:look.lods)hit |= choiceUsesStale(choice);
                for(const auto& stage:look.stages)hit |= choiceUsesStale(stage.choice);
                // A failed/null load may currently render a built-in shape.
                // Source dependencies are also read from the authored tree,
                // including LODs, stages and attachments not currently loaded.
                view_adapter::discoverModelPaths(rules_->types[i].view,false,
                    [](const std::string&){return false;},
                    [&](const std::string& path,const std::string&){
                        if(changed.contains(
                            view_adapter::GameModelResourceCache::sourceForKey(path)))
                            hit=true;
                    });
                const auto& streaming=rules_->types[i].view["streaming"];
                hit |= changed.contains(
                    view_adapter::GameModelResourceCache::sourceForKey(
                        streaming.getStr("coarseModel")));
                hit |= changed.contains(
                    view_adapter::GameModelResourceCache::sourceForKey(
                        streaming.getStr("detailModel")));
                if(!requestedModels_[i].empty())
                    hit |= changed.contains(
                        view_adapter::GameModelResourceCache::sourceForKey(
                            requestedModels_[i]));
                typeAffected[i]=hit;
                if(hit)rebuild.push_back(uint16_t(i));
            }
            // Destroy selective ECS pose sources BEFORE the model GPU buffers.
            std::vector<uint64_t> sourceIds;
            for(const auto& [id,animation]:anims_) {
                (void)id;
                if(animation.type<typeAffected.size() &&
                   typeAffected[animation.type] &&
                   (animation.genericViewPose || animation.wasGenericViewPose ||
                    animation.genericViewId!=0))
                    sourceIds.push_back(animation.genericViewId);
            }
            if(genericView_ && !sourceIds.empty())
                genericView_->forgetSourcesForReimport(sourceIds);
            for(auto it=anims_.begin();it!=anims_.end();) {
                if(it->second.type<typeAffected.size() &&
                   typeAffected[it->second.type]) {
                    rememberReloadPlayback(it->first,it->second);
                    it=anims_.erase(it);
                } else ++it;
            }
            for(auto it=streamingChoices_.begin();it!=streamingChoices_.end();) {
                if(it->second.first<typeAffected.size() &&
                   typeAffected[it->second.first])
                    it=streamingChoices_.erase(it);
                else ++it;
            }
            // Ghost previews are transient and may reference any choice.
            ghostAnim_=EntityAnim{};
            for(const auto* model:stale)animationCache_.erase(model);
            for(const auto type:rebuild) {
                looks_[type]=TypeLook{};
                loadedTypes_[type]=false;
            }
        });
    // Rebind only previously resident appearances. Unloaded editor types
    // continue to load on demand from fresh sources.
    for(const auto type:rebuild)loadType(type);
    return invalidated;
}

// Rebuild authored appearances for external graph/material-library changes.
// Keep GPU Model resources resident and stage all replacement looks BEFORE
// unlinking any old animation state, so malformed edits preserve last-good.
bool GameView::reloadAppearanceTypes(const std::set<size_t>& types,
                                    bool reloadMaterialLibrary,
                                    size_t* refreshed) {
    if(refreshed)*refreshed=0;
    gpuAppearancePending_=false;
    if(!dev_ || !rules_)return false;
#ifndef __EMSCRIPTEN__
    resources_.preparation().reset(); // no worker may publish during staging
#endif
    Json previousLibrary=materialLib_;
    if(reloadMaterialLibrary) {
        try {
            std::string text;
            Json next=Json::object();
            if(read_ && read_("materials.json",text)) {
                next=Json::parse(text)["materials"];
                if(!next.isObject())
                    throw std::runtime_error("materials.json must have a materials object");
            }
            materialLib_=std::move(next);
        } catch(const std::exception& e) {
            warnings_.push_back(std::string("material reload: ")+e.what());
            return false;
        }
    }

    // A successful appearance may use proposed future texture indices,
    // but neither new images nor entire newly imported Models may appear
    // in the live resource cache until ALL authored choices have passed.
    render::ModelImageUploadTransaction imageTransaction(
        *dev_,imageRollback_,gpuDeviceLease_);
    render::ModelResourceCache::ModelMap stagedModels;
    struct StagedModelRollback final {
        render::ModelResourceCache& resources;
        gfx::Device& device;
        render::ModelResourceCache::ModelMap& models;
        ~StagedModelRollback() {
            for(auto& [key,model]:models) {
                (void)key;
                resources.retireUnpublished(device,model);
            }
        }
    } scratchRollback{resources_,*dev_,stagedModels};
    std::map<size_t,TypeLook> staged;
    std::set<size_t> preserveAnimation;
    // Local registry is published only into the replacement TypeLooks after
    // ALL selected type choices successfully stage; old snapshots remain live.
    view::AppearanceAssetRegistry preparedSnapshots;
    struct StrictReloadScope final {
        bool& enabled;
        render::ModelImageUploadTransaction*& activeImages;
        render::ModelResourceCache::ModelMap*& activeModels;
        StrictReloadScope(bool& flag,
                          render::ModelImageUploadTransaction*& targetImages,
                          render::ModelResourceCache::ModelMap*& targetModels,
                          render::ModelImageUploadTransaction& images,
                          render::ModelResourceCache::ModelMap& models)
            :enabled(flag),activeImages(targetImages),activeModels(targetModels) {
            if(enabled || activeImages || activeModels)
                throw std::logic_error("nested RTS appearance transaction");
            enabled=true;
            activeImages=&images;
            activeModels=&models;
        }
        ~StrictReloadScope() {
            activeModels=nullptr;
            activeImages=nullptr;
            enabled=false;
        }
    };
    try {
        StrictReloadScope strict(strictAppearanceReload_,activeImageTransaction_,
                                 activeStagedModels_,imageTransaction,stagedModels);
        for(const size_t index:types) {
            if(index>=loadedTypes_.size() || !loadedTypes_[index])continue;
            const auto& authored=rules_->types[index];
            TypeLook candidate;
            const std::string path=requestedModels_[index].empty()
                ?authored.view["streaming"].getStr("detailModel")
                :requestedModels_[index];
            if(path.empty())
                buildLook(*dev_,authored,candidate);
            else {
                auto selected=authored;
                selected.view.set("model",Json::string(path));
                buildLook(*dev_,selected,candidate);
            }
            // If a previously working graph fails compilation, leave every
            // old TypeLook and its running players intact. This applies to
            // original choices, LODs and stage-specific appearances.
            const auto invalidGraph=[](const ModelChoice& oldChoice,
                                       const ModelChoice& nextChoice) {
                return bool(oldChoice.graph) && !nextChoice.graph;
            };
            const auto& previous=looks_[index];
            bool invalid=false;
            for(size_t i=0;i<previous.models.size() && i<candidate.models.size();++i)
                invalid|=invalidGraph(previous.models[i],candidate.models[i]);
            for(size_t i=0;i<previous.lods.size() && i<candidate.lods.size();++i)
                invalid|=invalidGraph(previous.lods[i],candidate.lods[i]);
            for(size_t i=0;i<previous.stages.size() && i<candidate.stages.size();++i)
                invalid|=invalidGraph(previous.stages[i].choice,candidate.stages[i].choice);
            if(candidate.models.size()<previous.models.size() ||
               candidate.lods.size()<previous.lods.size() ||
               candidate.stages.size()<previous.stages.size() || invalid)
                throw std::runtime_error(
                    "appearance reload declined: a model/graph was lost");

            // Every unchanged runtime graph/model keeps its existing program
            // pointer and ECS source clock. Shader/material-only changes must
            // not replay attack, locomotion, montage or death animations.
            const auto sameRuntime=[](const ModelChoice& oldChoice,
                                      const ModelChoice& nextChoice) {
                return oldChoice.model==nextChoice.model &&
                    oldChoice.graphSignature==nextChoice.graphSignature &&
                    oldChoice.programmableAnimation==nextChoice.programmableAnimation &&
                    oldChoice.needsPose==nextChoice.needsPose &&
                    oldChoice.baked==nextChoice.baked &&
                    oldChoice.procs.size()==nextChoice.procs.size() &&
                    oldChoice.legs.size()==nextChoice.legs.size() &&
                    oldChoice.attach.size()==nextChoice.attach.size() &&
                    oldChoice.additive.size()==nextChoice.additive.size() &&
                    oldChoice.weaponPoses.size()==nextChoice.weaponPoses.size();
            };
            bool canPreserve=reloadMaterialLibrary &&
                candidate.models.size()==previous.models.size() &&
                candidate.lods.size()==previous.lods.size() &&
                candidate.stages.size()==previous.stages.size();
            if(canPreserve) {
                for(size_t i=0;i<candidate.models.size();++i)
                    canPreserve &= sameRuntime(previous.models[i],candidate.models[i]);
                for(size_t i=0;i<candidate.lods.size();++i)
                    canPreserve &= sameRuntime(previous.lods[i],candidate.lods[i]);
                for(size_t i=0;i<candidate.stages.size();++i)
                    canPreserve &= sameRuntime(previous.stages[i].choice,
                                               candidate.stages[i].choice);
            }
            if(canPreserve) {
                for(size_t i=0;i<candidate.models.size();++i)
                    candidate.models[i].graph=previous.models[i].graph;
                for(size_t i=0;i<candidate.lods.size();++i)
                    candidate.lods[i].graph=previous.lods[i].graph;
                for(size_t i=0;i<candidate.stages.size();++i)
                    candidate.stages[i].choice.graph=previous.stages[i].choice.graph;
                preserveAnimation.insert(index);
            }
            // The host renderer is the only owner of GPU shader/pipeline
            // caches. Prewarm all candidate model/LOD/stage materials before
            // publishing either Model GPU residency or TypeLook snapshots.
            // Pending asynchronous jobs retain last-good visuals and ask the
            // host watcher to retry. Failed backend programs reject the edit.
            if(appearancePipelineValidator_) {
                bool pending=false;
                const auto warm=[&](const ModelChoice& choice) {
                    if(!choice.model)return;
                    const auto status=appearancePipelineValidator_(
                        *choice.model,choice.materials);
                    if(status==render::Renderer3D::PipelineWarmStatus::Failed)
                        throw std::runtime_error(
                            "appearance GPU shader/pipeline compilation failed");
                    pending|=status==
                        render::Renderer3D::PipelineWarmStatus::Pending;
                };
                for(const auto& choice:candidate.models)warm(choice);
                for(const auto& choice:candidate.lods)warm(choice);
                for(const auto& stage:candidate.stages)warm(stage.choice);
                if(pending) {
                    gpuAppearancePending_=true;
                    throw std::runtime_error(
                        "appearance GPU shader compilation still pending");
                }
            }
            // The prepared snapshot publishes copies of EXACTLY the
            // material/node/variant data already authored for the RTS choice.
            // It performs no second texture upload or graph compilation.
            size_t packetOrdinal=0;
            const auto stageChoice=[&](ModelChoice& choice,const std::string& kind) {
                view::AppearanceAssetRegistry::Asset prepared;
                prepared.model=choice.model;
                prepared.materials=choice.materials;
                prepared.nodeMaterial=choice.nodeMaterial;
                prepared.graph=choice.graph;
                prepared.geometry=choice.geometry;
                prepared.variant=choice.variant;
                prepared.graphDefinition=choice.graphSignature;
                prepared.modelSource=choice.model?choice.model->path:std::string{};
                prepared.sources={prepared.modelSource,"materials.json"};
                ++packetOrdinal;
                prepared.sourceId=choice.graph ?
                    ((uint64_t(index+1)<<32) | uint64_t(packetOrdinal)) : 0;
                const std::string key=std::to_string(index)+"/"+kind+"/"+
                                      std::to_string(packetOrdinal);
                std::string error;
                if(!preparedSnapshots.publishPrepared(key,prepared,&error))
                    throw std::runtime_error("appearance snapshot "+key+": "+error);
                choice.appearanceSnapshot=preparedSnapshots.find(key);
            };
            for(auto& choice:candidate.models)stageChoice(choice,"model");
            for(auto& choice:candidate.lods)stageChoice(choice,"lod");
            for(auto& stage:candidate.stages)stageChoice(stage.choice,"stage");
            staged.emplace(index,std::move(candidate));
        }
    } catch(const std::exception& e) {
        materialLib_=std::move(previousLibrary);
        warnings_.push_back(std::string("appearance reload: ")+e.what());
        return false;
    }
    if(staged.empty())return true;
    try {
        // Check all cache keys and prepare shared GPU cache map nodes
        // BEFORE the first live Model image vector is modified. This
        // preflights allocations and duplicate scratch texture aliases.
        std::vector<render::Model*> scratch;
        scratch.reserve(stagedModels.size());
        for(const auto& [key,model]:stagedModels) {
            if(resources_.models().contains(key))
                throw std::logic_error("staged Model key became resident: "+key);
            if(model)scratch.push_back(model.get());
        }
        const auto refs=imageTransaction.pendingImageReferences();
        std::vector<render::ModelTextureCache::PendingImage> proposed;
        proposed.reserve(refs.size());
        for(const auto& ref:refs)
            proposed.push_back({ref.model,ref.appendIndex,ref.staged});
        auto shared=resources_.textures()->prepareGpuPromotion(
            scratch,proposed);
        // Reserve capacity for ALL affected Model vectors before publishing
        // the first Image. Failure preserves Model image counts and TypeLooks;
        // the transaction destructor queues staged GPU handles for retirement.
        imageTransaction.commit();
        // Model ownership moves by preallocated std::map node handles;
        // original Model* pointers inside staged TypeLooks remain stable.
        resources_.publishStagedModels(*dev_,stagedModels);
        // Shared-cache hits have already been borrowed during import.
        // Promote only missing keys from accepted scratch Models without
        // uploading them again. Duplicate dedicated GPU handles are not
        // destroyed here: they retire on the image GPU-fence queue.
        auto duplicates=shared.commit();
        imageRollback_.retire(*dev_,std::move(duplicates));
    }catch(const std::exception& e) {
        materialLib_=std::move(previousLibrary);
        warnings_.push_back(std::string("appearance image commit: ")+e.what());
        return false;
    }
    std::set<size_t> affected;
    for(const auto& [index,look]:staged)
        if(!preserveAnimation.contains(index))
            affected.insert(index);
    std::vector<uint64_t> graphSources;
    for(const auto& [entity,state]:anims_) {
        (void)entity;
        if(affected.contains(state.type) && state.genericViewId!=0)
            graphSources.push_back(state.genericViewId);
    }
    if(genericView_ && !graphSources.empty())
        genericView_->forgetSourcesForReimport(graphSources);
    for(auto it=anims_.begin();it!=anims_.end();) {
        if(affected.contains(it->second.type)) {
            rememberReloadPlayback(it->first,it->second);
            it=anims_.erase(it);
        } else ++it;
    }
    for(auto it=streamingChoices_.begin();it!=streamingChoices_.end();) {
        if(affected.contains(it->second.first))it=streamingChoices_.erase(it);
        else ++it;
    }
    if(affected.contains(ghostAnim_.type))ghostAnim_=EntityAnim{};
    for(auto& [index,look]:staged)
        looks_[index]=std::move(look);
    if(refreshed)*refreshed=staged.size();
    return true;
}

// Compatibility adapter only. It never owns the graph request or textures.
// No implicit load, type replacement, material asset write or animation reset.
std::optional<GameView::GraphMaterialTargetInfo> GameView::graphMaterialTarget(
        size_t type,size_t choice,size_t material) const {
    if(!dev_||!gpuDeviceLease_.validFor(*dev_)||!dev_->gpuHealthy()||
       !resources_.boundToResourceDevice(*dev_)||resources_.activeModelReaders()!=0||
       strictAppearanceReload_||activeImageTransaction_||
       type>std::numeric_limits<uint16_t>::max()||type>=looks_.size()||
       !typeCurrent(uint16_t(type))||choice>=looks_[type].models.size())return {};
    const auto& live=looks_[type].models[choice];
    if(!live.model||!live.modelGeneration)return {};
    bool owned=false;
    for(const auto& [key,model]:resources_.models()){
        (void)key;if(model.get()==live.model){owned=true;break;}
    }
    if(!owned)return {};
    const auto& snapshot=live.appearanceSnapshot;
    if(snapshot&&snapshot->model!=live.model)return {};
    const auto& materials=snapshot?snapshot->materials:live.materials;
    const uint64_t publication=snapshot?snapshot->modelPublicationGeneration:live.modelGeneration;
    const uint64_t version=snapshot?snapshot->version:1;
    const view::LiveAppearanceMaterialTarget identity{publication,version,material};
    if(!identity.matches(publication,version,materials.size()))return {};
    const auto stats=render::texture_graph::graphImageStats(*live.model,imageRollback_);
    return GraphMaterialTargetInfo{identity,live.model->path,materials[material].name,live.variant,
        stats.live,stats.tombstones,stats.pendingRetirement};
}

bool GameView::applyGraphMaterial(size_t type,size_t choice,const view::LiveAppearanceMaterialTarget& target,
        const GraphMaterialApply& apply,std::string* error){
    if(error)error->clear();
    try{
        const auto current=graphMaterialTarget(type,choice,target.material);
        if(!apply||!current||!target.matches(current->identity.publication,current->identity.version,
                looks_[type].models[choice].materials.size()))
            throw std::runtime_error("live appearance target missing, stale or busy");
        auto& live=looks_[type].models[choice];
        // Initial choices may predate snapshot publication. Prepare the
        // exact already-authored state locally; publish only on success.
        auto snapshot=live.appearanceSnapshot;
        if(!snapshot){
            auto prepared=std::make_shared<view::AppearanceAssetRegistry::Asset>();
            prepared->model=live.model;prepared->materials=live.materials;
            prepared->nodeMaterial=live.nodeMaterial;prepared->geometry=live.geometry;
            prepared->variant=live.variant;prepared->graph=live.graph;
            prepared->graphDefinition=live.graphSignature;
            prepared->modelSource=live.model->path;
            prepared->modelPublicationGeneration=current->identity.publication;
            prepared->version=current->identity.version;
            // Existing animation players and their own generation stay
            // untouched; this snapshot only supplies appearance metadata.
            prepared->sourceId=live.graph?live.modelGeneration:0;
            prepared->generation=live.graph?live.modelGeneration:0;
            snapshot=std::move(prepared);
        }
        if(!apply(snapshot,live.materials,*live.model,*dev_,gpuDeviceLease_,imageRollback_,error))
            return false;
        live.appearanceSnapshot=std::move(snapshot);
        return true;
    }catch(const std::exception& e){
        if(error)*error=e.what();return false;
    }
}

void GameView::requestDetail(uint16_t type, bool detailed) {
    if(!rules_ || type>=requestedModels_.size())return;
    const auto& t=rules_->types[type];const auto coarse=t.view["streaming"].getStr("coarseModel");
    if(coarse.empty() || t.speed.raw!=0)return;
#ifndef __EMSCRIPTEN__
    if(resources_.preparation().active())return; // never replace an in-flight publication
#endif
    requestedModels_[type]=detailed ? std::string{} : coarse;
}
void GameView::loadType(uint16_t type) {
    if (!rules_ || type >= loadedTypes_.size() || typeCurrent(type)) return;
    const bool replacing=loadedTypes_[type];
    TypeLook ready;
    const auto path=requestedModels_[type].empty()?rules_->types[type].view["streaming"].getStr("detailModel"):requestedModels_[type];
    if(path.empty())buildLook(*dev_,rules_->types[type],ready);
    else {auto selected=rules_->types[type];selected.view.set("model",Json::string(path));buildLook(*dev_,selected,ready);}
    if(std::getenv("AIVERSE_STREAM_TRACE") && rules_->types[type].view["streaming"].has("detailModel"))
        std::fprintf(stderr,"[lod-residency] type=%u level=%s\n",unsigned(type),requestedModels_[type].empty()?"detail":"coarse");
    if(!(replacing && ready.shape && !looks_[type].shape))looks_[type]=std::move(ready);
    loadedModels_[type]=requestedModels_[type];
    if(replacing) {
        // The old Models stay CPU-readable until trimTypes() below; detach
        // their MotionWeave sources now (with portable playback) so the
        // bridge never snapshots a player of a released Model.
        std::vector<uint64_t> sourceIds;
        for(const auto& [id,animation]:anims_) {
            (void)id;
            if(animation.type==type && animation.genericViewId!=0)
                sourceIds.push_back(animation.genericViewId);
        }
        if(genericView_ && !sourceIds.empty())
            genericView_->forgetSourcesForReimport(sourceIds);
    }
    if(replacing)for(auto it=anims_.begin();it!=anims_.end();) {
        if(it->second.type==type) {
            rememberReloadPlayback(it->first,it->second);
            it=anims_.erase(it);
        } else ++it;
    }
    if (twoD_) twoD_->loadType(type);
    loadedTypes_[type] = true;
#ifndef __EMSCRIPTEN__
    if (resources_.preparation().type() == int(type)) {
        for (const auto& path : resources_.preparation().paths()) {
            auto it = resources_.models().find(path.second);
            if (it != resources_.models().end() && it->second) std::vector<float>().swap(it->second->baked.pending);
        }
        resources_.preparation().reset();
    }
#endif
    if(replacing)trimTypes(std::vector<bool>(looks_.size(),true),true);
}

std::string GameView::typeResource(uint16_t type) const {
    if (!rules_ || type >= rules_->types.size()) return {};
    const auto& t = rules_->types[type];
    return t.view.getStr("model", t.view["sprite"].getStr("sheet", t.name));
}

Model* GameView::loadModelCached(gfx::Device& dev, const UnitType& t, const std::string& path) {
    GeometryPolicy policy;
    try { policy = parseGeometryPolicy(t.view["geometry"]); }
    catch (const std::exception& e) { warn(t, e.what()); }
    const std::string key = path + ((policy.clusters || policy.shadowClusters || policy.occluder) ? "#geometry" : "");
    if(activeStagedModels_) {
        // Return a speculative Model for repeated choices/attachments in
        // this transaction without creating a resident cache entry.
        const auto pending=activeStagedModels_->find(key);
        if(pending!=activeStagedModels_->end())
            return pending->second.get();
    }
    auto it = resources_.models().find(key);
    if (it == resources_.models().end()) {
        if(activeStagedModels_) {
            std::unique_ptr<Model> m;
            try {
                std::vector<std::string> warnings;
                // CPU decode can read the shared decoded cache; speculative
                // GPU upload may BORROW resident texture leases, but never
                // insert a new key into ModelTextureCache::gpu_.
                m=loadModel(path,read_,&warnings,resources_.textures());
                if(!m)throw std::runtime_error("appearance Model import failed: "+path);
                for(const auto& message:warnings)warn(t,message);
                if(policy.clusters||policy.shadowClusters||policy.occluder)
                    prepareGeometry(*m);
                if(!uploadModel(dev,*m,false,false,resources_.retainPickingGeometry()))
                    throw std::runtime_error("appearance Model GPU upload failed: "+path);
                auto [staged,inserted]=activeStagedModels_->try_emplace(
                    key,std::move(m));
                (void)inserted;
                return staged->second.get();
            }catch(...) {
                // Failed/partially uploaded scratch Models still own GPU
                // geometry, imported images and morph targets. They must be
                // retired behind a token, never freed by unique_ptr reset.
                if(m)resources_.retireUnpublished(dev,m);
                throw;
            }
        }
        std::unique_ptr<Model> m;
        try {
            std::vector<std::string> w;
            m = loadModel(path, read_, &w, resources_.textures());
            for (auto& s : w) warn(t, s);
            if (!m)throw std::runtime_error("Model load failed: "+path);
            if (policy.clusters || policy.shadowClusters || policy.occluder) prepareGeometry(*m);
            if (!uploadModel(dev,*m,false,true,resources_.retainPickingGeometry()))
                throw std::runtime_error("Model GPU upload failed: "+path);
        } catch (const std::exception& e) {
            warn(t, e.what());
            // Upload may have allocated geometry or earlier images before a
            // later texture/morph allocation failed. Never call m.reset()
            // while those handles may still be in flight on the GPU.
            if(m)resources_.retireUnpublished(dev,m);
        }
        // Preserve the existing failed-load tombstone (null Model) so this
        // authored type can choose a safe fallback instead of a broken draw.
        return resources_.insertIfAbsent(key,std::move(m));
    }
    return it->second.get();
}

void GameView::configureExtras(gfx::Device& dev, const UnitType& t, const Json& v, ModelChoice& c) {
    const Model& m = *c.model;
    auto bad = [&](const char* what) { return [&, what](const std::string& st) { warn(t, std::string(what) + ": unknown state '" + st + "'"); }; };
    auto node = [&](const Json& j, const char* what) {
        if (!j.isString()) return -1;
        int n = m.findNode(j.str());
        if (n < 0) warn(t, std::string(what) + ": node '" + j.str() + "' not found in " + m.path);
        return n;
    };
    if (v.has("attach")) c.attach.clear();
    for (const auto& aj : v["attach"].items()) { // sockets: weapons, shields, carried cargo, banners
        Attachment at;
        at.model = loadModelCached(dev, t, aj.getStr("model"));
        if (!at.model) continue;
        at.materials = at.model->materials;
        for (auto& mt : at.materials) if (lower(mt.name).find("team") != std::string::npos && mt.team == TeamMode::None) mt.team = TeamMode::Tint;
        at.node = node(aj["node"], "attach");
        at.weapon=aj.getStr("weapon");
        const float3 r = vec3(aj["rotate"], {0, 0, 0}) * kDeg;
        const float sc = num(aj["scale"], 1);
        at.local = mat4::translate(vec3(aj["offset"], {0, 0, 0})) * mat4::rotateY(r.y) * mat4::rotateX(r.x) * mat4::rotateZ(r.z) * mat4::scale({sc, sc, sc});
        at.when = parseWhen(aj["when"], bad("attach"));
        if (aj.has("carry")) {
            std::string res = aj.getStr("carry");
            at.carry = res == "any" || res.empty() ? -1 : -3;
            for (size_t i = 0; i < rules_->resources.size(); ++i) if (rules_->resources[i].id == res) at.carry = int(i);
            if (at.carry == -3) { warn(t, "attach: unknown resource '" + res + "'"); at.carry = -1; }
        }
        c.attach.push_back(std::move(at));
    }
    if(v.has("weaponAnimations")){
        c.weaponPoses.clear();const auto& spec=v["weaponAnimations"];c.weaponMask=m.findNode(spec.getStr("mask","Spine"));
        for(const auto&[name,pose]:spec.members())if(pose.isObject()){
            WeaponPose wp;wp.weapon=name;wp.hold=m.findClip(pose.getStr("hold"));wp.aim=m.findClip(pose.getStr("aim"));wp.attack=m.findClip(pose.getStr("attack"));c.weaponPoses.push_back(wp);
        }
    }
    if (v.has("muzzle")) {
        const Json& mz = v["muzzle"];
        c.muzzle = node(mz.isObject() ? mz["node"] : mz, "muzzle");
        c.muzzleOffset = vec3(mz["offset"], {0, 0, 0});
    }
    if (v.has("fx")) c.fx.clear();
    for (const auto& fj : v["fx"].items()) { // effect anchors for particles
        Fx f;
        f.effect = fj.getStr("effect");
        if (f.effect.empty()) { warn(t, "fx: missing \"effect\""); continue; }
        f.node = node(fj["node"], "fx");
        f.offset = vec3(fj["offset"], {0, 0, 0});
        f.when = parseWhen(fj["when"], bad("fx"));
        f.hpBelow = num(fj["hpBelow"], 2);
        f.intensity = num(fj["intensity"], 1);
        c.fx.push_back(f);
    }
    if (v.has("additive")) c.additive.clear();
    for (const auto& aj : v["additive"].items()) { // additive clips layered on any action
        AdditiveSpec a;
        a.clip = m.findClip(aj.getStr("clip"));
        if (a.clip < 0) { warn(t, "additive: clip '" + aj.getStr("clip") + "' not found in " + m.path); continue; }
        a.when = parseWhen(aj["when"], bad("additive"));
        a.weight = num(aj["weight"], 1);
        c.additive.push_back(a);
    }
    if (v.has("footIK")) c.legs.clear();
    for (const auto& lj : v["footIK"].items()) { // two-bone legs planted on the terrain
        LegIK l{node(lj["hip"], "footIK"), node(lj["knee"], "footIK"), node(lj["foot"], "footIK")};
        if (l.hip >= 0 && l.knee >= 0 && l.foot >= 0) c.legs.push_back(l);
    }
}

void GameView::finishChoice(const UnitType& t, ModelChoice& c) {
    for (auto& p : c.procs) {
        p.resolve(c.model);
        if (!p.nodeName.empty() && p.node < 0) warn(t, "procedural: node '" + p.nodeName + "' not found in " + c.model->path);
    }
    bool skinned = false;
    for (const auto& n : c.model->nodes) skinned |= n.skin >= 0 && n.mesh >= 0;
    bool anyClip = false, nodeProcs = false, overlays = false;
    for (bool h : c.hasClip) anyClip |= h;
    for (auto& p : c.procs) nodeProcs |= p.node >= 0;
    for (int k = 0; k < kAnimCount; ++k) overlays |= c.hasClip[k] && c.clips[k].layer > 0 && c.clips[k].mask >= 0;
    // Baked skinning draws crowds instanced; per-object bone edits (masked overlays, node
    // modifiers, additive clips, IK) need the CPU palette path.
    const std::string mode = t.view.getStr("skinning", "auto");
    if (mode != "auto" && mode != "baked" && mode != "palette") warn(t, "skinning must be auto, baked or palette");
    const bool palette = c.programmableAnimation || bool(c.graph) || mode == "palette" || (mode == "auto" && (overlays || nodeProcs || !c.additive.empty() || !c.legs.empty()));
    c.baked = skinned && !palette && dev_ && bakeAnimations(*dev_, *c.model);
    if (c.baked) for (auto& cs : c.clips) { if (cs.layer > 0) { cs.layer = 0; cs.mask = -1; } } // one-shots on the base layer
    c.needsPose = c.programmableAnimation || bool(c.graph) || (!c.baked && (anyClip || nodeProcs)) || !c.attach.empty() || c.muzzle >= 0 || !c.legs.empty();
    for (auto& f : c.fx) c.needsPose |= f.node >= 0;
    // Derived once when the model choice is built/reloaded. Dynamic state,
    // FX overrides, movement and the live Animator are checked per frame.
    c.staticPlacementEligible = !c.graph && !c.needsPose && !c.baked &&
        c.procs.empty() && c.attach.empty() && c.fx.empty();
    c.staticAnimateEligible = c.staticPlacementEligible &&
        c.model && c.model->clips.empty();
}

std::vector<std::string> GameView::modelFiles() const {
    return resources_.modelFiles();
}

bool GameView::loadChoice(gfx::Device& dev, const UnitType& t, const Json& view, const std::string& path, ModelChoice& out) {
    const bool known = resources_.models().count(path) > 0;
    out.model = loadModelCached(dev, t, path);
    if (!out.model) {
        if (!known) warn(t, path + ": using the built-in shape");
        return false;
    }
    out.materials = out.model->materials;
    configureChoice(dev, t, view, out, false);
    out.modelGeneration=view::ModelPublicationEpoch::next();
    return true;
}

int GameView::loadTexture(gfx::Device& dev, const UnitType& t, Model& m, const std::string& path) {
    for (size_t i = 0; i < m.images.size(); ++i)
        if (m.images[i].name == path)return int(i);
    Image img;
    img.name=path;
    if(activeImageTransaction_) {
        // Reuse the shared decoded CPU image cache, but do not publish
        // a GPU cache entry until the entire authored transaction succeeds.
        // Its image keeps textureKey/textureCache for the shared promotion plan.
        if(!read_ || !resources_.textures()->read(path,read_,img))
            throw std::runtime_error("appearance texture not found/unsupported: "+path);
        return activeImageTransaction_->stage(m,std::move(img));
    }
    std::string bytes;
    if(!read_ || !read_(path,bytes)) {
        warn(t,"texture '"+path+"' not found");
        return -1;
    }
    if(!decodeImage(bytes,img)) {
        warn(t,"texture '"+path+"': unsupported format (PNG, JPEG, TGA)");
        return -1;
    }
    return addImage(dev,m,std::move(img));
}

void GameView::configureChoice(gfx::Device& dev, const UnitType& t, const Json& v, ModelChoice& c, bool isShape) {
    Model& m = *c.model;
    c.programmableAnimation=v.getBool("programmableAnimation")||v.has("animationGraph");
    if(v.has("animationGraph")) {
        try {
            Json definition=v["animationGraph"];
            // Resolve external JSON exactly once so a graph's hot-reload
            // signature describes the compiled bytes, not just its path.
            if(definition.isString()) {
                std::string source;
                if(!read_ || !read_(definition.str(),source))
                    throw std::runtime_error("animationGraph file not found: "+
                                             definition.str());
                definition=render::parseAnimationAsset(source);
            }
            c.graph=view::AppearanceAuthoringService::compileGraph(
                m,definition,{},animationOperations_,animationTraits_);
            c.graphSignature=definition.dump(0);
        }catch(const std::exception& e) {
            if(strictAppearanceReload_)throw;
            warn(t,e.what());
        }
    }
    try { c.geometry = parseGeometryPolicy(t.view["geometry"]); }
    catch (const std::exception& e) { warn(t, e.what()); }
    // ---- placement: centre on the ground, rotate the model's forward axis to +X, fit, offset
    const std::string fwd = v.getStr("forward", isShape ? "+x" : "+z");
    float yaw = fwd == "+z" ? 90 * kDeg : fwd == "-z" ? -90 * kDeg : fwd == "-x" ? 180 * kDeg : 0;
    if (fwd != "+x" && fwd != "-x" && fwd != "+z" && fwd != "-z") warn(t, "forward must be +x, -x, +z or -z");
    yaw += num(v["yaw"], 0) * kDeg;
    const float3 mn = m.boundsMin, mx = m.boundsMax, ext = mx - mn;
    mat4 center;
    if (!isShape && v.getBool("center", true)) center = mat4::translate({-(mn.x + mx.x) * 0.5f, -mn.y, -(mn.z + mx.z) * 0.5f});
    float fit = 1;
    const bool sideways = std::fabs(std::sin(yaw)) > 0.7f;
    if (v.has("height") && ext.y > 1e-5f) fit = num(v["height"], 1) / ext.y;
    else if (!isShape && t.structure && v.getBool("fit", true)) {
        const float ex = sideways ? ext.z : ext.x, ez = sideways ? ext.x : ext.z;
        if (ex > 1e-5f && ez > 1e-5f) fit = std::min(float(t.footprintW) / ex, float(t.footprintH) / ez);
    }
    c.fix = mat4::translate(vec3(v["offset"], {0, 0, 0})) * mat4::scale({fit, fit, fit}) * mat4::rotateY(yaw) * center;

    // ---- team colour: "team": "all" | "none" | [material names]; default: names containing "team"
    const Json& team = v["team"];
    for (auto& mt : c.materials) {
        bool on = mt.team != TeamMode::None;
        if (team.isString()) on = team.str() == "all";
        else if (team.isArray()) { on = false; for (const auto& n : team.items()) on |= n.str() == mt.name; }
        else if (!isShape && lower(mt.name).find("team") != std::string::npos) on = true;
        if (on && mt.team == TeamMode::None) mt.team = TeamMode::Tint;
        if (!on) mt.team = TeamMode::None;
    }
    for (const auto& n : team.items())
        if (m.findMaterial(n.str()) < 0) warn(t, "team: material '" + n.str() + "' not found in " + m.path);

    // Shared genre-independent CPU policy preserves authored "*" then
    // named material layering, descendant node assignments and variants.
    // GPU texture loading and warnings remain owned by this RTS host.
    const auto appearance=view::AppearanceChoiceAuthoring::resolve(
        m,v,[this,&t](const std::string& message) {warn(t,message);});
    if(strictAppearanceReload_) {
        // CPU preflight rejects invalid authored graphs and references before
        // texture loader callbacks add more GPU-backed Model images.
        const auto verify=[&](const Json& ref) {
            const auto resolved=view::AppearanceAuthoringService::resolveMaterial(
                materialLib_,ref,[](const std::string& message) {
                    throw std::runtime_error(message);
                });
            if(resolved.has("shaderGraph"))
                (void)render::graph::compile(resolved["shaderGraph"],"material");
        };
        for(const auto& layers:appearance.materialLayers)
            for(const auto& layer:layers)verify(layer);
        for(const auto& extra:appearance.extraMaterials)
            verify(extra.reference);
    }
    auto apply=[&](Material& material,const Json& ref) {
        applyMaterial(dev,t,m,material,ref);
    };
    for(size_t i=0;i<appearance.materialLayers.size();++i)
        for(const auto& layer:appearance.materialLayers[i])
            apply(c.materials[i],layer);
    for(const auto& extra:appearance.extraMaterials) {
        Material material;
        material.name=extra.name;
        // Name/reference defaults and GPU texture side effects match the
        // pre-existing RTS choice path; only the rule resolution moved.
        const Json resolved=resolveMaterial(t,extra.reference);
        apply(material,resolved);
        c.materials.push_back(std::move(material));
    }
    c.nodeMaterial=appearance.nodeMaterial;
    c.variant=appearance.variant;

    // ---- clips
    const Json& anims = v["animations"];
    for (int k = 0; k < kAnimCount; ++k) {
        ClipSpec& cs = c.clips[k];
        cs.loop = !(k == kAnimAttack || k == kAnimDie || k == kAnimSpawn || k == kAnimHit || k == kAnimCast);
        cs.layer = k == kAnimHit ? 2 : k == kAnimCast ? 1 : 0;
        if (k == kAnimMove || k == kAnimCarry) cs.speedRef = float(t.speed.toDouble());
        const Json& a = anims[animName(k)];
        std::vector<std::string> names;
        if (a.isString()) names.push_back(a.str());
        else if (a.isArray()) for (const auto& n : a.items()) names.push_back(n.str());
        else if (a.isObject()) {
            const Json& cl = a["clip"];
            if (cl.isString()) names.push_back(cl.str());
            for (const auto& n : cl.items()) names.push_back(n.str());
            cs.speed = num(a["speed"], cs.speed);
            cs.speedRef = num(a["speedRef"], cs.speedRef);
            cs.fade = num(a["fade"], cs.fade);
            cs.loop = a.getBool("loop", cs.loop);
            if (a.has("mask")) {
                cs.mask = m.findNode(a.getStr("mask"));
                if (cs.mask < 0) warn(t, "animations." + std::string(animName(k)) + ": mask node '" + a.getStr("mask") + "' not found");
                if (k == kAnimAttack) cs.layer = 1;
            }
            const Json& ly = a["layer"];
            if (ly.isString()) cs.layer = ly.str() == "base" ? 0 : ly.str() == "hit" ? 2 : 1;
            else if (ly.isNumber()) cs.layer = std::clamp(int(num(ly, 0)), 0, Animator::kLayers - 1);
            for (const auto& [at, name] : a["events"].members()) { // "events": {"0.35": "hit", "0.1": "step"}
                char* end = nullptr;
                const float tm = std::strtof(at.c_str(), &end);
                if (end == at.c_str()) { warn(t, "animations." + std::string(animName(k)) + ": event time '" + at + "' is not a number"); continue; }
                cs.events.push_back({tm, name.str()});
            }
            for (const auto& bp : a["blend"].items()) { // blend space: [{"clip": "Walk", "speed": 1.5}, {"clip": "Run", "speed": 4}]
                const int ci = m.findClip(bp.getStr("clip"));
                if (ci < 0) { warn(t, "animations." + std::string(animName(k)) + ": blend clip '" + bp.getStr("clip") + "' not found in " + m.path); continue; }
                cs.blend.push_back({ci, num(bp["speed"], 1)});
            }
            std::sort(cs.blend.begin(), cs.blend.end(), [](auto& x, auto& y) { return x.speed < y.speed; });
            if (!cs.blend.empty() && names.empty()) cs.clips.push_back(cs.blend[0].clip);
        }
        for (const auto& n : names) {
            int ci = m.findClip(n);
            if (ci >= 0) { cs.clips.push_back(ci); continue; }
            std::string avail;
            for (auto& cl : m.clips) avail += (avail.empty() ? "" : ", ") + cl.name;
            warn(t, "animations." + std::string(animName(k)) + ": clip '" + n + "' not found in " + m.path + (avail.empty() ? " (no clips)" : " (has: " + avail + ")"));
        }
        if (!anims.isObject() && names.empty() && !m.clips.empty()) { // automatic mapping by clip name
            for (int ci = 0; ci < int(m.clips.size()) && cs.clips.empty(); ++ci) {
                const std::string ln = lower(m.clips[size_t(ci)].name);
                for (const char* const* cand = kAuto[k]; *cand && cs.clips.empty(); ++cand)
                    if (ln.find(*cand) != std::string::npos) cs.clips.push_back(ci);
            }
            // numbered variants of the same action (Warcraft III "Stand", "Stand - 2", "Attack - 1",
            // "Attack - 2"): all of them, one picked at random like an explicit list
            if (cs.clips.size() == 1) {
                auto base = [](std::string n) {
                    n = lower(n);
                    while (!n.empty() && (std::isdigit(static_cast<unsigned char>(n.back())) || n.back() == ' ' || n.back() == '-' || n.back() == '_')) n.pop_back();
                    return n;
                };
                const std::string key = base(m.clips[size_t(cs.clips[0])].name);
                for (int ci = 0; ci < int(m.clips.size()); ++ci)
                    if (ci != cs.clips[0] && !key.empty() && base(m.clips[size_t(ci)].name) == key) cs.clips.push_back(ci);
            }
        }
        c.hasClip[k] = !cs.clips.empty();
    }
}

namespace {
// Preset defaults; JSON fields override them.
void preset(ProcAnim& p, const std::string& type, bool& special, std::string& err) {
    special = true;
    if (type == "bob") { p.channel = ProcAnim::Translate; p.axis = {0, 1, 0}; p.amp = 0.05f; p.wave = ProcAnim::Abs; p.by = ProcAnim::ByDistance; p.freq = 1.2f; p.when = kProcMoving; }
    else if (type == "hover") { p.channel = ProcAnim::Translate; p.axis = {0, 1, 0}; p.amp = 0.12f; p.freq = 0.48f; }
    else if (type == "sway") { p.channel = ProcAnim::Rotate; p.axis = {1, 0, 0}; p.amp = 4 * kDeg; p.freq = 0.5f; }
    else if (type == "spin") { p.channel = ProcAnim::Rotate; p.axis = {0, 1, 0}; p.spin = 180 * kDeg; p.amp = 0; }
    else if (type == "wheel") { p.channel = ProcAnim::Rotate; p.axis = {0, 0, 1}; p.by = ProcAnim::ByDistance; p.spin = -1 / 0.2f; p.amp = 0; }
    else if (type == "steer") { p.channel = ProcAnim::Rotate; p.axis = {0, 1, 0}; p.special = ProcAnim::Steer; p.amp = 1; p.limit = 45 * kDeg; }
    else if (type == "pulse") { p.channel = ProcAnim::Scale; p.axis = {1, 1, 1}; p.amp = 0.05f; p.freq = 1; }
    else if (type == "recoil") { p.channel = ProcAnim::Translate; p.axis = {-1, 0, 0}; p.amp = 0.15f; p.wave = ProcAnim::Const; p.on = kProcEvAttack; p.attack = 0.02f; p.decay = 12; }
    else if (type == "lunge") { p.channel = ProcAnim::Translate; p.axis = {1, 0, 0}; p.amp = 0.12f; p.wave = ProcAnim::Const; p.on = kProcEvAttack; p.attack = 0.08f; p.decay = 8; }
    else if (type == "shake") { p.channel = ProcAnim::Translate; p.axis = {0, 0, 1}; p.amp = 0.05f; p.freq = 18; p.on = kProcEvHit; p.attack = 0.0f; p.decay = 10; }
    else if (type == "flap") { p.channel = ProcAnim::Rotate; p.axis = {1, 0, 0}; p.amp = 25 * kDeg; p.freq = 3; }
    else if (type == "pop") { p.channel = ProcAnim::Scale; p.axis = {1, 1, 1}; p.amp = -0.35f; p.wave = ProcAnim::Const; p.on = kProcEvSpawn; p.attack = 0; p.decay = 6; p.randomPhase = false; }
    else if (type == "squash") { p.channel = ProcAnim::Scale; p.axis = {0.5f, -1, 0.5f}; p.amp = 0.15f; p.wave = ProcAnim::Const; p.on = kProcEvAttack; p.attack = 0.03f; p.decay = 10; }
    else if (type == "lean") { p.channel = ProcAnim::Rotate; p.axis = {1, 0, 0}; p.special = ProcAnim::Lean; p.amp = 0.12f; p.limit = 15 * kDeg; p.when = kProcMoving; }
    else if (type == "aim") { p.channel = ProcAnim::Rotate; p.axis = {0, 1, 0}; p.special = ProcAnim::Aim; p.limit = 180 * kDeg; p.turnSpeed = 180 * kDeg; }
    else if (type == "aimPitch") { p.channel = ProcAnim::Rotate; p.axis = {0, 0, 1}; p.special = ProcAnim::AimPitch; p.limit = 30 * kDeg; p.turnSpeed = 90 * kDeg; }
    else if (type == "look") { p.channel = ProcAnim::Rotate; p.axis = {0, 1, 0}; p.special = ProcAnim::Aim; p.limit = 70 * kDeg; p.turnSpeed = 240 * kDeg; }
    else if (type == "align") { p.channel = ProcAnim::Rotate; p.special = ProcAnim::Align; p.amp = 1; p.turnSpeed = 8; }
    else if (type == "fall") { p.channel = ProcAnim::Rotate; p.axis = {1, 0, 0}; p.special = ProcAnim::Fall; p.amp = 90 * kDeg; p.on = kProcEvDeath; p.decay = 0.6f; }
    else if (type == "sink") { p.channel = ProcAnim::Translate; p.axis = {0, -1, 0}; p.special = ProcAnim::Fall; p.amp = 0.8f; p.on = kProcEvDeath; p.decay = 2.5f; }
    else if (type == "custom" || type.empty()) special = false;
    else err = "unknown procedural type '" + type + "'";
}
} // namespace

void GameView::parseProcedural(const UnitType& t, const Json& list, ModelChoice& c) {
    if (!list.isArray()) { warn(t, "procedural must be an array"); return; }
    for (const auto& j : list.items()) {
        ProcAnim p;
        bool isPreset = false;
        std::string err;
        preset(p, j.getStr("type", "custom"), isPreset, err);
        if (!err.empty()) { warn(t, err + " (bob, hover, sway, spin, wheel, steer, pulse, recoil, lunge, shake, flap, pop, squash, lean, aim, aimPitch, look, align, fall, sink, custom)"); continue; }
        p.nodeName = j.getStr("node");
        if (j.has("channel")) {
            std::string ch = j.getStr("channel");
            p.channel = ch == "rotate" ? ProcAnim::Rotate : ch == "scale" ? ProcAnim::Scale : ProcAnim::Translate;
        }
        p.axis = vec3(j["axis"], p.axis);
        const float angular = p.channel == ProcAnim::Rotate ? kDeg : 1.0f; // rotations are given in degrees
        if (j.has("amp")) p.amp = num(j["amp"], 0) * angular;
        if (j.has("radius") && num(j["radius"], 0) > 0) p.spin = -1 / num(j["radius"], 1); // wheels: rad per unit travelled
        if (j.has("spin")) p.spin = num(j["spin"], 0) * kDeg;
        std::string wave = j.getStr("wave");
        if (!wave.empty()) {
            p.wave = wave == "abs" ? ProcAnim::Abs : wave == "saw" ? ProcAnim::Saw : wave == "triangle" ? ProcAnim::Triangle
                   : wave == "square" ? ProcAnim::Square : wave == "const" ? ProcAnim::Const : ProcAnim::Sine;
        }
        p.freq = num(j["freq"], p.freq);
        if (j.has("by")) p.by = j.getStr("by") == "distance" ? ProcAnim::ByDistance : ProcAnim::ByTime;
        p.phase = num(j["phase"], p.phase);
        p.randomPhase = j.getBool("randomPhase", p.randomPhase);
        if (j.has("on")) {
            p.on = eventIndex(j.getStr("on"));
            if (p.on < 0) warn(t, "procedural: 'on' must be attack, hit, spawn or death");
        }
        p.attack = num(j["attack"], p.attack);
        p.decay = num(j["decay"], p.decay);
        if (j.has("duration")) p.decay = num(j["duration"], p.decay);
        const Json& when = j["when"];
        std::vector<std::string> states;
        if (when.isString()) states.push_back(when.str());
        for (const auto& w : when.items()) states.push_back(w.str());
        if (!states.empty()) p.when = 0;
        for (const auto& s : states) {
            uint32_t b = stateBit(s);
            if (!b) warn(t, "procedural: unknown state '" + s + "'");
            p.when |= b;
        }
        p.fade = num(j["fade"], p.fade);
        if (j.has("limit")) p.limit = num(j["limit"], 180) * kDeg;
        if (j.has("turnSpeed")) p.turnSpeed = num(j["turnSpeed"], 180) * kDeg;
        p.scaleBySpeed = j.getBool("scaleBySpeed", p.scaleBySpeed);
        p.speedRef = num(j["speedRef"], float(t.speed.toDouble()) > 0 ? float(t.speed.toDouble()) : 1.0f);
        c.procs.push_back(p);
    }
}

// Light motion for objects whose model has no clip for it, so shapes and static models feel alive.
void GameView::defaultProcedural(const UnitType& t, const TypeLook& L, ModelChoice& c) {
    auto add = [&](const char* type, auto&& tweak) {
        ProcAnim p;
        bool sp = false;
        std::string err;
        preset(p, type, sp, err);
        tweak(p);
        c.procs.push_back(p);
    };
    if (t.structure) {
        if (!c.hasClip[kAnimDie]) add("sink", [&](ProcAnim& p) { p.amp = 1.2f; p.decay = L.corpse; });
        return;
    }
    if (L.air) add("hover", [](ProcAnim&) {});
    else {
        if (!c.hasClip[kAnimMove]) add("bob", [](ProcAnim&) {});
        if (!c.hasClip[kAnimDie]) {
            add("fall", [](ProcAnim&) {});
            add("sink", [&](ProcAnim& p) { p.amp = 0.6f; p.decay = L.corpse; });
        }
    }
    if (!c.hasClip[kAnimAttack]) add("lunge", [](ProcAnim&) {});
    if (!c.hasClip[kAnimSpawn]) add("pop", [](ProcAnim&) {});
}

} // namespace aiverse::game
