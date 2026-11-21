#pragma once
// AIVERSE Stage 37: genre-independent fence-backed retirement of GPU resource
// handles and complete owner bundles. This is a PRESENTATION/RENDER lifetime
// facility only; it never updates ECS or treats a CPU frame number as a fence.
//
// A device queue submission token comes ONLY from gfx::Device::gpuSubmissionFence().
// A zero submission token is quarantined until a genuine GPU idle drain.
// Release callbacks execute on the graphics owner thread, NEVER on a Metal
// completion thread and never before the last referencing submission completes.
#include "engine/gfx/Gfx.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#include <utility>

namespace aiverse::render {
class GpuResourceRetirement {
public:
    // Never allow a pending owner-bundle callback to be destroyed while the
    // device may already be gone: the capture might own C++ objects whose
    // destructors call Device::destroy*. Explicit collect/drain is preferred.
    ~GpuResourceRetirement() { abandonAll(); }
    GpuResourceRetirement() = default;
    GpuResourceRetirement(const GpuResourceRetirement&) = delete;
    GpuResourceRetirement& operator=(const GpuResourceRetirement&) = delete;
    enum class Kind : uint8_t {
        Pipeline, Compute, Buffer, Texture, Shader, Bundle
    };
    struct Stats {
        size_t pipelines = 0, computes = 0, buffers = 0;
        size_t textures = 0, shaders = 0, bundles = 0;
        size_t fenced = 0, unfenced = 0;
        uint64_t knownPendingBytes = 0;  // optional, lower bound
        size_t bundlesWithoutByteEstimate = 0; // ShaderGraph/Portrait owner archives
        size_t texturesWithoutByteEstimate = 0;
        size_t buffersWithoutByteEstimate = 0;
        uint64_t completedFence = 0;
        size_t abandonedHandles = 0, abandonedBundles = 0;
    };

    void retire(gfx::Device& device, gfx::PipelineHandle& h) {
        if (!h.valid()) return;
        enqueue(device,Kind::Pipeline,h.id,0,{});
        h = {};
    }
    void retire(gfx::Device& device, gfx::ShaderHandle& h) {
        if (!h.valid()) return;
        enqueue(device,Kind::Shader,h.id,0,{});
        h = {};
    }
    void retire(gfx::Device& device, gfx::BufferHandle& h,
                uint64_t knownBytes = 0) {
        if (!h.valid()) return;
        if (!knownBytes) {
            gfx::BufferInfo info{};
            if (device.bufferInfo(h,info))
                knownBytes = uint64_t(info.logicalBytes);
        }
        enqueue(device,Kind::Buffer,h.id,knownBytes,{});
        h = {};
    }
    void retire(gfx::Device& device, gfx::TextureHandle& h,
                uint64_t knownBytes = 0) {
        if (!h.valid()) return;
        if (!knownBytes) {
            gfx::TextureInfo info{};
            if (device.textureInfo(h,info) && info.width > 0 && info.height > 0) {
                const uint64_t bytesPerPixel =
                    info.format == gfx::PixelFormat::R8 ? 1 :
                    info.format == gfx::PixelFormat::RGBA16F ? 8 :
                    info.format == gfx::PixelFormat::RGBA32F ? 16 : 4;
                knownBytes = uint64_t(info.width)*uint64_t(info.height)*bytesPerPixel;
            }
        }
        enqueue(device,Kind::Texture,h.id,knownBytes,{});
        h = {};
    }
    void retire(gfx::Device& device, gfx::ComputeHandle& h) {
        if (!h.valid()) return;
        enqueue(device,Kind::Compute,h.id,0,{});
        h = {};
    }

    // Use for a complete owner containing resources and interdependencies
    // (e.g. ShaderGraphPreview -> Portrait/ParticleSystem/Prisma). Callback
    // MUST own its captured state and release the bundle only when invoked.
    // It must not access an already-destroyed window/Editor or other owner.
    void retireOwned(gfx::Device& device, std::function<void()> cleanup,
                     uint64_t knownBytes = 0) {
        if (!cleanup) return;
        enqueue(device,Kind::Bundle,0,knownBytes,std::move(cleanup));
    }

    size_t collect(gfx::Device& device) {
        if (!device.gpuHealthy()) {
            abandonDevice(device);
            return 0;
        }
        const uint64_t completed = device.gpuCompletedFence();
        if (!completed) return 0;
        return releaseMatching(device,completed,false);
    }

    // Call only when the caller has proven all previously-submitted work
    // using these resources is GPU-complete. Must not execute inside a pass.
    size_t drainAfterGpuIdle(gfx::Device& device) {
        if (!device.gpuHealthy()) {
            abandonDevice(device);
            return 0;
        }
        return releaseMatching(device,0,true);
    }

