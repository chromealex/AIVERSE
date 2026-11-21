#pragma once
// GPU-fenced lifetime owner for editor offscreen Viewport color/depth.
// Lives in Editor, NOT the window: a window may close or destruct while
// ImGui or the GPU still references its previous texture IDs.
// Records are consumed only after backend completion, or an explicit
// submitted-GPU-work idle confirmation at a safe frame boundary.
#include "engine/gfx/Gfx.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::editor {

class ViewportGpuRetirement {
private:
    struct Live {
        const void* owner = nullptr;
        std::string name;
        int width = 0, height = 0;
        gfx::TextureHandle color{}, depth{};
    };
public:
    struct Pending {
        gfx::TextureHandle handle{};
        uint64_t token = 0; // 0 = no fence, quarantine
        uint64_t bytes = 0;
    };
    struct LiveStats {
        std::string name;
        int width = 0, height = 0;
        size_t textures = 0, unknownTextures = 0;
        uint64_t knownBytes = 0;
    };
    struct Stats {
        size_t fenced = 0, unfenced = 0;
        uint64_t knownPendingBytes = 0;
        uint64_t budgetBytes = 0;
        uint64_t completedFence = 0;
        bool overBudget = false;
    };

    // A warning/pressure threshold, NOT permission to free in-flight GPU
    // memory. At the next frame's pre-render safe point the host can choose
    // to explicitly wait for submitted GPU commands and then drain.
    void setBudgetBytes(uint64_t bytes) { budgetBytes_ = bytes; }
    uint64_t budgetBytes() const { return budgetBytes_; }

    // Active framebuffer registry: observer only, never an additional owner.
    // Owner keys are stable Viewport addresses and are forgotten by release().
    void noteLive(const void* owner, const char* name, int width, int height,
                  gfx::TextureHandle color, gfx::TextureHandle depth) {
        if (!owner) return;
        auto it = std::find_if(live_.begin(), live_.end(),
            [owner](const Live& entry) { return entry.owner == owner; });
        if (it == live_.end()) {
            live_.push_back({owner, name ? name : "Viewport", width, height, color, depth});
        } else {
            it->name = name ? name : "Viewport";
            it->width = width;
            it->height = height;
            it->color = color;
            it->depth = depth;
        }
    }
    void forgetLive(const void* owner) {
        live_.erase(std::remove_if(live_.begin(), live_.end(),
            [owner](const Live& entry) { return entry.owner == owner; }), live_.end());
    }
    std::vector<LiveStats> liveStats(const gfx::Device& device) const {
        std::vector<LiveStats> result;
        result.reserve(live_.size());
        for (const Live& live : live_) {
            LiveStats row;
            row.name = live.name;
            row.width = live.width;
            row.height = live.height;
            for (gfx::TextureHandle h : {live.color, live.depth}) {
                if (!h.valid()) continue;
                ++row.textures;
                gfx::TextureInfo info{};
                if (!device.gpuHealthy() || !device.textureInfo(h, info) ||
                    info.width <= 0 || info.height <= 0) {
                    ++row.unknownTextures;
                    continue;
                }
                // Render targets are uncompressed 2D base levels; driver
                // padding/mips are not included in this lower bound.
                const uint64_t bpp = info.format == gfx::PixelFormat::RGBA16F ? 8 :
                    info.format == gfx::PixelFormat::RGBA32F ? 16 :
                    info.format == gfx::PixelFormat::R8 ? 1 : 4;
                row.knownBytes += uint64_t(info.width) * uint64_t(info.height) * bpp;
            }
            result.push_back(std::move(row));
        }
        return result;
    }

    void retire(gfx::Device& device, gfx::TextureHandle& texture) {
        if (!texture.valid()) return;
        gfx::TextureInfo info{};
        uint64_t bytes = 0;
        if (device.textureInfo(texture,info) && info.width > 0 && info.height > 0) {
            const uint64_t bpp = info.format == gfx::PixelFormat::RGBA16F ? 8 :
                info.format == gfx::PixelFormat::RGBA32F ? 16 :
                info.format == gfx::PixelFormat::R8 ? 1 : 4;
            bytes = uint64_t(info.width) * uint64_t(info.height) * bpp;
        }
        pending_.push_back({texture,device.gpuSubmissionFence(),bytes});
        knownPendingBytes_ += bytes;
        texture = {};
    }

    size_t collect(gfx::Device& device) {
        const uint64_t completed = device.gpuCompletedFence();
        if (!completed) return 0;
        size_t count = 0;
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->token && it->token <= completed) {
                device.destroyTexture(it->handle);
                knownPendingBytes_ -= it->bytes;
                it = pending_.erase(it);
                ++count;
            } else ++it;
        }
        return count;
    }

    // Host has synchronized all PREVIOUSLY SUBMITTED GPU work, and must
    // call this before encoding new commands that use these textures.
    size_t drainAfterGpuIdle(gfx::Device& device) {
        size_t count = 0;
        for (auto& p : pending_) {
            if (p.handle.valid()) { device.destroyTexture(p.handle); ++count; }
        }
        pending_.clear();
        knownPendingBytes_ = 0;
        return count;
    }

    Stats stats(const gfx::Device& device) const {
        Stats s;
        s.budgetBytes = budgetBytes_;
        s.knownPendingBytes = knownPendingBytes_;
        s.completedFence = device.gpuCompletedFence();
        for (const auto& p : pending_) {
            if (p.token) ++s.fenced;
            else ++s.unfenced;
        }
        s.overBudget = budgetBytes_ && knownPendingBytes_ > budgetBytes_;
        return s;
    }
private:
    std::vector<Live> live_;
    std::vector<Pending> pending_;
    uint64_t knownPendingBytes_ = 0;
    uint64_t budgetBytes_ = uint64_t(128) * 1024 * 1024;
};

} // namespace aiverse::editor
