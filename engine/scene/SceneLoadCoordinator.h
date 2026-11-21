// Async CPU scene preparation + explicit owner-thread ECS activation.
// The worker does ONLY manifest read/parse. It never mutates World, GPU,
// physics, SceneHierarchy, simulation RNG, or scene authoritative state.
#pragma once

#include "SceneAsset.h"
#include "SceneRuntime.h"
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::scene {

struct SceneTicket {
    SceneId scene{};
    uint64_t epoch = 0;  // Host-only rollback epoch; NOT part of deterministic ECS state.
    uint64_t serial = 0; // Unique coordinator job across timeline rewinds.
    constexpr bool valid() const { return scene.valid() && epoch && serial; }
    constexpr bool operator==(const SceneTicket&) const = default;
};

enum class SceneLoadStatus : uint8_t {
    Unknown, Preparing, Ready, Failed, Cancelled, Committed, Unloaded
};

struct SceneResourceAdapter {
    // Both callbacks execute ONLY on the owner thread, at the commit/unload barrier.
    // acquire() is called once when resource refcount goes from 0 to 1.
    // release() is called once when refcount reaches 0 (must not throw).
    std::function<bool(const std::string&, std::string&)> acquire;
    std::function<void(const std::string&)> release;
};

class SceneLoadCoordinator {
public:
    using ReadAsset = std::function<bool(const std::string&, std::string&, std::string&)>;
    using SpawnPrefab = std::function<bool(World&, Entity, const SceneObjectAsset&,
                                           std::string&)>;

    SceneLoadCoordinator(World& world, ReadAsset reader, SceneResourceAdapter resources,
                         SpawnPrefab spawn, SceneHierarchy* hierarchy = nullptr,
                         size_t maxPreparing = 4)
        : scenes_(world), world_(world), reader_(std::move(reader)),
          resources_(std::move(resources)), spawn_(std::move(spawn)),
          hierarchy_(hierarchy), maxPreparing_(maxPreparing) {}

    // Release presentation leases owned by this coordinator on the owner thread.
    // Pending std::async futures join when jobs_ is destroyed; reader callbacks
    // must not depend on an external object already being destroyed.
    ~SceneLoadCoordinator() {
        for(const auto& [id, resident] : resident_) {
            (void)id;
            release(resident.leases);
        }
    }

    SceneLoadCoordinator(const SceneLoadCoordinator&) = delete;
    SceneLoadCoordinator& operator=(const SceneLoadCoordinator&) = delete;

    // At a simulation/structural barrier. Start asynchronous manifest read,
    // leave the scene in Loading. The host must not activate on worker timing.
    SceneTicket request(std::string asset, LoadMode mode = LoadMode::Additive,
                        bool persistent = false) {
        if(!reader_ || inFlight() >= maxPreparing_ ||
           serial_ == std::numeric_limits<uint64_t>::max())
            return {};
        const SceneId id = scenes_.beginLoad(asset, mode, persistent);
        if(!id.valid()) return {};
        const SceneTicket ticket{id, epoch_, ++serial_};
        if(!schedule(ticket, std::move(asset), false)) {
            scenes_.cancel(id, hierarchy_);
            return {};
        }
        return ticket;
    }

    // Host-only poll; never mutates ECS. Status Ready is non-authoritative.
    // Await a deterministic command/tick barrier before calling commit().
    void poll() {
        for(auto it = jobs_.begin(); it != jobs_.end();) {
            Job& job = it->second;
            if(job.status != SceneLoadStatus::Preparing &&
               job.status != SceneLoadStatus::Cancelled) {
                ++it;
                continue;
            }
            if(!job.future.valid()) {
                if(job.status == SceneLoadStatus::Cancelled) it = jobs_.erase(it);
                else ++it;
                continue;
            }
            if(job.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            Prepared result;
            try { result = job.future.get(); }
            catch(const std::exception& e) { result.error = e.what(); }
            catch(...) { result.error = "unknown scene preparation exception"; }

            if(job.status == SceneLoadStatus::Cancelled ||
               job.ticket.epoch != epoch_) {
                it = jobs_.erase(it); // Retired async work cannot enter the restored world.
                continue;
            }
            if(!result.error.empty()) {
                // Poll is non-authoritative: leave SceneState untouched until
                // cancel()/commit() at the host's structural barrier.
                job.error = std::move(result.error);
                job.status = SceneLoadStatus::Failed;
            } else {
                job.manifest = std::move(result.asset);
                job.status = SceneLoadStatus::Ready;
            }
            ++it;
        }
        pruneTerminalJobs();
    }

    SceneLoadStatus status(SceneTicket ticket) const {
        const Job* job = lookup(ticket);
        return job ? job->status : SceneLoadStatus::Unknown;
    }
    const std::string& error(SceneTicket ticket) const {
        static const std::string empty;
        const Job* job = lookup(ticket);
        return job ? job->error : empty;
    }
    bool ready(SceneTicket ticket) const {
        return status(ticket) == SceneLoadStatus::Ready;
    }
    // Read-only prepared manifest for host-only prewarming of dependencies.
    // Invalidated by any mutating coordinator operation or rollback; never
    // expose this pointer to a background worker or authoritative gameplay.
    const SceneAsset* prepared(SceneTicket ticket) const {
        const Job* job = lookup(ticket);
        return job && job->status == SceneLoadStatus::Ready && job->manifest
            ? &*job->manifest : nullptr;
    }

    // Caller must be in a deterministic tick/structural barrier. In networked
    // games, the *command* to activate is canonical and all peers must be ready
    // before progressing the tick (GameModule::readyToStep gate).
    // On failure, no previous active scene is unloaded.
    bool commit(SceneTicket ticket) {
        Job* job = lookup(ticket);
        if(!job || job->status != SceneLoadStatus::Ready ||
           !job->manifest) return false;
        const SceneRecord* record = scenes_.record(ticket.scene);
        if(!record || record->asset != job->manifest->id ||
           (job->residencyOnly ? record->phase != ScenePhase::Active
                               : record->phase != ScenePhase::Loading)) {
            fail(*job, "scene state changed before activation");
            return false;
        }
        if(!job->residencyOnly && !spawn_ && !job->manifest->objects.empty()) {
            fail(*job, "scene has prefabs but no native prefab spawner");
            return false;
        }
        if(!job->residencyOnly && !hierarchy_) {
            for(const auto& object : job->manifest->objects)
                if(object.parent != 0) {
                    fail(*job, "parented prefab requires SceneHierarchy");
                    return false;
                }
        }

        std::vector<std::string> leases;
        std::string errorMessage;
        if(!acquire(*job->manifest, leases, errorMessage)) {
            fail(*job, errorMessage);
            return false;
        }

        if(!job->residencyOnly) {
            // Instantiate in canonical scene-local ID order after ALL asset
            // dependencies have been acquired. Callbacks may only mutate the
            // provided freshly-created entity, not global gameplay state.
            std::map<uint64_t, Entity> created;
            try {
                for(const auto& object : job->manifest->objects) {
                    Entity entity = scenes_.createEntity(ticket.scene);
                    if(!entity.valid()) {
                        errorMessage = "failed to create scene entity";
                        break;
                    }
                    created.emplace(object.id, entity);
                    if(spawn_ && !spawn_(world_, entity, object, errorMessage)) {
                        if(errorMessage.empty()) errorMessage = "prefab spawn rejected";
                        break;
                    }
                    const LocalTransform local = world_.has<LocalTransform>(entity)
                        ? *world_.tryGetConst<LocalTransform>(entity) : LocalTransform{};
                    if(hierarchy_ && !hierarchy_->attach(world_, entity, local)) {
                        errorMessage = "failed to attach scene hierarchy entity";
                        break;
                    }
                }
                if(errorMessage.empty() && hierarchy_) {
                    for(const auto& object : job->manifest->objects)
                        if(object.parent != 0 &&
                           !hierarchy_->setParent(world_, created.at(object.id),
                                                 created.at(object.parent))) {
                            errorMessage = "failed to link scene parent";
                            break;
                        }
                }
            } catch(const std::exception& e) {
                errorMessage = e.what();
            } catch(...) {
                errorMessage = "unknown prefab instantiation exception";
            }
            if(!errorMessage.empty()) {
                scenes_.cancel(ticket.scene, hierarchy_);
                release(leases);
                job->error = std::move(errorMessage);
                job->status = SceneLoadStatus::Failed;
                return false;
            }
        }

        // Collect old resource owners before SceneRuntime removes their
        // World records. Persistent scenes are deliberately retained.
        std::vector<uint64_t> discarded;
        if(!job->residencyOnly) {
            if(const SceneState* state = scenes_.stateView();
               state && state->pendingReplace == ticket.scene.value) {
                for(const auto& [id, existing] : state->scenes)
                    if(id != ticket.scene.value && !existing.persistent)
                        discarded.push_back(id);
            }
            if(!scenes_.activate(ticket.scene, hierarchy_)) {
                scenes_.cancel(ticket.scene, hierarchy_);
                release(leases);
                fail(*job, "scene runtime activation rejected");
                return false;
            }
        }

        // Refcounts are non-authoritative presentation/collision residency.
        // World SceneOwner/SceneState remain canonical across snapshots.
        resident_[ticket.scene.value] = Resident{job->manifest->id, std::move(leases)};
        for(uint64_t sceneId : discarded) {
            releaseResident(sceneId);
            markUnloaded(sceneId);
        }
        // The committed manifest is no longer needed; keep only the status
        // until pruneTerminalJobs() retires the record.
        job->manifest.reset();
        job->status = SceneLoadStatus::Committed;
        return true;
    }

    // Asynchronous cancellation is immediately visible in the ECS World;
    // a running worker is retained as a tombstone until poll() joins it.
    // This avoids blocking cancellation on std::future destruction.
    bool cancel(SceneTicket ticket) {
        Job* job = lookup(ticket);
        if(!job || (job->status != SceneLoadStatus::Preparing &&
                     job->status != SceneLoadStatus::Ready &&
                     job->status != SceneLoadStatus::Failed))
            return false;
        if(!job->residencyOnly) scenes_.cancel(ticket.scene, hierarchy_);
        job->manifest.reset();
        job->status = SceneLoadStatus::Cancelled;
        return true;
    }

    bool unload(SceneId id) {
        if(!scenes_.record(id)) return false;
        for(auto& [serial, job] : jobs_) {
            (void)serial;
            if(job.ticket.epoch == epoch_ && job.ticket.scene == id) {
                job.manifest.reset();
                if(job.status == SceneLoadStatus::Preparing)
                    job.status = SceneLoadStatus::Cancelled;
                else job.status = SceneLoadStatus::Unloaded;
            }
        }
        if(!scenes_.unload(id, hierarchy_)) return false;
        releaseResident(id.value);
        return true;
    }

    // Invoke AFTER Simulation::loadState / deserialize at a host barrier.
    // Discard all pre-rollback future results and invalidate their
    // SceneTickets. Derived presentation leases are diffed against the
    // restored SceneState: a resident scene that is still Active with the
    // same asset keeps its leases (no restream); every other lease is
    // released. Authoritative scene ECS state is NOT changed.
    // Use queueRestoredResidency() before resuming gameplay.
    void invalidateAfterRestore() {
        if(epoch_ == std::numeric_limits<uint64_t>::max()) {
            // Cannot ever alias a previous epoch, so abort explicitly.
            throw std::overflow_error("scene coordinator epoch exhausted");
        }
        ++epoch_;
        for(auto it = jobs_.begin(); it != jobs_.end();) {
            Job& job = it->second;
            // Old-epoch tickets are unreachable; only running workers stay
            // as tombstones until poll() joins them.
            if(!job.future.valid()) {
                it = jobs_.erase(it);
                continue;
            }
            job.manifest.reset();
            job.status = SceneLoadStatus::Cancelled;
            ++it;
        }
        const SceneState* state = scenes_.stateView();
        for(auto it = resident_.begin(); it != resident_.end();) {
            const SceneRecord* record = nullptr;
            if(state) {
                const auto found = state->scenes.find(it->first);
                if(found != state->scenes.end()) record = &found->second;
            }
            if(record && record->phase == ScenePhase::Active &&
               record->asset == it->second.asset) {
                ++it;
                continue;
            }
            release(it->second.leases);
            it = resident_.erase(it);
        }
    }

    // Non-authoritative restore after rollback / cold deserialize.
    // Active scene records that are not resident get a residency-only job
    // (no ECS entities are respawned). Loading scene records lost their job
    // with the old epoch; they get a full preparation job again so the host
    // can commit (or cancel) them, which also clears a restored
    // pendingReplace. The host must await/commit these tickets at its
    // deterministic barrier before its next relevant tick. Calls beyond the
    // worker cap leave records for a later call.
    std::vector<SceneTicket> queueRestoredResidency() {
        std::vector<SceneTicket> result;
        const SceneState* state = scenes_.stateView();
        if(!state) return result;
        for(const auto& [id, record] : state->scenes) {
            const bool residencyOnly = record.phase == ScenePhase::Active;
            if(residencyOnly ? (resident_.contains(id) || hasActiveResidencyJob(id))
                             : hasLiveLoadJob(id))
                continue;
            if(inFlight() >= maxPreparing_ ||
               serial_ == std::numeric_limits<uint64_t>::max())
                break;
            const SceneTicket ticket{{id}, epoch_, ++serial_};
            if(schedule(ticket, record.asset, residencyOnly))
                result.push_back(ticket);
        }
        return result;
    }

    // Terminal (Committed/Unloaded/settled Failed) job records retained for
    // status() queries; older ones are pruned by poll().
    static constexpr size_t kMaxTerminalJobs = 64;
    size_t jobCount() const { return jobs_.size(); }

    size_t residentSceneCount() const { return resident_.size(); }
    size_t sharedResourceCount() const { return refcounts_.size(); }
    size_t resourceRefCount(const std::string& key) const {
        const auto it = refcounts_.find(key);
        return it == refcounts_.end() ? 0 : it->second;
    }

private:
    struct Prepared {
        SceneAsset asset;
        std::string error;
    };
    struct Resident {
        std::string asset;
        std::vector<std::string> leases;
    };
    struct Job {
        SceneTicket ticket;
        SceneLoadStatus status = SceneLoadStatus::Preparing;
        bool residencyOnly = false;
        std::future<Prepared> future;
        std::optional<SceneAsset> manifest;
        std::string error;
    };

    bool schedule(SceneTicket ticket, std::string asset, bool residencyOnly) {
        try {
            const ReadAsset read = reader_;
            // No reference to this coordinator, World or SceneHierarchy is
            // captured by the worker, making late completion safe.
            auto future = std::async(std::launch::async,
                [read, asset = std::move(asset)]() -> Prepared {
                    Prepared result;
                    std::string source;
                    if(!read(asset, source, result.error)) {
                        if(result.error.empty()) result.error = "scene asset read failed";
                        return result;
                    }
                    if(source.size() > SceneAsset::kMaxManifestBytes) {
                        result.error = "scene manifest exceeds size limit";
                        return result;
                    }
                    try {
                        const Json json = Json::parse(source);
                        if(!SceneAsset::decode(json, result.asset, &result.error))
                            return result;
                        if(result.asset.id != asset)
                            result.error = "scene manifest id differs from requested asset";
                    } catch(const std::exception& e) { result.error = e.what(); }
                    return result;
                });
            Job job;
            job.ticket = ticket;
            job.residencyOnly = residencyOnly;
            job.future = std::move(future);
            jobs_.emplace(ticket.serial, std::move(job));
            return true;
        } catch(...) { return false; }
    }
    bool hasActiveResidencyJob(uint64_t sceneId) const {
        for(const auto& [serial, job] : jobs_) {
            (void)serial;
            if(job.ticket.epoch == epoch_ && job.ticket.scene.value == sceneId &&
               job.residencyOnly &&
               (job.status == SceneLoadStatus::Preparing ||
                job.status == SceneLoadStatus::Ready ||
                job.status == SceneLoadStatus::Committed)) return true;
        }
        return false;
    }
    bool hasLiveLoadJob(uint64_t sceneId) const {
        for(const auto& [serial, job] : jobs_) {
            (void)serial;
            if(job.ticket.epoch == epoch_ && job.ticket.scene.value == sceneId &&
               !job.residencyOnly &&
               (job.status == SceneLoadStatus::Preparing ||
                job.status == SceneLoadStatus::Ready ||
                job.status == SceneLoadStatus::Failed)) return true;
        }
        return false;
    }
    // A Failed full-load job whose Loading record still exists must remain
    // so the host can cancel() it; every other terminal job is prunable.
    bool terminal(const Job& job) const {
        if(job.future.valid()) return false;
        switch(job.status) {
            case SceneLoadStatus::Committed:
            case SceneLoadStatus::Unloaded:
                return true;
            case SceneLoadStatus::Failed: {
                if(job.residencyOnly || job.ticket.epoch != epoch_) return true;
                const SceneRecord* record = scenes_.record(job.ticket.scene);
                return !record || record->phase != ScenePhase::Loading;
            }
            default:
                return false;
        }
    }
    void pruneTerminalJobs() {
        size_t count = 0;
        for(const auto& [serial, job] : jobs_) {
            (void)serial;
            if(terminal(job)) ++count;
        }
        // jobs_ is ordered by serial: erase the oldest terminal records.
        for(auto it = jobs_.begin(); it != jobs_.end() && count > kMaxTerminalJobs;) {
            if(terminal(it->second)) {
                it = jobs_.erase(it);
                --count;
            } else ++it;
        }
    }
    size_t inFlight() const {
        size_t count = 0;
        for(const auto& [serial, job] : jobs_) {
            (void)serial;
            // Canceled/rollback-invalidated std::async work still runs
            // until joined by poll(). Count these futures to preserve the
            // actual worker cap during repeated cancels and rewinds.
            if(job.future.valid() &&
               (job.status == SceneLoadStatus::Preparing ||
                job.status == SceneLoadStatus::Cancelled)) ++count;
        }
        return count;
    }
    Job* lookup(SceneTicket ticket) {
        if(ticket.epoch != epoch_ || !ticket.valid()) return nullptr;
        const auto it = jobs_.find(ticket.serial);
        return it != jobs_.end() && it->second.ticket == ticket ? &it->second : nullptr;
    }
    const Job* lookup(SceneTicket ticket) const {
        if(ticket.epoch != epoch_ || !ticket.valid()) return nullptr;
        const auto it = jobs_.find(ticket.serial);
        return it != jobs_.end() && it->second.ticket == ticket ? &it->second : nullptr;
    }
    void fail(Job& job, std::string reason) {
        if(!job.residencyOnly) scenes_.cancel(job.ticket.scene, hierarchy_);
        job.manifest.reset();
        job.error = std::move(reason);
        job.status = SceneLoadStatus::Failed;
    }

    bool acquire(const SceneAsset& asset, std::vector<std::string>& leases,
                 std::string& error) {
        for(const std::string& dependency : asset.dependencies) {
            const auto it = refcounts_.find(dependency);
            if(it != refcounts_.end()) {
                ++it->second;
                leases.push_back(dependency);
                continue;
            }
            try {
                if(!resources_.acquire ||
                   !resources_.acquire(dependency, error)) {
                    if(error.empty()) error = "scene dependency acquisition failed: " + dependency;
                    release(leases);
                    leases.clear();
                    return false;
                }
            } catch(const std::exception& e) {
                error = e.what();
                release(leases);
                leases.clear();
                return false;
            } catch(...) {
                error = "unknown scene resource exception";
                release(leases);
                leases.clear();
                return false;
            }
            refcounts_[dependency] = 1;
            leases.push_back(dependency);
        }
        return true;
    }
    void release(const std::vector<std::string>& leases) {
        for(auto it = leases.rbegin(); it != leases.rend(); ++it) {
            const auto count = refcounts_.find(*it);
            if(count == refcounts_.end()) continue;
            if(--count->second == 0) {
                // Erase first: the refcount table stays consistent even if a
                // (contract-violating) release callback throws.
                const std::string key = count->first;
                refcounts_.erase(count);
                if(resources_.release) {
                    try { resources_.release(key); }
                    catch(...) {} // Must not throw; never abort a teardown loop.
                }
            }
        }
    }
    void releaseResident(uint64_t sceneId) {
        const auto it = resident_.find(sceneId);
        if(it == resident_.end()) return;
        const std::vector<std::string> leases = std::move(it->second.leases);
        resident_.erase(it);
        release(leases);
    }
    void markUnloaded(uint64_t sceneId) {
        for(auto& [serial, job] : jobs_) {
            (void)serial;
            if(job.ticket.epoch == epoch_ &&
               job.ticket.scene.value == sceneId) {
                job.manifest.reset();
                if(job.status == SceneLoadStatus::Preparing)
                    job.status = SceneLoadStatus::Cancelled;
                else job.status = SceneLoadStatus::Unloaded;
            }
        }
    }

    SceneRuntime scenes_;
    World& world_;
    ReadAsset reader_;
    SceneResourceAdapter resources_;
    SpawnPrefab spawn_;
    SceneHierarchy* hierarchy_ = nullptr;
    size_t maxPreparing_ = 4;
    uint64_t epoch_ = 1;
    uint64_t serial_ = 0;
    std::map<uint64_t, Job> jobs_;
    std::map<uint64_t, Resident> resident_;
    std::map<std::string, size_t> refcounts_;
};

} // namespace aiverse::scene