    // Device lost / shutdown without a verifiable GPU completion. Discard
    // raw driver handles WITHOUT issuing a GPU destroy. Bundle callbacks
    // cannot be simply destroyed: their captured C++ objects may call the
    // dead Device in their destructors. Quarantine the captures in a
    // process-lifetime no-dtor sink instead. This intentionally leaks
    // orphaned CPU data until process exit, but avoids a use-after-free.
    // Call before the graphics Device is destroyed if idle could not be
    // verified. NEVER use this as ordinary memory-pressure collection.
    size_t abandonDevice(gfx::Device& device) {
        size_t n = 0;
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (it->device != &device) { ++it; continue; }
            if (it->kind == Kind::Bundle && it->cleanup) {
                orphanedBundles().push_back(std::move(it->cleanup));
                ++abandonedBundles_;
            } else {
                ++abandonedHandles_;
            }
            it = pending_.erase(it);
            ++n;
        }
        return n;
    }

    Stats stats(const gfx::Device& device) const {
        Stats out;
        out.completedFence = device.gpuCompletedFence();
        out.abandonedHandles = abandonedHandles_;
        out.abandonedBundles = abandonedBundles_;
        for (const auto& p : pending_) {
            if (p.device != &device) continue;
            if (p.token) ++out.fenced;
            else ++out.unfenced;
            out.knownPendingBytes += p.knownBytes;
            switch (p.kind) {
                case Kind::Pipeline: ++out.pipelines; break;
                case Kind::Shader: ++out.shaders; break;
                case Kind::Buffer:
                    ++out.buffers;
                    if (!p.knownBytes) ++out.buffersWithoutByteEstimate;
                    break;
                case Kind::Texture:
                    ++out.textures;
                    if (!p.knownBytes) ++out.texturesWithoutByteEstimate;
                    break;
                case Kind::Compute: ++out.computes; break;
                case Kind::Bundle:
                    ++out.bundles;
                    if (!p.knownBytes) ++out.bundlesWithoutByteEstimate;
                    break;
            }
        }
        return out;
    }
private:
    static std::vector<std::function<void()>>& orphanedBundles() {
        static auto* sink = new std::vector<std::function<void()>>();
        return *sink; // no destructor: callbacks can refer to dead GPU Device
    }
    void abandonAll() {
        // Owner destroyed while GPU work is unconfirmed. Preserve bundle
        // captures inert rather than running their unsafe destructors.
        for (auto& p : pending_)
            if (p.kind == Kind::Bundle && p.cleanup)
                orphanedBundles().push_back(std::move(p.cleanup));
        pending_.clear();
    }
    size_t abandonedHandles_ = 0, abandonedBundles_ = 0;
    struct Pending {
        gfx::Device* device = nullptr;
        uint64_t token = 0;
        Kind kind = Kind::Buffer;
        uint32_t handle = 0;
        uint64_t knownBytes = 0;
        std::function<void()> cleanup;
    };
    std::vector<Pending> pending_;

    void enqueue(gfx::Device& device,Kind kind,uint32_t handle,
                 uint64_t bytes,std::function<void()> fn) {
        pending_.push_back({&device,device.gpuSubmissionFence(),
                            kind,handle,bytes,std::move(fn)});
    }

    size_t releaseMatching(gfx::Device& device,uint64_t completed,bool idle) {
        std::vector<Pending> ready;
        ready.reserve(pending_.size());
        for (auto it=pending_.begin();it!=pending_.end();) {
            if (it->device == &device &&
                (idle || (it->token && it->token <= completed))) {
                ready.push_back(std::move(*it));
                it=pending_.erase(it);
            } else ++it;
        }
        // Pipelines depend on shaders. Release all ready pipelines first,
        // then compute, buffers/textures and shaders. Bundles manage their
        // own internal dependency order after their completion fence.
        for (Kind kind : {Kind::Pipeline,Kind::Compute,Kind::Buffer,
                          Kind::Texture,Kind::Shader,Kind::Bundle})
            for (auto& p : ready) {
                if (p.kind != kind) continue;
                switch (kind) {
                    case Kind::Pipeline:
                        device.destroyPipeline(gfx::PipelineHandle{p.handle});break;
                    case Kind::Shader:
                        device.destroyShader(gfx::ShaderHandle{p.handle});break;
                    case Kind::Buffer:
                        device.destroyBuffer(gfx::BufferHandle{p.handle});break;
                    case Kind::Texture:
                        device.destroyTexture(gfx::TextureHandle{p.handle});break;
                    case Kind::Compute:
                        device.destroyCompute(gfx::ComputeHandle{p.handle});break;
                    case Kind::Bundle:
                        if (p.cleanup) p.cleanup();break;
                }
            }
        return ready.size();
    }
};
} // namespace aiverse::render
