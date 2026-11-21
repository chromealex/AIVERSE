// AIVERSE render: non-owning, genre-neutral resource owner aggregation.
// Count resource allocations once by owner, Model identity, and shared
// geometry page identity across multiple native project hosts.
//
// Caller guarantees device-thread access and that every host belongs to
// the supplied gfx::Device. This class DOES NOT claim driver VRAM bytes,
// destroy GPU resources, wait on GPU fences, or own the registered caches.
#pragma once
#include "GlobalGeometryPool.h"
#include "ModelResourceCache.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace aiverse::render {

enum class RenderResourceHealth : uint8_t {
    Ready, PendingGpuRetirement, RetirementFenceFailed, DeviceUnhealthy
};

struct RenderResourceGroupStats {
    size_t uniqueOwners=0;
    size_t residentModels=0,retiredModels=0;
    size_t uniqueTextureOwners=0;
    size_t uniqueTextures=0,compressedTextures=0;
    uint64_t residentModelKnownBytes=0,retiredModelKnownBytes=0;
    uint64_t textureOwnerRgba8EquivalentBytes=0;
    // Device page capacity from the exact set of referenced pages. These
    // bytes must NOT be counted a second time for every Model or host.
    GeometryPoolReferencedStats geometryPages;
    GeometryPoolStats deviceGeometryPool;
    uint64_t pendingGpuPolls=0,asyncTokenFallbacks=0;
    uint64_t idleFallbackFailures=0;
    uint64_t failedFenceBatches=0;
    size_t unresolvedFailedFences=0,expiredRegistrations=0;
    RenderResourceHealth health=RenderResourceHealth::Ready;
    // A source/format/layout estimate of owned allocations, not a VRAM
    // budget or total GPU heap consumption.
    uint64_t estimatedKnownBytes() const noexcept {
        const auto max=std::numeric_limits<uint64_t>::max();
        auto add=[](uint64_t lhs,uint64_t rhs) {
            return rhs>max-lhs?max:lhs+rhs;
        };
        return add(add(add(residentModelKnownBytes,retiredModelKnownBytes),
                       textureOwnerRgba8EquivalentBytes),
                   uint64_t(geometryPages.allocatedBytes));
    }
};

class RenderResourceOwnerGroup final {
    struct ScopedState {
        std::atomic<RenderResourceOwnerGroup*> owner{nullptr};
        std::thread::id thread;
        std::atomic<size_t> wrongThreadReleases{0};
    };
public:
    // Scoped registration is the recommended path for GUI/native hosts.
    // The returned lease never owns or extends a ModelResourceCache.
    class ScopedRegistration final {
        friend class RenderResourceOwnerGroup;
    public:
        ScopedRegistration()=default;
        ScopedRegistration(const ScopedRegistration&)=delete;
        ScopedRegistration& operator=(const ScopedRegistration&)=delete;
        ScopedRegistration(ScopedRegistration&& other) noexcept
            :state_(std::move(other.state_)),id_(std::exchange(other.id_,0)) {}
        ScopedRegistration& operator=(ScopedRegistration&& other) noexcept {
            if(this!=&other) {
                reset();
                state_=std::move(other.state_);
                id_=std::exchange(other.id_,0);
            }
            return *this;
        }
        ~ScopedRegistration(){reset();}
        void reset() noexcept {
            if(!id_)return;
            if(auto state=state_.lock()) {
                auto* owner=state->owner.load(std::memory_order_acquire);
                if(std::this_thread::get_id()==state->thread && owner)
                    owner->unregister(id_);
                else if(owner)
                    state->wrongThreadReleases.fetch_add(1,std::memory_order_relaxed);
            }
            id_=0;
            state_.reset();
        }
        bool valid() const noexcept {return id_!=0 && !state_.expired();}
    private:
        ScopedRegistration(std::weak_ptr<ScopedState> state,uint64_t id)
            :state_(std::move(state)),id_(id){}
        std::weak_ptr<ScopedState> state_;
        uint64_t id_=0;
    };
    RenderResourceOwnerGroup()
        :ownerThread_(std::this_thread::get_id()),
         scopedState_(std::make_shared<ScopedState>()) {
        scopedState_->owner.store(this,std::memory_order_release);
        scopedState_->thread=ownerThread_;
    }
    RenderResourceOwnerGroup(const RenderResourceOwnerGroup&)=delete;
    RenderResourceOwnerGroup& operator=(const RenderResourceOwnerGroup&)=delete;
    RenderResourceOwnerGroup(RenderResourceOwnerGroup&&)=delete;
    RenderResourceOwnerGroup& operator=(RenderResourceOwnerGroup&&)=delete;
    ~RenderResourceOwnerGroup(){
        scopedState_->owner.store(nullptr,std::memory_order_release);
    }

