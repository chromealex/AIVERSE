// AIVERSE ModelView — opt-in native MotionWeave scene/prefab preview.
// No FPS/RTS glue: one real Simulation, SceneLoadCoordinator and visual
// GameHost. All GPU operations and scene commits run on the owner thread.
#pragma once
#include "engine/scene/SceneLoadCoordinator.h"
#include "engine/view/MotionWeaveScenePrefabBindings.h"
#include "engine/view/View.h"
#include "engine/render/Renderer3D.h"
#include "engine/sim/SceneHierarchy.h"
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::modelview {

class NativeScenePreview final {
public:
    NativeScenePreview(); // defined out of line: Actor is incomplete here
    NativeScenePreview(const NativeScenePreview&)=delete;
    NativeScenePreview& operator=(const NativeScenePreview&)=delete;
    ~NativeScenePreview();

    // Directory contains motionweave-visuals.json and scenes/<name>.json.
    // The reader is copied into the async coordinator (no capture of this).
    bool init(gfx::Device& device,
              render::RenderDeviceLifetime::Lease deviceLease,
              std::string contentDirectory,std::string firstScene,
              std::string& error);
    bool request(std::string sceneKey,scene::LoadMode mode,
                 bool persistent,std::string& error);
    // Poll on an owner frame boundary BEFORE Renderer3D::beginScene.
    void pollAndCommit(std::string& error);
    // Submit borrowed Model packets after Renderer3D::beginScene, before
    // Renderer3D::render. Caller must call finishRender() after render().
    void submit(render::Renderer3D& renderer,float dt,std::string& error);
    void finishRender() noexcept;
    void saveSnapshot();
    void restoreSnapshot(std::string& error);
    bool shutdown(std::string& error);

    std::size_t sceneCount() const;
    std::size_t pendingJobs() const noexcept {return pending_.size();}
    std::size_t drawnParents() const noexcept {return drawnParents_;}
    std::size_t authoritativeEntities() const;
    std::size_t sharedProviderRefs() const;
    const std::string& startupScene() const noexcept {return firstScene_;}
    const std::string& lastError() const noexcept {return lastError_;}
    bool hasSnapshot() const noexcept {return snapshotValid_;}

private:
    struct Actor;
    using ActorKey=std::pair<uint64_t,uint64_t>;
    using Prepared=::aiverse::view::ImportedAttachmentAuthoring::Prepared;
    struct Pending {
        scene::SceneTicket ticket;
        std::string key;
    };
    static bool readFile(const std::string& path,std::string& bytes);
    bool registerVisuals(const std::string& json,std::string& error);
    void reconcileScenes();
    void clearPresentation() noexcept;

    gfx::Device* device_=nullptr;
    render::RenderDeviceLifetime::Lease deviceLease_;
    std::string root_;
    std::string firstScene_;
    std::set<std::string> visualPrefabs_;
    std::map<std::string,std::string> clipNames_;
    render::FileReader modelReader_;
    scene::SceneLoadCoordinator::ReadAsset sceneReader_;
    ::aiverse::view::MotionWeaveScenePrefabBindings bindings_;
    ::aiverse::view::AppearanceAssetRegistry appearances_;
    std::unique_ptr<Simulation> simulation_;
    std::unique_ptr<SceneHierarchy> hierarchy_;
    std::unique_ptr<scene::SceneLoadCoordinator> coordinator_;
    std::unique_ptr<::aiverse::view::MotionWeaveSceneGameHost> host_;
    std::map<uint64_t,scene::SceneAsset> manifests_;
    std::vector<Pending> pending_;
    std::map<ActorKey,std::unique_ptr<Actor>> actors_;
    ::aiverse::View presentation_;
    std::vector<Prepared> prepared_;
    SimSnapshot snapshot_;
    bool snapshotValid_=false;
    bool shutdownComplete_=false;
    std::size_t drawnParents_=0;
    std::string lastError_;
};

} // namespace aiverse::modelview
