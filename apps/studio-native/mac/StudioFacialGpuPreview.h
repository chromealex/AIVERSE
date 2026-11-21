// Native Studio facial preview owns one real imported Model and ONE Animator.
// Reuses MotionWeave FacialPreviewSession for expression and IK postprocessing.
// Only called on the Metal gfx::Device owner thread, never from browser code.
#pragma once
#include "apps/studio-native/StudioFacialPreviewBinding.h"
#include "engine/character/vivid/HumanCreatorWorkbench.h"
#include "engine/render/RenderDeviceLifetime.h"
#include "engine/render/ModelAssetProvider.h"
#include "engine/render/Animation.h"
#include "engine/render/Renderer3D.h"
#include <aiverselib/animation/FacialPreviewSession.h>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::studio {

class StudioFacialGpuPreview final {
public:
    StudioFacialGpuPreview()=default;
    StudioFacialGpuPreview(const StudioFacialGpuPreview&)=delete;
    StudioFacialGpuPreview& operator=(const StudioFacialGpuPreview&)=delete;
    void setDeviceLease(render::RenderDeviceLifetime::Lease lease) {
        gpuLease_=std::move(lease);
    }
    // Discard GPU resources only after device completion; on failure leave
    // the Model quarantined instead of destroying in-flight handles.
    bool clear(gfx::Device& device,render::Renderer3D& renderer) noexcept;
    bool sync(gfx::Device& device,render::Renderer3D& renderer,const std::string& projectRoot,
              const StudioFacialPreviewBinding& binding,std::string& error);
    bool submit(render::Renderer3D& renderer,
                const StudioFacialPreviewBinding& binding,
                float dt,std::string& error);
    // Keep this lease alive through Animator evaluation AND renderer.render().
    render::ModelReadLease acquireRead() const {
        return provider_?provider_->resources().acquireModelRead(model_):render::ModelReadLease{};
    }
    const render::Model* model() const noexcept {return model_;}
    // Viewport sculpt: world-space ray against the selected face meshes of
    // the displayed character (bind pose, normalized head coordinates).
    bool sculptRaycast(render::float3 origin,render::float3 direction,
                       const vivid::HumanFaceShape& selection,vivid::HumanFaceHit& hit) const {
        (void)selection; // the pick surface was built for the bound selection
        if(!model_ || !framed_ || pickSurface_.empty())return false;
        const auto toModel=drawWorld_.inverse();
        const auto o=toModel.transformPoint(origin);
        const auto d=toModel.transformPoint(origin+direction)-o;
        return vivid::raycastHumanFace(pickSurface_,o,d,hit);
    }
    // The last published character still matches the selection while a new
    // revision (edit) imports: keep drawing it instead of flashing the map.
    // Appearance-only characters (no facial rig state) also stay on screen
    // while ANOTHER character imports, e.g. after "show assembled character".
    bool showsCharacter(const StudioFacialPreviewBinding& binding) const noexcept {
        return model_ && !guid_.empty() && binding.bound() && (guid_==binding.guid() || appearanceOnly_);
    }
    bool ready() const noexcept {return bool(model_) && (appearanceOnly_ || facial_.ready());}
    uint64_t sourceRevision() const noexcept {return revision_;}
    // Makeup placement in skin UV from the shown character's face geometry
    // (lips, cheeks, eyelids, chin); throws when no face/skin is available.
    Json alignMakeup(const StudioFacialPreviewBinding& binding) const;
    // Bind-pose height of the shown character in model units (metres for glTF).
    float baseHeight() const {return model_?model_->boundsMax.y-model_->boundsMin.y:0.f;}
    std::vector<std::string> clipNames() const {std::vector<std::string> out;if(model_)for(const auto& c:model_->clips)if(!c.name.starts_with("__vivid_preview_"))out.push_back(c.name);return out;}
private:
    static bool readProjectFile(const std::filesystem::path& projectRoot,
                                const std::string& key,std::string& bytes);
    render::RenderDeviceLifetime::Lease gpuLease_;
    std::unique_ptr<render::ModelAssetProvider> provider_;
    render::Model* model_=nullptr; // borrowed from provider, guarded per frame
    render::mat4 drawWorld_{};bool framed_=false; // last head framing (sculpt rays)
    vivid::HumanFacePickSurface pickSurface_; // CPU face triangles kept for sculpt picking
    // Fast interactive edits: pristine CPU model (no texture pixels) and the
    // face/look currently baked into the published Model.
    std::unique_ptr<render::Model> pristine_;
    Json appliedFace_,appliedLook_;
    float expressionTime_=0; // test-expression clock (talk visemes)
    // Live wardrobe try-on assembly (worker thread) and abandoned jobs.
    std::future<std::unique_ptr<render::Model>> tryOnJob_;
    std::vector<std::future<std::unique_ptr<render::Model>>> staleTryOn_;
    uint64_t tryOnRevision_=0;std::string tryOnGuid_;
    Json appliedWardrobe_;
    uint64_t appliedReload_=0;
    bool editInPlace(gfx::Device& device,render::Renderer3D& renderer,const StudioFacialPreviewBinding& binding);
    render::Model* hair_=nullptr;  // Human Creator groom cards (borrowed)
    std::string hairKey_;
    vivid::RigAuthoring creatorRig_; // Human Creator presentation overlay
    lib::animation::FacialPreviewSession facial_;
    render::Animator animator_; // ONLY parent pose evaluator in this preview
    render::Pose pose_;
    int previewClipBase_=-1,activePreviewClip_=-1;
    std::vector<render::mat4> nodeWorld_;
    std::string projectRoot_,guid_;
    std::string readerProject_,pendingSource_,pendingProject_,pendingGuid_,modelKey_;
    uint64_t pendingRevision_=0,publicationSerial_=0;
    bool appearanceOnly_=false;
    uint64_t revision_=0;
    uint64_t rejectedRevision_=0;
    std::string rejectedGuid_,rejectedProject_,rejectedError_;
};

} // namespace aiverse::studio