    // Explicit binding: both the group and a cache are confined to one
    // gfx Device and one caller thread. No new asset may be inserted on
    // another device after an owner has been registered here.
    ScopedRegistration registerScoped(gfx::Device& device,
                                      const ModelResourceCache& cache) {
        checkThread();
        if(boundDevice_ && boundDevice_!=&device)
            throw std::logic_error("RenderResourceOwnerGroup device mismatch");
        cache.bindResourceDevice(device);
        boundDevice_=&device;
        const uint64_t id=nextRegistration_++;
        owners_.push_back({&cache,cache.lifetimeToken(),&device,id,{}});
        return ScopedRegistration(scopedState_,id);
    }

    // Generation-aware registration. Even if a device gets recreated at
    // the exact same address, a different host epoch cannot reuse stale
    // live Model resources, View references or GPU retirement markers.
    ScopedRegistration registerScoped(
            const RenderDeviceLifetime::Lease& lease,
            const ModelResourceCache& cache) {
        checkThread();
        if(!lease.alive() || !lease.device() ||
           !lease.validFor(*lease.device()))
            throw std::logic_error("RenderResourceOwnerGroup invalid GPU epoch");
        if(boundDevice_ && boundDevice_!=lease.device())
            throw std::logic_error("RenderResourceOwnerGroup device mismatch");
        if(boundEpoch_!=0 && boundEpoch_!=lease.generation())
            throw std::logic_error("RenderResourceOwnerGroup device epoch mismatch");
        cache.bindResourceDevice(*lease.device(),lease);
        boundDevice_=lease.device();
        boundEpoch_=lease.generation();
        const uint64_t id=nextRegistration_++;
        owners_.push_back({&cache,cache.lifetimeToken(),
                           lease.device(),id,lease});
        return ScopedRegistration(scopedState_,id);
    }

    // Legacy CPU-only read access. For device snapshots, prefer the scoped
    // device-bound registration above. This remains for old native tests.
    void add(const ModelResourceCache& cache) {
        checkThread();
        if(boundDevice_)cache.bindResourceDevice(*boundDevice_);
        for(const auto& entry:owners_)
            if(!entry.lifetime.expired() && entry.cache==&cache)return;
        owners_.push_back({&cache,cache.lifetimeToken(),boundDevice_,nextRegistration_++,{}});
    }
    void clear() {checkThread();owners_.clear();}
    size_t size() const {
        checkThread();
        size_t alive=0;
        for(const auto& entry:owners_)
            if(!entry.lifetime.expired())++alive;
        return alive;
    }
    size_t wrongThreadReleaseAttempts() const noexcept {
        return scopedState_->wrongThreadReleases.load(std::memory_order_relaxed);
    }

    void setEstimatedBudgetBytes(uint64_t bytes) {
        checkThread();
        budgetBytes_=bytes;
    }
    uint64_t estimatedBudgetBytes() const {
        checkThread();
        return budgetBytes_;
    }
    bool mayAdmitNewSources(const RenderResourceGroupStats& stats) const noexcept {
        return stats.health!=RenderResourceHealth::DeviceUnhealthy &&
               stats.health!=RenderResourceHealth::RetirementFenceFailed &&
               (budgetBytes_==0 || stats.estimatedKnownBytes()<budgetBytes_);
    }

