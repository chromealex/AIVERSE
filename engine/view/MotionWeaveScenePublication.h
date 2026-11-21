// AIVERSE MotionWeave — scene publication transaction for cosmetic ECS.
// Genre-neutral: no SceneOwner, gameplay World, simulation or network writes.
// A native scene loader calls replaceScene() ONLY after its commit succeeds
// at the presentation owner barrier, and unload() at its unload barrier.
#pragma once
#include "AnimatedSceneAttachmentSources.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

class MotionWeaveScenePublication final {
public:
    using Snapshot=AppearanceAssetRegistry::Snapshot;
    using Resolver=EcsAttachmentScene::Resolver;
    using ChildSources=AnimatedSceneAttachmentSources;
    using StaticPrepared=ImportedAttachmentAuthoring::Prepared;

    // One committed logical object in the host's scene manifest.
    // objectKey is stable across reload/rollback, e.g. decimal SceneObjectAsset::id.
    // A caller must publish Model/Graph snapshots in the SAME cache before
    // calling this. The resolver is used only during this synchronous call.
    struct Object {
        std::string objectKey;
        AttachmentLayout layout;
        Snapshot parent;
        Resolver resolveStaticChild;
        std::map<std::string,Snapshot> animatedChildren;
    };
    struct Statistics {
        size_t objects=0;
        size_t animatedChildren=0;
        size_t playbackQueued=0;
        size_t playbackDeclined=0;
        ChildSources::Statistics initialGraph;
    };

    MotionWeaveScenePublication()=default;
    MotionWeaveScenePublication(const MotionWeaveScenePublication&)=delete;
    MotionWeaveScenePublication& operator=(const MotionWeaveScenePublication&)=delete;

