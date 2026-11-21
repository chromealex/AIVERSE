#include "NativeScenePreview.h"
#include "engine/render/Animation.h"
#include "engine/scene/SceneRuntime.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace aiverse::modelview {
namespace {

// The preview uses a genuine GameModule/Simulation for canonical SceneOwner
// and SceneState, NOT an imitation World in the presentation renderer.
// No genre gameplay systems or network state are installed by this opt-in tool.
class ScenePreviewModule final : public GameModule {
public:
    std::string name() const override {return "MotionWeave Scene Preview";}
    void setup(Simulation& simulation,const Json&,const Json&) override {
        scene::registerScenePersistence(simulation);
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    int result(Simulation&) override {return 0;}
};

bool relativeAsset(const std::string& key) {
    if(key.empty())return false;
    std::filesystem::path p(key);
    if(p.is_absolute())return false;
    const auto normalized=p.lexically_normal();
    if(normalized.empty())return false;
    for(const auto& part:normalized)
        if(part==".." || part==".")return false;
    return true;
}

std::string relativeJoin(const std::string& root,
                         const std::string& key) {
    if(!relativeAsset(key))return {};
    return (std::filesystem::path(root)/
            std::filesystem::path(key).lexically_normal()).string();
}
} // namespace

struct NativeScenePreview::Actor {
    const render::Model* model=nullptr; // borrowed until scene GPU retirement
    render::Animator animator;          // ONE parent-pose writer
    render::Pose pose;
    std::vector<render::mat4> matrices;
    bool playing=false;
};

NativeScenePreview::NativeScenePreview()=default;
NativeScenePreview::~NativeScenePreview()=default;

bool NativeScenePreview::readFile(const std::string& path,
                                  std::string& bytes) {
    std::ifstream file(path,std::ios::binary);
    if(!file)return false;
    bytes.assign(std::istreambuf_iterator<char>(file),
                 std::istreambuf_iterator<char>());
    return true;
}

bool NativeScenePreview::registerVisuals(const std::string& text,
                                         std::string& error) {
    try {
        const Json doc=Json::parse(text);
        if(!doc.isObject() || !doc["version"].isNumber() ||
           doc["version"].numberLiteral()!="1" ||
           !doc["prefabs"].isArray() ||
           !doc["residencyKey"].isString())
            throw std::invalid_argument("invalid MotionWeave preview visual catalogue v1");
        const auto& prefabs=doc["prefabs"];
        if(prefabs.size()==0 || prefabs.size()>512)
            throw std::invalid_argument("visual prefab catalogue count out of range");
        for(const Json& entry:prefabs.items()) {
            if(!entry.isObject() || !entry["key"].isString())
                throw std::invalid_argument("visual prefab entry needs a key");
            const std::string key=entry["key"].str();
            if(!scene::SceneAsset::validKey(key))
                throw std::invalid_argument("invalid visual prefab key: "+key);
            if(entry.has("nonvisual")) {
                if(entry["nonvisual"].type()!=Json::Type::Bool ||
                   !entry["nonvisual"].boolean() ||
                   entry.has("model") || entry.has("clip"))
                    throw std::invalid_argument("invalid nonvisual prefab: "+key);
                if(!bindings_.registerNonvisual(key,&error))
                    throw std::invalid_argument(error);
                continue;
            }
            if(!entry["model"].isString() ||
               !relativeAsset(entry["model"].str()))
                throw std::invalid_argument("visual prefab model path invalid: "+key);
            // Graph-based parent animation needs one host-owned Graph
            // evaluator; this preview intentionally uses one native
            // Animator per object instead. Explicit clip is optional.
            if(entry.has("animationGraph"))
                throw std::invalid_argument(
                    "preview uses native Animator; parent AnimationGraph not supported");
            if(entry.has("clip") && !entry["clip"].isString())
                throw std::invalid_argument("visual prefab clip must be text");
            const std::string modelPath=entry["model"].str();
            const std::string clipName=entry.getStr("clip");
            const auto fresh=[this,key,modelPath,clipName](
                const scene::SceneObjectAsset&,
                render::ModelAssetProvider& provider,gfx::Device& device,
                std::optional<view::MotionWeaveSceneGameHost::Object>& out,
                std::string& reason)->bool {
                auto* model=provider.load(device,modelPath);
                if(!model) {
                    reason="native Model load failed: "+modelPath;
                    return false;
                }
                if(!clipName.empty()) {
                    const bool found=std::any_of(
                        model->clips.begin(),model->clips.end(),
                        [&clipName](const auto& clip) {
                            return clip.name==clipName;
                        });
                    if(!found) {
                        reason="authored clip '"+clipName+
                               "' not found in native model "+modelPath;
                        return false;
                    }
                }
                // A newly created provider may reuse a retired Model*
                // address. Invalidate any stale publication before the
                // registry decides whether the Model epoch can be reused.
                if(const auto old=appearances_.find(key);
                   old && old->model &&
                   !provider.resources().acquireModelRead(old->model))
                    (void)appearances_.invalidateSources({modelPath});
                view::AppearanceAssetRegistry::Request authored;
                authored.model=model;
                authored.sources.push_back(modelPath);
                if(!appearances_.publish(key,authored,{}, {},&reason))
                    return false;
                auto snapshot=appearances_.find(key);
                if(!snapshot)return false;
                view::MotionWeaveSceneGameHost::Object object;
                object.parent=std::move(snapshot);
                object.layout.parent=modelPath;
                out=std::move(object);
                return true;
            };
            const auto resident=[this,key,modelPath](
                const scene::SceneObjectAsset&,
                const render::ModelAssetProvider& provider,
                std::optional<view::MotionWeaveSceneGameHost::Object>& out,
                std::string& reason)->bool {
                auto snapshot=appearances_.find(key);
                if(!snapshot || snapshot->modelSource!=modelPath ||
                   !snapshot->model ||
                   !provider.resources().acquireModelRead(snapshot->model)) {
                    reason="model missing from immutable shared GPU pack: "+modelPath;
                    return false;
                }
                view::MotionWeaveSceneGameHost::Object object;
                object.parent=std::move(snapshot);
                object.layout.parent=modelPath;
                out=std::move(object);
                return true;
            };
            if(!bindings_.registerVisual(key,fresh,resident,&error))
                throw std::invalid_argument(error);
            visualPrefabs_.insert(key);
            clipNames_.emplace(key,clipName);
        }
        if(!bindings_.seal(doc["residencyKey"].str(),&error))
            throw std::invalid_argument(error);
        return true;
    }catch(const std::exception& ex) {
        error=ex.what();
        return false;
    }
}

bool NativeScenePreview::init(
    gfx::Device& device,render::RenderDeviceLifetime::Lease lease,
    std::string contentDirectory,std::string firstScene,
    std::string& error) {
    error.clear();
    if(device_ || !lease.validFor(device) ||
       contentDirectory.empty() || !scene::SceneAsset::validKey(firstScene)) {
        error="preview needs valid device epoch, content root and scene key";
        return false;
    }
    device_=&device;
    deviceLease_=lease;
    root_=std::move(contentDirectory);
    firstScene_=std::move(firstScene);
    std::string bytes;
    if(!readFile(root_+"/motionweave-visuals.json",bytes) ||
       !registerVisuals(bytes,error)) {
        if(error.empty())error="motionweave-visuals.json not found";
        return false;
    }

    // Both CPU readers capture only the immutable path string; the
    // SceneLoadCoordinator async worker never touches this Preview instance.
    modelReader_=[root=root_](const std::string& key,std::string& out) {
        const auto path=relativeJoin(root,key);
        return !path.empty() && readFile(path,out);
    };
    sceneReader_=[root=root_](const std::string& key,std::string& out,
                              std::string& failure) {
        const auto path=relativeJoin(root,key+".json");
        if(path.empty() || !readFile(path,out)) {
            failure="scene manifest not found: "+key;
            return false;
        }
        return true;
    };

    simulation_=std::make_unique<Simulation>(
        SimConfig{},std::make_unique<ScenePreviewModule>());
    simulation_->setup(Json::object(),Json::object());
    hierarchy_=std::make_unique<SceneHierarchy>();
    coordinator_=std::make_unique<scene::SceneLoadCoordinator>(
        simulation_->world(),sceneReader_,scene::SceneResourceAdapter{},
        [](World& world,Entity entity,const scene::SceneObjectAsset& obj,
           std::string& failure)->bool {
            // Authoritative native spawner. Logic-only objects are created,
            // too. LocalTransform belongs only to Simulation::world().
            try {
                LocalTransform local;
                local.position.x=Fixed::fromInt(obj.properties.getInt("x",0));
                local.position.y=Fixed::fromInt(obj.properties.getInt("y",0));
                local.position.z=Fixed::fromInt(obj.properties.getInt("z",0));
                world.add<LocalTransform>(entity,local);
                return true;
            }catch(const std::exception& ex) {
                failure=ex.what();
                return false;
            }
        },hierarchy_.get());
    host_=std::make_unique<view::MotionWeaveSceneGameHost>(
        *simulation_,*coordinator_,device,deviceLease_);
    return request(firstScene_,scene::LoadMode::Additive,true,error);
}

bool NativeScenePreview::request(std::string key,scene::LoadMode mode,
                                 bool persistent,std::string& error) {
    error.clear();
    if(!host_ || !pending_.empty() ||
       !scene::SceneAsset::validKey(key)) {
        error="preview already preparing a scene or scene key invalid";
        return false;
    }
    const auto ticket=host_->request(key,mode,persistent);
    if(!ticket.valid()) {
        error="native SceneLoadCoordinator rejected scene request";
        return false;
    }
    pending_.push_back({ticket,std::move(key)});
    return true;
}

void NativeScenePreview::reconcileScenes() {
    const auto* state=simulation_->world().resourceConst<scene::SceneState>();
    std::set<uint64_t> active;
    if(state)
        for(const auto& [id,record]:state->scenes)
            if(record.phase==scene::ScenePhase::Active)active.insert(id);
    for(auto it=manifests_.begin();it!=manifests_.end();)
        if(!active.contains(it->first))it=manifests_.erase(it);
        else ++it;
    for(auto it=actors_.begin();it!=actors_.end();)
        if(!active.contains(it->first.first))it=actors_.erase(it);
        else ++it;
}

void NativeScenePreview::pollAndCommit(std::string& error) {
    error.clear();
    if(!host_ || !coordinator_)return;
    // Only the Device owner checks completed GPU fences, before the next
    // frame records any new MotionWeave model submissions.
    (void)host_->collectRetired();
    host_->poll();
    for(auto it=pending_.begin();it!=pending_.end();) {
        const auto status=coordinator_->status(it->ticket);
        if(status==scene::SceneLoadStatus::Preparing) {++it;continue;}
        if(status!=scene::SceneLoadStatus::Ready) {
            error="native scene preparation rejected: "+it->key+
                  " "+coordinator_->error(it->ticket);
            (void)host_->cancel(it->ticket);
            it=pending_.erase(it);
            continue;
        }
        const auto* manifest=coordinator_->prepared(it->ticket);
        if(!manifest) {
            error="prepared SceneAsset missing";
            (void)host_->cancel(it->ticket);
            it=pending_.erase(it);
            continue;
        }
        const scene::SceneAsset copy=*manifest; // invalid after commit
        view::MotionWeaveSceneGameHost::Statistics stats;
        if(!bindings_.commitReadyShared(
                *host_,it->ticket,*device_,deviceLease_,
                modelReader_,stats,&error)) {
            it=pending_.erase(it);
            continue; // previous Active scenes are not replaced on rejection
        }
        manifests_[it->ticket.scene.value]=copy;
        reconcileScenes();
        it=pending_.erase(it);
    }
    if(!error.empty())lastError_=error;
}

void NativeScenePreview::submit(
    render::Renderer3D& renderer,float dt,std::string& error) {
    error.clear();
    drawnParents_=0;
    if(!host_ || !deviceLease_.validFor(*device_))return;
    presentation_.beginPreparedFrame();
    const auto* state=simulation_->world().resourceConst<scene::SceneState>();
    if(!state)return;
    for(const auto& [sceneId,manifest]:manifests_) {
        const auto active=state->scenes.find(sceneId);
        if(active==state->scenes.end() ||
           active->second.phase!=scene::ScenePhase::Active)continue;
        view::AnimatedSceneAttachmentSources::Statistics stats;
        std::string reason;
        if(!host_->sync({sceneId},std::max(0.f,dt),stats,&reason)) {
            error=reason;
            continue;
        }
        for(const auto& object:manifest.objects) {
            if(!visualPrefabs_.contains(object.prefab))continue;
            Prepared prepared;
            render::mat4 world{};
            world.m[12]=float(object.properties.getInt("x",0));
            world.m[13]=float(object.properties.getInt("y",0));
            world.m[14]=float(object.properties.getInt("z",0));
            if(!host_->prepareParent(
                    {sceneId},std::to_string(object.id),world,
                    prepared,&reason)) {
                error=reason;
                continue;
            }
            const ActorKey key{sceneId,object.id};
            auto& actor=actors_[key];
            if(!actor)actor=std::make_unique<Actor>();
            if(actor->model!=prepared.source->model) {
                actor=std::make_unique<Actor>();
                actor->model=prepared.source->model;
                actor->animator.setModel(actor->model);
                int clip=-1;
                const auto clipName=clipNames_.find(object.prefab);
                if(clipName!=clipNames_.end() &&
                   !clipName->second.empty()) {
                    for(std::size_t i=0;i<actor->model->clips.size();++i)
                        if(actor->model->clips[i].name==clipName->second) {
                            clip=int(i);
                            break;
                        }
                }else if(!actor->model->clips.empty())clip=0;
                if(clip>=0) {
                    render::Animator::Play play;
                    play.restart=true;
                    play.fade=0;
                    actor->animator.play(0,clip,play);
                    actor->playing=true;
                }
            }
            if(actor->playing)actor->animator.update(std::max(0.f,dt));
            actor->animator.evaluate(actor->pose);
            render::worldMatrices(*actor->model,actor->pose,actor->matrices);
            // Native Animator is the one and only parent pose owner.
            // The scene bridge only composes/borrows already sampled data.
            prepared.object.draw.pose=&actor->pose;
            prepared.object.draw.nodeWorld=&actor->matrices;
            presentation_.submitOne(renderer,prepared.object);
            prepared_.push_back(std::move(prepared));
            ++drawnParents_;
        }
    }
    if(!error.empty())lastError_=error;
}

void NativeScenePreview::finishRender() noexcept {
    // Frame-scoped ModelReadLeases must outlive Renderer3D::render().
    prepared_.clear();
}

void NativeScenePreview::saveSnapshot() {
    if(!simulation_ || !pending_.empty())return;
    simulation_->saveState(snapshot_);
    snapshotValid_=true;
}

void NativeScenePreview::restoreSnapshot(std::string& error) {
    error.clear();
    if(!snapshotValid_ || !simulation_ || !host_ || !prepared_.empty()) {
        error="snapshot is unavailable or render read leases still active";
        return;
    }
    // Real Simulation rollback, not a new World pretending to restore.
    simulation_->loadState(snapshot_);
    hierarchy_->rebuild(simulation_->world());
    hierarchy_->update(simulation_->world());
    clearPresentation();
    appearances_.clear(); // source generations/Graphs cannot outlive old GPU pack
    pending_.clear(); // old tickets belong to the pre-rollback epoch
    const auto tickets=host_->restoreResidencyAfterRollback();
    for(const auto ticket:tickets) {
        std::string key;
        const auto* state=simulation_->world().resourceConst<scene::SceneState>();
        if(state) {
            const auto found=state->scenes.find(ticket.scene.value);
            if(found!=state->scenes.end())key=found->second.asset;
        }
        pending_.push_back({ticket,key});
    }
}

void NativeScenePreview::clearPresentation() noexcept {
    prepared_.clear();
    actors_.clear();
    manifests_.clear();
}

std::size_t NativeScenePreview::sceneCount() const {
    return manifests_.size();
}
std::size_t NativeScenePreview::authoritativeEntities() const {
    if(!simulation_)return 0;
    std::size_t count=0;
    simulation_->world().eachExistingConst<scene::SceneOwner>(
        [&](Entity,const scene::SceneOwner&){++count;});
    return count;
}
std::size_t NativeScenePreview::sharedProviderRefs() const {
    return host_?host_->sharedProviderReferences(bindings_.residencyKey()):0;
}

bool NativeScenePreview::shutdown(std::string& error) {
    error.clear();
    if(shutdownComplete_)return true;
    clearPresentation();
    appearances_.clear();
    pending_.clear();
    if(host_) {
        if(!host_->completeForShutdown()) {
            error="MotionWeave SceneGameHost GPU completion not proven; GPU owners quarantined";
            lastError_=error;
            (void)host_.release(); // never destruct possibly GPU-live Models
            return false;
        }
        host_.reset();
    }
    coordinator_.reset();
    hierarchy_.reset();
    simulation_.reset();
    shutdownComplete_=true;
    device_=nullptr;
    return true;
}

} // namespace aiverse::modelview
