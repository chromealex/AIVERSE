// Generic View visibility field and visual fog overlay.
// The host supplies the visibility mask; this does NOT authorize gameplay.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <span>
#include <utility>
#include <vector>
#include "engine/data/Json.h"
#include "engine/gfx/Gfx.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/NativePresentationRetirement.h"
#include "engine/render/GpuOwnedMemory.h"

namespace aiverse::view {

struct VisibilityLook {
    enum Style : int { Smooth = 0, Cells = 1, Grid = 2, Pattern = 3 };
    bool show = true;                // false: the view draws no fog (the simulation still hides enemies)
    float color[3] = {8 / 255.0f, 10 / 255.0f, 14 / 255.0f};
    float unexplored = 0.92f, explored = 0.47f;   // opacity of the fog over never-seen / seen-before ground
    Style style = Smooth;
    int softness = 2;                // blur radius in cells (smooth, pattern)
    float fade = 0.3f;               // seconds for a cell to clear or fog over again
    float noise = 0;                 // 0..1: cloudy, moving edges
    float height = 1.3f;             // sheet height above the ground (covers units and low buildings)
    float gridColor[3] = {1, 1, 1};
    float gridAlpha = 0;             // cell grid lines (0 = none)
    bool gridEverywhere = false;     // grid over the whole map, not only under the fog
};

// Host-defined base styling followed by optional view-specific overrides.
VisibilityLook parseVisibilityLook(const Json& rulesFog, const Json& gameFog);

class VisibilityField {
public:
    // Shader and pipeline are staged first; partial creation errors never
    // publish GPU handles into a live field.
    bool init(gfx::Device& dev);
    // Safe for hosts without a prior full-device completion proof. A refused
    // idle barrier leaves all published handles owned and retryable.
    bool shutdownAfterGpuIdle();
    // Caller must already have established GPU idle (RtsView::shutdown does).
    void shutdown();
    bool ready() const { return pipe_.valid(); }
    void setLook(const VisibilityLook& look) { look_ = look; dirty_ = true; ++revision_; }
    const VisibilityLook& look() const { return look_; }

