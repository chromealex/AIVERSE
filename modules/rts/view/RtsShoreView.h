// Shores in the view (docs/water.md § Look): the map's "waterNetwork" banks turned into static models — embankments
// (quay wall, coping, railing or stone parapet, paved promenade, lamps, benches, bollards, steps down to the water),
// cliffs (a rugged rock face from the water to a grassy top and a back slope, boulders and foam at the foot), beaches
// (sand running under the water, surf, umbrellas, loungers, lifeguard towers) and natural banks, with the transitions
// between them. The water surface itself is the terrain's (the baked '~' cells). Textures: procedural, or the game's
// (shore type "textures"); props: built-in shapes or the game's models. Drawn through Renderer3D::drawModel.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../../../engine/render/Model.h"
#include "../../../engine/render/NativePresentationRetirement.h"
#include "../../../engine/render/Renderer3D.h"
#include "RtsWater.h"

namespace aiverse::game {

class RtsShoreView {
public:
    void init(gfx::Device& dev, const Json& shoreTypes, render::FileReader reader);
    // CPU runtime configuration carried to a staged replacement before GPU
    // initialization. Meshes and RHI resources are deliberately not copied.
    void inheritRuntimeStateForReload(const RtsShoreView& old);
    bool ownsGpuResources() const noexcept {return dev_!=nullptr;}
    void shutdown(gfx::Device& dev);
    // Called only at a frame boundary, BEFORE this frame's scene submissions.
    // Polls actual GPU retirement tokens, never frame-count heuristics.
    bool collectRetired(gfx::Device& dev) { return retirement_.collect(dev); }
    size_t pendingGpuRetirements() const noexcept {return retirement_.pending();}
    uint64_t stateRevision() const noexcept {return stateRevision_;}
    void setMap(const Json& map);   // its "waterNetwork"; rebuilt on the next draw
    void invalidateGround() { dirty_ = true; ++stateRevision_; }
    // heightAt in world units; waterY: the water surface (world units); w, h: the map size in cells
    void draw(render::Renderer3D& r, const std::function<float(float, float)>& heightAt, float waterY, int w, int h);
    struct PreparedRebuild;
    bool needsVisualPreparation(float waterY,int w,int h) const noexcept {
        return dirty_ || waterY!=builtWaterY_ || w!=builtW_ || h!=builtH_;
    }
    std::shared_ptr<PreparedRebuild> prepareRebuild(
        gfx::Device& dev,const std::function<float(float,float)>& heightAt,
        float waterY,int w,int h,bool forceGround=false);
    void drawCandidate(const std::shared_ptr<PreparedRebuild>&,render::Renderer3D&);
    void drawPrepared(render::Renderer3D&);
    void commitRebuild(std::shared_ptr<PreparedRebuild> candidate) noexcept;
    bool active() const { return !net_.empty(); }
    const water::ShoreTypes& types() const { return types_; }
    const water::Network& network() const { return net_; }
    render::GpuOwnedMemoryStats ownedGpuMemoryStats() const {
        if (!dev_) return {};
        render::GpuOwnedMemoryCounter c(*dev_);
        for (const auto& m : chunks_)
            if (m) render::appendModelOwnedGpuMemory(*m,c);
        for (const auto& m : transparent_)
            if (m) render::appendModelOwnedGpuMemory(*m,c);
        for (const auto& p : propModels_)
            if (p.model) render::appendModelOwnedGpuMemory(*p.model,c);
        // The shore kit owns its images; road/chunk meshes borrow them.
        for (const auto& i : images_) c.texture(i.tex);
        return c.stats();
    }
    const std::vector<std::string>& warnings() const { return warnings_; }

    // extra materials after the road kit's (RtsRoadView::Mat)
    enum Mat : int { kStone = 100, kRock, kSand, kFoam, kCanvas, kPaint, kShoreMatEnd };

private:
    struct PropModel { std::string path; std::unique_ptr<render::Model> model; };
    struct PropInstance { int model = -1; render::mat4 xf; };
    void rebuild(gfx::Device& dev, const std::function<float(float, float)>& heightAt, float waterY, int w, int h);
    void buildUnpublished(gfx::Device& dev, const std::function<float(float, float)>& heightAt, float waterY, int w, int h);
    void release(gfx::Device& dev);
    int propModel(gfx::Device& dev, const std::string& path);

    gfx::Device* dev_ = nullptr;
    uint64_t stateRevision_=1; // invalidates unpublished multi-owner frame leases
    // GPU kit uploads may throw before images_ receives ownership. Preserve
    // this unpublished kit for an idle-gated shutdown or recovery retry.
    std::unique_ptr<render::Model> buildingKit_;
    render::FileReader read_;
    water::ShoreTypes types_;
    water::Network net_;
    Json netJson_;
    bool dirty_ = false;
    float builtWaterY_ = 0;
    int builtW_ = 0, builtH_ = 0;
    render::NativePresentationRetirement retirement_;
    std::vector<std::unique_ptr<render::Model>> chunks_, transparent_;
    std::vector<PropModel> propModels_;
    std::vector<PropInstance> props_;
    std::vector<render::Image> images_;
    std::vector<render::Material> materials_;
    std::vector<std::string> warnings_;
};

} // namespace aiverse::game
