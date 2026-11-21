// VIVID whole-human GPU retirement: model streams, images and actor buffers
// remain owned until a verified GPU completion token or successful idle drain.
// Never delete a Model before all GPU draws referencing its vertex stream end.
#pragma once
#include "HumanPreview.h"
#include "GpuActorStream.h"
#include "CpuActorStream.h"
#include "engine/render/AsyncResourceRetirementQueue.h"
#include "engine/render/ModelResidentEstimate.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <aiverselib/motionweave/MotionWeave.h>
#include <memory>
#include <utility>

namespace aiverse::vivid {
class HumanRetirementQueue final {
public:
    struct Resident {
        HumanPreviewAsset human;
        std::unique_ptr<GpuActorStream> actor;
        std::unique_ptr<CpuActorStream> cpuActor;
        std::unique_ptr<World> world;
        std::shared_ptr<const render::AnimationGraph> graph;
    };
    using Resource=std::unique_ptr<Resident>;
    using Queue=render::AsyncResourceRetirementQueue<Resource>;

    explicit HumanRetirementQueue(gfx::Device& device):device_(device){}
    HumanRetirementQueue(const HumanRetirementQueue&)=delete;
    HumanRetirementQueue& operator=(const HumanRetirementQueue&)=delete;

    // Estimates are from source geometry and known actor buffers, not
    // driver-accounted VRAM. Shared GPU assets may be held by multiple actors;
    // only per-instance write buffers are included in this estimate.
    struct Admission {
        std::size_t maxPending=3;
        std::uint64_t maxEstimatedBytes=512ull*1024ull*1024ull;
    };
    struct Residency {
        std::size_t pending=0;
        std::uint64_t estimatedBytes=0;
        std::size_t highWatermark=0;
        std::uint64_t blockedPublications=0;
    };
    void setAdmission(Admission limits) noexcept {limits_=limits;}
    Admission admission() const noexcept {return limits_;}
    static std::uint64_t estimatedCurrentBytes(
        const HumanPreviewAsset& human,
        const GpuActorStream* actor,
        const CpuActorStream* cpuActor) noexcept {
        auto add=[](std::uint64_t& total,std::uint64_t amount) {
            const auto max=std::numeric_limits<std::uint64_t>::max();
            total=amount>max-total?max:total+amount;
        };
        std::uint64_t total=0;
        if(human.head)
            add(total,render::estimateModelResident(*human.head).knownBytes());
        if(human.hair)
            add(total,render::estimateModelResident(*human.hair).knownBytes());
        if(actor) {
            const auto count=actor->vertexCount();
            if(count>std::numeric_limits<std::uint64_t>::max()/
                     (sizeof(GpuMorphVertex)+sizeof(render::ModelVertex)))
                return std::numeric_limits<std::uint64_t>::max();
            add(total,std::uint64_t(count)*
                (sizeof(GpuMorphVertex)+sizeof(render::ModelVertex)));
        }
        if(cpuActor && human.head)
            add(total,std::uint64_t(human.head->vertices.size())*
                sizeof(render::ModelVertex));
        return total;
    }
    static std::uint64_t estimatedBytes(const Resident& resident) noexcept {
        return estimatedCurrentBytes(resident.human,resident.actor.get(),
                                     resident.cpuActor.get());
    }
    Residency residency() const noexcept {
        Residency result;
        result.pending=queue_.pending();
        result.highWatermark=queue_.stats().highWatermark;
        result.blockedPublications=blocked_;
        result.estimatedBytes=queue_.estimatedPendingBytes(
            [](const Resource& item) {
                return item?estimatedBytes(*item):std::uint64_t(0);
            });
        return result;
    }
    // Reject additional full-character publication before allocating more
    // RHI resources. Pending fences are NEVER treated as completed to
    // satisfy these budgets, even if they have been pending many frames.
    bool canPublish(std::uint64_t nextRetiredBytes) noexcept {
        const auto state=residency();
        const bool slots=limits_.maxPending!=0 &&
            state.pending<limits_.maxPending;
        const bool bytes=nextRetiredBytes<=limits_.maxEstimatedBytes &&
            state.estimatedBytes<=limits_.maxEstimatedBytes-nextRetiredBytes;
        if(!slots || !bytes) {
            ++blocked_;
            return false;
        }
        return true;
    }
    // Reserve pending slot *before* the caller transfers old live owners.
    // On allocation failure no live Model/World/actor pointer has moved.
    Resident* enqueueEmpty() {
        auto value=std::make_unique<Resident>();
        auto* owned=value.get();
        queue_.enqueueFrom(value);
        return owned;
    }
    // Call collect() at the next frame boundary AFTER the previous endFrame.
    // Returns false for pending or failed tokens; never assumes completion.
    bool collect() {
        return queue_.collect(
            [&](){return device_.beginResourceRetirement();},
            [&](std::uint64_t token){return device_.resourceRetirementComplete(token);},
            [&](std::uint64_t token){
                if(device_.resourceRetirementStatus(token)==
                   gfx::ResourceRetirementStatus::Complete)
                    device_.endResourceRetirement(token);
                else device_.abandonRetirementTokenAfterIdle(token);
            },
            [&](){return device_.waitForResourceIdle();},
            [&](Resource& resource){release(resource);},
            [&](std::uint64_t token){
                return device_.resourceRetirementStatus(token)==
                    gfx::ResourceRetirementStatus::Failed;
            });
    }
    bool drain() {
        return queue_.drain(
            [&](){return device_.waitForResourceIdle();},
            [&](std::uint64_t token){
                device_.abandonRetirementTokenAfterIdle(token);
            },
            [&](Resource& resource){release(resource);});
    }
    std::size_t pending() const noexcept {return queue_.pending();}
    Queue::Stats stats() const noexcept {return queue_.stats();}
private:
    void release(Resource& resource) noexcept {
        if(!resource)return;
        // MotionWeave still borrows Model; release it before the Model's
        // native buffers/images are retired. The token proves GPU completion.
        resource->world.reset();
        resource->graph.reset();
        if(resource->actor) {
            resource->actor->releaseAfterGpuCompletion();
            resource->actor.reset();
        }
        if(resource->cpuActor) {
            resource->cpuActor->releaseAfterGpuCompletion();
            resource->cpuActor.reset();
        }
        if(resource->human.hair)
            render::releaseModel(device_,*resource->human.hair);
        if(resource->human.head)
            render::releaseModel(device_,*resource->human.head);
        resource.reset();
    }
    gfx::Device& device_;
    Queue queue_;
    Admission limits_{};
    std::uint64_t blocked_=0;
};
} // namespace aiverse::vivid