    // Phase 1: stage all EcsAttachmentScene children, immutable appearances
    // and independent Graph players into an unpublished private World.
    // Use BEFORE SceneLoadCoordinator::commit() after the native host has
    // prepublished the required assets; a failed/aborted coordinator commit
    // calls discardStaged(), leaving the current scene untouched.
    // A failed staging attempt also invalidates any older pending candidate.
    // Call only at the owner pre-render barrier, never on asset workers.
    bool stageScene(uint64_t sceneId,std::span<const Object> objects,
                      const render::ModelResourceCache& cache,
                      Statistics& stats,std::string* error=nullptr) {
        if(error)error->clear();
        pending_.erase(sceneId);
        try {
            if(!sceneId)
                throw std::invalid_argument("presentation scene ID must be nonzero");
            auto next=std::make_unique<Generation>();
            std::set<std::string> keys;
            for(const auto& object:objects) {
                if(object.objectKey.empty() || object.objectKey.size()>256 ||
                   !keys.insert(object.objectKey).second)
                    throw std::invalid_argument(
                        "duplicate, empty or oversized presentation object key");
                if(!object.parent || !object.parent->model ||
                   !object.parent->modelPublicationGeneration)
                    throw std::invalid_argument(
                        "presentation source Model is not published");
                bool needsStaticResolver=false;
                for(const auto& child:object.layout.attachments)
                    needsStaticResolver|=child.enabled && !child.animated;
                if(needsStaticResolver && !object.resolveStaticChild)
                    throw std::invalid_argument(
                        "presentation static child resolver is missing");

                // Validate complete authored document, including disabled
                // child identities, before adding ANY ECS children for this
                // object. Unexpected supplied animated snapshots are errors.
                const Json authored=AttachmentLayoutCodec::write(object.layout);
                std::set<std::string> animatedKeys;
                for(const auto& child:object.layout.attachments) {
                    if(!child.enabled || !child.animated)continue;
                    const auto it=object.animatedChildren.find(child.id);
                    if(it==object.animatedChildren.end() ||
                       !it->second || !it->second->model ||
                       !it->second->modelPublicationGeneration ||
                       !it->second->graph ||
                       it->second->graph->model()!=it->second->model)
                        throw std::invalid_argument(
                            "missing/mismatched published independent child Graph");
                    animatedKeys.insert(child.id);
                }
                if(animatedKeys.size()!=object.animatedChildren.size())
                    throw std::invalid_argument(
                        "unexpected independent child Graph snapshot");

                Entity visualParent=next->presentation.create();
                std::string reason;
                Resolver resolver=object.resolveStaticChild;
                if(!resolver)
                    resolver=[](const std::string&) -> Snapshot {
                        return {};
                    };
                if(!next->attachments.activate(
                       {sceneId,object.objectKey},visualParent,authored,
                       object.parent,cache,resolver,&reason))
                    throw std::runtime_error(
                        "scene visual attachment publish: "+reason);
                next->parents.emplace(object.objectKey,object.parent);
                next->parentEntities.emplace(object.objectKey,visualParent);
                next->childSnapshots.emplace(
                    object.objectKey,object.animatedChildren);
            }

            // Capture portable Graph clocks from the currently presented
            // scene BEFORE destroying its Graph players. Only matching
            // scene/object/stable child ID and copied rig hierarchy qualify.
            size_t playbackQueued=0,playbackDeclined=0;
            if(const auto previous=scenes_.find(sceneId);
               previous!=scenes_.end() && previous->second) {
                for(const auto& [key,children]:next->childSnapshots)
                    for(const auto& [childId,appearance]:children) {
                        const ChildSources::Key child{sceneId,key,childId};
                        const auto snapshot=
                            previous->second->animated.capturePlayback(child);
                        if(!snapshot) {++playbackDeclined;continue;}
                        if(next->animated.queuePlayback(
                                child,*snapshot,*appearance->model))
                            ++playbackQueued;
                        else ++playbackDeclined;
                    }
            }
            std::vector<ChildSources::Input> inputs=collectInputs(
                sceneId,*next);
            ChildSources::Statistics initial{};
            std::string reason;
            if(!next->animated.sync(
                   next->attachments,cache,inputs,0.f,initial,&reason))
                throw std::runtime_error(
                    "independent child Graph publication: "+reason);

            // Each independently created scene Graph host has its OWN
            // monotonic source IDs starting at one. A shared render-only
            // View must not receive colliding IDs from different scenes.
            // Allocate a distinct global visual draw ID for every parent
            // and every independently animated child, never reused even on
            // scene replace. A rejected candidate may consume IDs but can
            // never overwrite the last-good visual generation.
            for(const auto& [key,parent]:next->parents) {
                (void)parent;
                next->parentDrawIds.emplace(key,allocateDrawId());
            }
            for(const auto& [key,children]:next->childSnapshots)
                for(const auto& [childId,appearance]:children) {
                    (void)appearance;
                    next->childDrawIds.emplace(
                        ChildSources::Key{sceneId,key,childId},
                        allocateDrawId());
                }

            // Allocate the committed map slot here, BEFORE the host's
            // authoritative SceneLoadCoordinator::commit. This makes
            // phase-2 publication a move of unique_ptr with no map insert.
            // An empty slot is not reported as a published scene.
            (void)scenes_.try_emplace(sceneId);
            const size_t objectCount=next->parents.size();
            pending_.insert_or_assign(sceneId,std::move(next));
            stats={objectCount,inputs.size(),
                   playbackQueued,playbackDeclined,initial};
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    // Phase 2: call immediately AFTER a successful authoritative commit.
    // This swaps the prevalidated visual generation without importing,
    // compiling or allocating map slots after gameplay activation.
    bool commitStaged(uint64_t sceneId) {
        const auto prepared=pending_.find(sceneId);
        const auto current=scenes_.find(sceneId);
        if(prepared==pending_.end() || current==scenes_.end())
            return false;
        current->second=std::move(prepared->second);
        pending_.erase(prepared);
        return true;
    }
    bool discardStaged(uint64_t sceneId) {
        return pending_.erase(sceneId)!=0;
    }
    // Convenience for hosts without a separate authoritative scene
    // commit transaction (eg. Studio authoring/model preview).
    bool replaceScene(uint64_t sceneId,std::span<const Object> objects,
                      const render::ModelResourceCache& cache,
                      Statistics& stats,std::string* error=nullptr) {
        return stageScene(sceneId,objects,cache,stats,error) &&
               commitStaged(sceneId);
    }

    // Exactly ONE presentation graph step per scene/frame, before renderer
    // submission. Call on the same owner thread as replaceScene().
    bool sync(uint64_t sceneId,const render::ModelResourceCache& cache,
              float dt,ChildSources::Statistics& stats,
              std::string* error=nullptr) {
        if(error)error->clear();
        const auto it=scenes_.find(sceneId);
        if(it==scenes_.end() || !it->second) {
            if(error)*error="presentation scene not published";
            return false;
        }
        const auto inputs=collectInputs(sceneId,*it->second);
        return it->second->animated.sync(
            it->second->attachments,cache,inputs,dt,stats,error);
    }

    // Prepare the existing host-precomputed parent pose, without advancing
    // an AnimationGraph. The caller sets draw.pose and draw.nodeWorld from
    // its ONE parent animation owner and retains the move-only Prepared
    // through Renderer3D::render().
    bool prepareParent(uint64_t sceneId,const std::string& objectKey,
                       const render::ModelResourceCache& cache,
                       const render::mat4& world,StaticPrepared& out,
                       std::string* error=nullptr) const {
        if(error)error->clear();
        const auto it=scenes_.find(sceneId);
        if(it==scenes_.end() || !it->second) {
            if(error)*error="presentation scene not published";
            return false;
        }
        StaticPrepared staged;
        if(!it->second->attachments.prepare(
               sceneId,objectKey,cache,world,staged,error))
            return false;
        const auto id=it->second->parentDrawIds.find(objectKey);
        if(id==it->second->parentDrawIds.end()) {
            if(error)*error="presentation parent render ID unavailable";
            return false;
        }
        staged.object.id=id->second;
        out=std::move(staged);
        return true;
    }

    // The child pose is already evaluated by sync(), never by the render
    // View. The returned Object is graphless and owns frame-scoped leases.
    bool prepareChild(uint64_t sceneId,const std::string& objectKey,
                      const std::string& childId,
                      const render::ModelResourceCache& cache,
                      const render::ModelDraw& parentDraw,
                      std::span<const render::mat4> parentNodeMatrices,
                      ChildSources::Prepared& out,
                      std::string* error=nullptr) const {
        if(error)error->clear();
        const auto it=scenes_.find(sceneId);
        if(it==scenes_.end() || !it->second) {
            if(error)*error="presentation scene not published";
            return false;
        }
        const ChildSources::Key key{sceneId,objectKey,childId};
        const auto id=it->second->childDrawIds.find(key);
        if(id==it->second->childDrawIds.end()) {
            if(error)*error="independent child render ID unavailable";
            return false;
        }
        ChildSources::Prepared staged;
        if(!it->second->animated.prepare(
               it->second->attachments,cache,key,parentDraw,
               parentNodeMatrices,staged,error))
            return false;
        staged.object.id=id->second;
        out=std::move(staged);
        return true;
    }

    // Authoritative scene unload/replace MUST invoke this on the same
    // owner barrier after the last CPU Prepared draw, but BEFORE old
    // Model/Graph CPU owners are retired. The asset host separately proves
    // GPU completion; CPU ModelReadLease is NOT a GPU fence.
    bool unload(uint64_t sceneId) {
        pending_.erase(sceneId);
        const auto it=scenes_.find(sceneId);
        if(it==scenes_.end())return false;
        const bool published=bool(it->second);
        scenes_.erase(it);
        return published;
    }

    // After a SUCCESSFUL authoritative scene Replace/commit, the caller
    // passes SceneState's complete ACTIVE scene IDs. Keep persistent scenes
    // if and only if they remain in that authoritative active set. This
    // works with the peer SceneLoadCoordinator without linking animation
    // code to SceneState, rollback internals or authoritative components.
    // Never call from an asynchronous scene-file poll/worker.
    size_t unloadAbsent(std::span<const uint64_t> activeScenes) {
        std::set<uint64_t> keep;
        for(const uint64_t id:activeScenes) {
            if(id==0 || !keep.insert(id).second)
                throw std::invalid_argument(
                    "active scene list contains zero or duplicate IDs");
        }
        for(auto it=pending_.begin();it!=pending_.end();) {
            if(keep.count(it->first)) {++it;continue;}
            it=pending_.erase(it);
        }
        size_t removed=0;
        for(auto it=scenes_.begin();it!=scenes_.end();) {
            if(keep.count(it->first)) {++it;continue;}
            const bool wasPublished=bool(it->second);
            it=scenes_.erase(it);
            removed+=wasPublished;
        }
        return removed;
    }

    // On authoritative rollback/loadState, all previously borrowed
    // presentation source IDs, Graph clocks and appearance epochs are
    // invalid. Clear them before queueing new scene residency restore.
    void resetAfterRollback() {clear();}
    void clear() {
        pending_.clear();
        scenes_.clear();
    }
    bool contains(uint64_t sceneId) const noexcept {
        const auto it=scenes_.find(sceneId);
        return it!=scenes_.end() && bool(it->second);
    }
    size_t sceneCount() const noexcept {
        size_t count=0;
        for(const auto& [id,scene]:scenes_) {
            (void)id;
            count+=bool(scene);
        }
        return count;
    }
    Json saveScene(uint64_t sceneId) const {
        const auto it=scenes_.find(sceneId);
        if(it==scenes_.end() || !it->second)
            throw std::out_of_range("presentation scene not published");
        return it->second->attachments.saveScene(sceneId);
    }

private:
    struct Generation {
        World presentation;
        SceneAttachmentHost attachments;
        ChildSources animated;
        std::map<std::string,Snapshot> parents;
        std::map<std::string,Entity> parentEntities;
        std::map<std::string,std::map<std::string,Snapshot>> childSnapshots;
        std::map<std::string,uint64_t> parentDrawIds;
        std::map<ChildSources::Key,uint64_t,ChildSources::KeyLess>
            childDrawIds;
        Generation():attachments(presentation){}
        Generation(const Generation&)=delete;
        Generation& operator=(const Generation&)=delete;
        ~Generation() {
            // Detach all Graph players FIRST, before ECS attachment children
            // and appearance snapshots. No borrowed rig outlives this World.
            animated.clear();
            attachments.clear();
        }
    };
    static std::vector<ChildSources::Input> collectInputs(
            uint64_t sceneId,const Generation& scene) {
        std::vector<ChildSources::Input> result;
        for(const auto& [objectKey,children]:scene.childSnapshots)
            for(const auto& [childId,appearance]:children)
                result.push_back({{sceneId,objectKey,childId},
                                  appearance,appearance->parameters});
        return result;
    }
    uint64_t allocateDrawId() {
        if(nextDrawId_==0 ||
           nextDrawId_==std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("presentation draw IDs exhausted");
        return nextDrawId_++;
    }
    uint64_t nextDrawId_=1; // global to this host; never reused
    std::map<uint64_t,std::unique_ptr<Generation>> scenes_;
    std::map<uint64_t,std::unique_ptr<Generation>> pending_;
};

} // namespace aiverse::view
