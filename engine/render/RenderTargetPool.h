// AIVERSE LENS: managed color/depth render targets addressed by stable asset keys.
// Render targets are presentation resources, never ECS component fields.
// An acquired target is stable while its specification remains unchanged.
// Replacement textures are retired only after a confirmed GPU completion
// token; unknown completion defers destruction until explicitly safe shutdown.
// A key whose texture creation failed is not re-created every frame: the same
// specification is refused until it changes or retryFailedTargets() is called.
// The sum of live target pixels is bounded (setPixelBudget, default 64 Mpix).
#pragma once
#include "OffscreenTargetLifetime.h"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aiverse::render {

class CameraRenderTargetPool {
public:
    struct Target {
        gfx::TextureHandle color{}, depth{};
        int width=0, height=0;
        bool hdr=false, exposedDepth=true, ready=false;
        bool matches(int w,int h,bool useHdr,bool wantDepth) const {
            return width==w && height==h && hdr==useHdr && exposedDepth==wantDepth &&
                   color.valid() && depth.valid();
        }
    };
    // 8192 x 8192: one maximal target fits; many large targets do not.
    static constexpr uint64_t kDefaultPixelBudget=uint64_t(64)*1024*1024;
    CameraRenderTargetPool() = default;
    // GPU idle is unknown here, so live/retired textures cannot be destroyed
    // safely: they are leaked with a warning. Call shutdownAfterGpuIdle().
    ~CameraRenderTargetPool() {
        if (device_ && (!targets_.empty() || retiredTargets()))
            std::fprintf(stderr,"[lens] CameraRenderTargetPool destroyed without "
                         "shutdownAfterGpuIdle: leaking %zu live, %zu retired targets\n",
                         targets_.size(),retiredTargets());
    }
    CameraRenderTargetPool(const CameraRenderTargetPool&) = delete;
    CameraRenderTargetPool& operator=(const CameraRenderTargetPool&) = delete;
    bool init(gfx::Device& device) {
        if (device_ && device_!=&device) return false;
        if (!retired_)retired_=std::make_unique<OffscreenTargetRetirement>(device);
        device_=&device;
        return true;
    }
    const Target* find(uint64_t key) const {
        auto it=targets_.find(key);
        return it==targets_.end() ? nullptr : &it->second;
    }
    gfx::TextureHandle color(uint64_t key) const {
        const auto* target=find(key);
        return target && target->ready ? target->color : gfx::TextureHandle{};
    }
    gfx::TextureHandle depth(uint64_t key) const {
        const auto* target=find(key);
        return target && target->ready && target->exposedDepth ? target->depth : gfx::TextureHandle{};
    }
    const Target* acquire(uint64_t key,int width,int height,bool hdr=false,bool exposeDepth=true) {
        if (!device_ || !key || width<=0 || height<=0 ||
            width>8192 || height>8192) return nullptr;
        if (hdr && !device_->supports(gfx::PixelFormat::RGBA16F)) return nullptr;
        auto found=targets_.find(key);
        if (found!=targets_.end() && found->second.matches(width,height,hdr,exposeDepth))
            return &found->second;
        const Spec spec{width,height,hdr,exposeDepth};
        if (auto failed=failed_.find(key); failed!=failed_.end()) {
            if (failed->second==spec) return nullptr; // no per-frame retry storm
            failed_.erase(failed);
        }
        const uint64_t pixels=uint64_t(width)*uint64_t(height);
        const uint64_t replaced=found!=targets_.end()?
            uint64_t(found->second.width)*uint64_t(found->second.height):0;
        if (livePixels()-replaced+pixels>pixelBudget_) return nullptr;
        gfx::TextureDesc colorDesc;
        colorDesc.width=width;
        colorDesc.height=height;
        colorDesc.renderTarget=true;
        colorDesc.format=hdr?gfx::PixelFormat::RGBA16F:gfx::PixelFormat::RGBA8;
        colorDesc.label="lens camera color";
        Target fresh{};
        fresh.width=width;
        fresh.height=height;
        fresh.hdr=hdr;
        fresh.exposedDepth=exposeDepth;
        fresh.color=device_->createTexture(colorDesc);
        if (!fresh.color.valid()) { failed_[key]=spec; return nullptr; }
        auto depthDesc=colorDesc;
        depthDesc.format=gfx::PixelFormat::Depth;
        depthDesc.filter=gfx::Filter::Nearest;
        depthDesc.label="lens camera depth";
        // Even color-only captures require a depth attachment for correct
        // 3D occlusion. exposedDepth controls public access, not attachment.
        try {
        fresh.depth=device_->createTexture(depthDesc);
        if (!fresh.depth.valid()) {
            device_->destroyTexture(fresh.color); // not yet submitted to GPU
            failed_[key]=spec;
            return nullptr;
        }
        if (found!=targets_.end()) {
            retire(found->second);
            found->second=fresh;
            return &found->second;
        }
        auto inserted=targets_.emplace(key,fresh);
        return &inserted.first->second;
        } catch (...) {
            destroy(fresh); // private pair never submitted or published
            throw;
        }
    }
    void markReady(uint64_t key) {
        auto it=targets_.find(key);
        if (it!=targets_.end()) it->second.ready=true;
    }
    void markInvalid(uint64_t key) {
        auto it=targets_.find(key);
        if (it!=targets_.end()) it->second.ready=false;
    }
    void release(uint64_t key) {
        failed_.erase(key);
        auto it=targets_.find(key);
        if (it==targets_.end()) return;
        retire(it->second);
        targets_.erase(it);
    }
    // Retire (GPU-fenced) every target whose key is not in `keep`.
    size_t releaseExcept(const std::unordered_set<uint64_t>& keep) {
        std::vector<uint64_t> stale;
        for (const auto& [key,target]:targets_) { (void)target; if (!keep.count(key)) stale.push_back(key); }
        for (const auto& [key,spec]:failed_) { (void)spec; if (!keep.count(key)) stale.push_back(key); }
        for (uint64_t key:stale) release(key);
        return stale.size();
    }
    // Allow keys whose creation failed to try the same specification again
    // (e.g. after the host freed GPU memory or recreated the device).
    void retryFailedTargets() { failed_.clear(); }
    bool failed(uint64_t key) const { return failed_.count(key)!=0; }
    void setPixelBudget(uint64_t pixels) { pixelBudget_=pixels; }
    uint64_t pixelBudget() const { return pixelBudget_; }
    uint64_t livePixels() const {
        uint64_t total=0;
        for (const auto& [key,target]:targets_) { (void)key; total+=uint64_t(target.width)*uint64_t(target.height); }
        return total;
    }
    // Call only at the owning GPU frame boundary, after prior submission.
    void collectCompleted() {
        if (retired_)retired_->collect();
    }
    // Failure retains live targets and the retirement owner for retry.
    void shutdownAfterGpuIdle() {
        if (!device_) return;
        if (!device_->waitForResourceIdle() || !retired_->drain())
            throw std::runtime_error("LENS target shutdown: GPU completion unavailable");
        for(auto& [key,target]:targets_) { (void)key; destroy(target); }
        targets_.clear();
        failed_.clear();
        retired_.reset();
        device_=nullptr;
    }
    size_t activeTargets() const { return targets_.size(); }
    size_t retiredTargets() const { return retired_?retired_->pendingPairs():0; }
private:
    struct Spec {
        int width=0,height=0; bool hdr=false,depth=true;
        bool operator==(const Spec& o) const {
            return width==o.width && height==o.height && hdr==o.hdr && depth==o.depth;
        }
    };
    gfx::Device* device_=nullptr;
    std::unordered_map<uint64_t,Target> targets_;
    std::unordered_map<uint64_t,Spec> failed_;
    uint64_t pixelBudget_=kDefaultPixelBudget;
    std::unique_ptr<OffscreenTargetRetirement> retired_;
    void destroy(const Target& t) {
        if(t.color.valid()) device_->destroyTexture(t.color);
        if(t.depth.valid()) device_->destroyTexture(t.depth);
    }
    void retire(const Target& target) {
        retired_->retire(target.color,target.depth);
    }
};

} // namespace aiverse::render