    // Sheet draped over the ground. Creates replacement buffers/texture
    // before replacing the live set; on failure throws and preserves old
    // texture, buffers, visibility data and indices. Replacing live handles
    // requires a successful full-device idle boundary.
    void build(gfx::Device& dev, int w, int h, const std::function<float(float, float)>& height);
    // Per frame: vision of the viewer (0/1/2 per cell) -> faded + blurred field, uploaded when it changed.
    // Returns true when the field changed this frame.
    // Staged sheet mesh: GPU VB/IB and optional new map-sized CPU field are
    // private until the owner cohort commits. Unlike legacy build(), this
    // path never waits for full GPU idle or modifies live presentation state.
    class PreparedSheet final {
        friend class VisibilityField;
    public:
        PreparedSheet()=default;
        PreparedSheet(const PreparedSheet&)=delete;
        PreparedSheet& operator=(const PreparedSheet&)=delete;
        ~PreparedSheet() noexcept {
            if(!dev_)return;
            if(ib_.valid())try {dev_->destroyBuffer(ib_);}catch(...) {}
            if(vb_.valid())try {dev_->destroyBuffer(vb_);}catch(...) {}
        }
    private:
        VisibilityField* owner_=nullptr;
        gfx::Device* dev_=nullptr;
        uint64_t revision_=0;
        gfx::BufferHandle vb_{},ib_{};
        uint32_t indexCount_=0;
        int width_=0,height_=0;
        bool resize_=false;
        std::vector<uint8_t> target_,cur_,field_;
    };
    bool sheetMatches(int w,int h) const noexcept {
        return vb_.valid() && ib_.valid() && w_==w && h_==h;
    }
    bool needsSheetRebuild(int w,int h) const noexcept {
        return !sheetMatches(w,h);
    }
    std::unique_ptr<PreparedSheet> prepareSheet(gfx::Device& dev,int w,int h,
        const std::function<float(float,float)>& height);
    // Phase 1 stages a new faded/blurred field and private R8 GPU texture,
    // with no mutation to the currently sampled fog texture or clock.
    // Phase 2 publishes the prepared result after all dependent owners have
    // prepared successfully; abandoned candidates destroy only private RHI.
    class PreparedUpdate final {
        friend class VisibilityField;
    public:
        PreparedUpdate()=default;
        PreparedUpdate(const PreparedUpdate&)=delete;
        PreparedUpdate& operator=(const PreparedUpdate&)=delete;
        ~PreparedUpdate() noexcept {
            if(dev_ && tex_.valid()) {
                try {dev_->destroyTexture(tex_);} catch(...) {}
            }
        }
        bool changed() const noexcept{return changed_;}
        const std::vector<uint8_t>& field() const noexcept{return field_;}
    private:
        gfx::Device* dev_=nullptr;
        gfx::TextureHandle tex_{};
        VisibilityField* owner_=nullptr;
        uint64_t revision_=0;
        std::vector<uint8_t> target_,cur_,field_;
        std::vector<uint16_t> tmp_;
        double lastTime_=-1;
        bool dirty_=false,fading_=false,changed_=false,unchangedOnly_=false;
    };
    std::unique_ptr<PreparedUpdate> prepareUpdate(
        std::span<const uint8_t> vision,int w,int h,double time,
        const PreparedSheet* stagedSheet=nullptr);
    // Publish BOTH sheet and field after the whole owner cohort prepares.
    // Never allocates GPU resources or waits for idle during commit.
    // Invariant violations are fatal because no partial publication may escape.
    bool commitPreparedFrame(std::unique_ptr<PreparedSheet> sheet,
                             std::unique_ptr<PreparedUpdate> field) noexcept;
    // Same-size editor sculpt: update only the height-draped Fog Sheet.
    // Retains the R8 GPU texture, all target/cur/blur pixels, animation
    // clock and viewer memory. A map-size change must still use
    // commitPreparedFrame(sheet,field) to install a new sized R8 image.
    bool commitPreparedSheet(std::unique_ptr<PreparedSheet> sheet) noexcept;
    bool commitPreparedUpdate(std::unique_ptr<PreparedUpdate> candidate);
    bool update(std::span<const uint8_t> vision,int w,int h,double time);
    // Existing vector callers remain source-compatible; host buffers
    // can now be passed as a non-owning span without a temporary vector.
    bool update(const std::vector<uint8_t>& vision,int w,int h,double time) {
        return update(std::span<const uint8_t>(vision.data(),vision.size()),
                      w,h,time);
    }
    const std::vector<uint8_t>& field() const { return field_; }
    // Optional blur replacing the box blur over the grid rectangle (planets: stacked face rows need a
    // seam-aware neighbourhood). Called with the faded field and the blur radius in cells; null = box blur.
    void setBlur(std::function<void(std::vector<uint8_t>& field, int radius)> fn) { blur_ = std::move(fn); dirty_ = true; ++revision_; }
    gfx::TextureHandle texture() const { return tex_; }
    size_t pendingGpuRetirements() const noexcept {return retired_.pending();}
    uint64_t stateRevision() const noexcept {return revision_;}
    bool collectRetired(gfx::Device& dev) {
        if(!dev_)return true;
        if(dev_!=&dev)throw std::logic_error("VisibilityField retirement: wrong device");
        return retired_.collect(dev);
    }
    render::GpuOwnedMemoryStats ownedGpuMemoryStats() const {
        if (!dev_) return {};
        render::GpuOwnedMemoryCounter c(*dev_);
        c.buffer(vb_);c.buffer(ib_);c.texture(tex_);
        return c.stats();
    }
    // Draws the sheet (Renderer3D custom draw after the scene). `cells`: weak tier forces Cells.
    void drawCandidate(PreparedSheet*,PreparedUpdate*,render::Renderer3D&,double,bool);
    void draw(render::Renderer3D& r, double time, bool lowTier);

private:
    gfx::Device* dev_ = nullptr;
    render::NativePresentationRetirement retired_;
    uint64_t revision_=1;
    gfx::ShaderHandle shader_;
    gfx::PipelineHandle pipe_;
    gfx::BufferHandle vb_, ib_;
    gfx::TextureHandle tex_;
    uint32_t indexCount_ = 0;
    int w_ = 0, h_ = 0;
    VisibilityLook look_;
    std::vector<uint8_t> target_, cur_, field_;
    std::vector<uint16_t> tmp_;
    double lastTime_ = -1;
    bool dirty_ = true, fading_ = false;
    std::function<void(std::vector<uint8_t>&, int)> blur_;
};

} // namespace aiverse::game
