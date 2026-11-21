// Shared, genre-neutral GPU shader/pipeline retirement owner.
// Pipeline cache invalidation must not destroy programs referenced by
// submitted Metal/Vulkan/GL work. No frame-count retirement shortcuts.
#pragma once
#include "AsyncResourceRetirementQueue.h"
#include "RenderDeviceLifetime.h"
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

struct GpuProgramBatch final {
    std::vector<gfx::PipelineHandle> pipelines;
    std::vector<gfx::ShaderHandle> shaders;
    bool empty() const noexcept {
        return pipelines.empty() && shaders.empty();
    }
};

class GpuProgramRetirement final {
public:
    using Queue=AsyncResourceRetirementQueue<GpuProgramBatch>;
    using DiagnosticObserver=Queue::DiagnosticObserver;
    GpuProgramRetirement(gfx::Device& device,
                         RenderDeviceLifetime::Lease lease={})
        :device_(device),lease_(std::move(lease)) {
        if(!deviceValid())
            throw std::logic_error("GPU program retire owner: expired device epoch");
    }
    GpuProgramRetirement(const GpuProgramRetirement&)=delete;
    GpuProgramRetirement& operator=(const GpuProgramRetirement&)=delete;

    void setRetirementDiagnosticObserver(DiagnosticObserver observer) {
        queue_.setDiagnosticObserver(std::move(observer));
    }
    // Must be invoked on the device owner thread. Never destroy a shader
    // while pending pipelines referencing it may be in submitted work.
    void retire(GpuProgramBatch&& programs) noexcept {
        if(programs.empty())return;
        if(!deviceValid()){++orphanBatches_;return;}
        try {
            queue_.enqueueFrom(programs);
        }catch(...) {
            ++enqueueFailures_;
            try {
                if(device_.waitForResourceIdle())
                    destroy(programs);
                else ++orphanBatches_;
            }catch(...) {++orphanBatches_;}
        }
    }
    bool collect() {
        if(!deviceValid())return false;
        return queue_.collect(
            [&]{return device_.beginResourceRetirement();},
            [&](uint64_t token){return device_.resourceRetirementComplete(token);},
            [&](uint64_t token){
                if(device_.resourceRetirementStatus(token)==
                        gfx::ResourceRetirementStatus::Complete)
                    device_.endResourceRetirement(token);
                else device_.abandonRetirementTokenAfterIdle(token);
            },
            [&]{return device_.waitForResourceIdle();},
            [&](GpuProgramBatch& programs){destroy(programs);},
            [&](uint64_t token){
                return device_.resourceRetirementStatus(token)==
                       gfx::ResourceRetirementStatus::Failed;
            });
    }
    bool drain() {
        if(!deviceValid())return queue_.empty() && orphanBatches_==0;
        return queue_.drain(
            [&]{return device_.waitForResourceIdle();},
            [&](uint64_t token){
                if(token)device_.abandonRetirementTokenAfterIdle(token);
            },
            [&](GpuProgramBatch& programs){destroy(programs);});
    }
    size_t pendingBatches() const noexcept{return queue_.pending();}
    uint64_t enqueueFailures() const noexcept{return enqueueFailures_;}
    uint64_t orphanBatches() const noexcept{return orphanBatches_;}
private:
    bool deviceValid() const noexcept {
        return !lease_.device() || lease_.validFor(device_);
    }
    void destroy(GpuProgramBatch& programs) {
        // Pipeline destruction first: pipelines borrow shader programs.
        for(const auto pipeline:programs.pipelines)
            if(pipeline.valid())device_.destroyPipeline(pipeline);
        for(const auto shader:programs.shaders)
            if(shader.valid())device_.destroyShader(shader);
        programs.pipelines.clear();
        programs.shaders.clear();
    }
    gfx::Device& device_;
    RenderDeviceLifetime::Lease lease_;
    Queue queue_;
    uint64_t enqueueFailures_=0,orphanBatches_=0;
};

} // namespace aiverse::render