    // CPU-only accounting, suitable for native project diagnostics and
    // deterministic regression fixtures without constructing a gfx Device.
    // Geometry-page capacity requires the device-level snapshot() below.
    RenderResourceGroupStats snapshotOwners(bool gpuHealthy=true) const {
        checkThread();
        RenderResourceGroupStats stats;
        stats.uniqueOwners=size();
        stats.expiredRegistrations=owners_.size()-stats.uniqueOwners;
        stats.health=gpuHealthy ? RenderResourceHealth::Ready:
                                  RenderResourceHealth::DeviceUnhealthy;
        std::set<const Model*> visitedModels;
        std::set<const ModelTextureCache*> visitedTextureOwners;
        const auto max=std::numeric_limits<uint64_t>::max();
        auto add=[](uint64_t& dst,uint64_t amount) {
            dst=amount>max-dst?max:dst+amount;
        };
        for(const auto& entry:owners_) {
            // Never dereference a destroyed cache even if the original
            // address has been reused by a new owner.
            if(entry.lifetime.expired())continue;
            const ModelResourceCache* owner=entry.cache;
            const auto retired=owner->retirementStats();
            if(retired.pendingResources &&
               stats.health==RenderResourceHealth::Ready)
                stats.health=RenderResourceHealth::PendingGpuRetirement;
            add(stats.pendingGpuPolls,retired.pendingPolls);
            add(stats.asyncTokenFallbacks,retired.tokenFailures);
            add(stats.idleFallbackFailures,retired.idleFallbackFailures);
            add(stats.failedFenceBatches,retired.failedFenceBatches);
            stats.unresolvedFailedFences+=retired.unresolvedFailedFences;
            if(retired.unresolvedFailedFences &&
               stats.health!=RenderResourceHealth::DeviceUnhealthy)
                stats.health=RenderResourceHealth::RetirementFenceFailed;
            owner->forEachOwnedModel([&](const Model& model,bool isRetired) {
                if(!visitedModels.insert(&model).second)return;
                const auto estimate=estimateModelResident(model);
                if(isRetired) {
                    ++stats.retiredModels;
                    add(stats.retiredModelKnownBytes,estimate.knownBytes());
                } else {
                    ++stats.residentModels;
                    add(stats.residentModelKnownBytes,estimate.knownBytes());
                }
            });
            const auto textures=owner->textures();
            if(textures && visitedTextureOwners.insert(textures.get()).second) {
                ++stats.uniqueTextureOwners;
                const auto estimate=textures->gpuOwnerEstimate();
                stats.uniqueTextures+=estimate.uniqueTextures;
                stats.compressedTextures+=estimate.compressedTextures;
                add(stats.textureOwnerRgba8EquivalentBytes,
                    estimate.rgba8BaseMipEquivalentBytes);
            }
        }
        return stats;
    }

    // Device-owner-thread snapshot: enrich owner accounting with geometry
    // pages and backend health. No GPU submissions or fences are issued.
    // The input caches must belong to the supplied gfx Device.
    RenderResourceGroupStats snapshot(gfx::Device& device) const {
        checkThread();
        if(boundEpoch_!=0 &&
           !std::any_of(owners_.begin(),owners_.end(),
               [&](const Registration& entry) {
                   return !entry.lifetime.expired() &&
                       entry.epoch.generation()==boundEpoch_ &&
                       entry.epoch.validFor(device);
               }))
            throw std::logic_error("RenderResourceOwnerGroup expired GPU epoch");
        if(boundDevice_ && boundDevice_!=&device)
            throw std::logic_error("RenderResourceOwnerGroup snapshot device mismatch");
        for(const auto& entry:owners_) {
            if(entry.lifetime.expired() || !entry.device)continue;
            if(entry.device!=&device ||
               (entry.epoch.generation()!=0 && !entry.epoch.validFor(device)) ||
               !entry.cache->boundToResourceDevice(device))
                throw std::logic_error(
                    "RenderResourceOwnerGroup stale or mixed-device registration");
        }
        auto stats=snapshotOwners(device.gpuHealthy());
        std::vector<const Model*> modelsForPool;
        for(const auto& entry:owners_) {
            // Never dereference a destroyed cache even if the original
            // address has been reused by a new owner.
            if(entry.lifetime.expired())continue;
            const ModelResourceCache* owner=entry.cache;
            owner->forEachOwnedModel([&](const Model& model,bool) {
                if(model.geometryPoolPage)modelsForPool.push_back(&model);
            });
        }
        stats.geometryPages=geometryPoolReferencedStats(
            device,std::span<const Model* const>(
                modelsForPool.data(),modelsForPool.size()));
        stats.deviceGeometryPool=geometryPoolStats(device);
        return stats;
    }
private:
    void checkThread() const {
        if(std::this_thread::get_id()!=ownerThread_)
            throw std::logic_error("RenderResourceOwnerGroup wrong owner thread");
    }
    struct Registration {
        const ModelResourceCache* cache=nullptr;
        std::weak_ptr<const void> lifetime;
        const gfx::Device* device=nullptr;
        uint64_t id=0;
        RenderDeviceLifetime::Lease epoch;
    };
    void unregister(uint64_t id) {
        checkThread();
        owners_.erase(std::remove_if(owners_.begin(),owners_.end(),
            [id](const Registration& entry){return entry.id==id;}),
            owners_.end());
    }
    std::vector<Registration> owners_;
    std::thread::id ownerThread_;
    std::shared_ptr<ScopedState> scopedState_;
    gfx::Device* boundDevice_=nullptr;
    uint64_t nextRegistration_=1,budgetBytes_=0,boundEpoch_=0;
};

} // namespace aiverse::render
