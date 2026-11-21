// AIVERSE scene dependency residency. Generic front-end to existing renderer,
// collision/terrain, water, weather, VFX, navmesh and audio owners.
// Does not duplicate their caches or create GPU/physics objects itself.
// Prepare is worker-safe; publish/retire run at the caller's owner-thread barrier.
#pragma once
#include "SceneAsset.h"
#include "SceneLoadCoordinator.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::scene {

enum class SceneResourceKind : uint8_t {
    Render, Collision, Terrain, Water, Weather, Vfx, Audio, Navmesh, Other
};
enum class SceneResourcePhase : uint8_t {
    Unknown, Queued, Preparing, Prepared, Published, Failed, Cancelled
};
struct SceneResourceSpec {
    SceneResourceKind kind = SceneResourceKind::Other;
    size_t stagingBytes = 0;  // Conservative *peak* reservation incl. prepared payload.
    size_t residentBytes = 0; // Conservative post-publication resource reservation.
    size_t publishBytes = 0;  // Publication bandwidth cost (may equal residentBytes).
};
struct SceneResourceLimits {
    size_t stagingBytes = 64 * 1024 * 1024;
    size_t residentBytes = 256 * 1024 * 1024;
    size_t maxConcurrent = 4;
    size_t maxEntries = 8192;
};
struct SceneResourceStatus {
    size_t pending = 0;
    size_t preparing = 0;
    size_t prepared = 0;
    size_t published = 0;
    size_t failed = 0;
    size_t stagingReserved = 0;
    size_t residentReserved = 0;
};

// Published resource belongs to its original subsystem owner. This payload
// is temporary CPU-prepared data; it is never an ECS resource or GPU handle.
using ScenePreparedResource = std::shared_ptr<void>;
struct SceneResourceHooks {
    using Prepare = std::function<ScenePreparedResource(const std::string&, std::string&)>;
    using Publish = std::function<bool(const std::string&, const ScenePreparedResource&,
                                       std::string&)>;
    using Retire = std::function<void(const std::string&)>;
    Prepare prepare;
    Publish publish;
    Retire retire; // Must not throw. Called only after successful publish.
};

class SceneResourceBackend {
public:
    explicit SceneResourceBackend(SceneResourceLimits limits = {}) : limits_(limits) {}
    ~SceneResourceBackend() {
        // Owner must destroy the SceneLoadCoordinator before the backend, so
        // every scene lease is returned and callbacks are still alive.
        // Running futures finish when entries_ destruct; the host must keep
        // worker callback captures alive until then.
        for(auto& [key, e] : entries_)
            if(e.phase == SceneResourcePhase::Published && e.hooks.retire)
                e.hooks.retire(key);
    }
    SceneResourceBackend(const SceneResourceBackend&) = delete;
    SceneResourceBackend& operator=(const SceneResourceBackend&) = delete;

    bool registerKind(SceneResourceKind kind, SceneResourceHooks hooks) {
        if(!hooks.prepare || !hooks.publish || !hooks.retire ||
           kinds_.contains(kind)) return false;
        kinds_.emplace(kind, std::move(hooks));
        return true;
    }

    // A content catalog (or host plugin) defines conservative budgets before
    // the first load. Refuse to redefine an in-use/queued key.
    bool define(std::string key, SceneResourceSpec spec) {
        if(!SceneAsset::validKey(key) || entries_.contains(key) ||
           !kinds_.contains(spec.kind) || specs_.contains(key) ||
           specs_.size() >= limits_.maxEntries ||
           spec.stagingBytes > limits_.stagingBytes ||
           spec.residentBytes > limits_.residentBytes)
            return false;
        specs_.emplace(std::move(key), spec);
        return true;
    }

    // Pins are temporary interests held while a scene is preparing.
    // Leases are acquired by SceneLoadCoordinator only at commit.
    // Neither has any effect on authoritative ECS state.
    bool pin(const std::string& key, std::string* error = nullptr) {
        const auto spec = specs_.find(key);
        if(spec == specs_.end()) {
            if(error) *error = "missing scene resource catalog entry: " + key;
            return false;
        }
        auto [it, inserted] = entries_.try_emplace(key);
        Entry& e = it->second;
        if(inserted) {
            e.spec = spec->second;
            e.hooks = kinds_.at(e.spec.kind);
            e.phase = SceneResourcePhase::Queued;
        }
        if(e.phase == SceneResourcePhase::Cancelled) {
            // A cancelled entry is a tombstone for a still-running worker
            // (its staging reservation is still held). A new interest
            // resurrects it instead of failing forever: keep the running
            // future, or queue a fresh preparation if none exists.
            if(e.future.valid()) e.phase = SceneResourcePhase::Preparing;
            else {
                releaseStaging(e);
                e.phase = SceneResourcePhase::Queued;
            }
            e.error.clear();
        }
        if(e.phase == SceneResourcePhase::Failed) {
            if(error) *error = e.error;
            return false;
        }
        ++e.pins;
        return true;
    }
    void unpin(const std::string& key) {
        auto it = entries_.find(key);
        if(it == entries_.end() || it->second.pins == 0) return;
        --it->second.pins;
        retireIfUnused(it);
    }
    bool lease(const std::string& key, std::string& error) {
        const auto it = entries_.find(key);
        if(it == entries_.end() ||
           it->second.phase != SceneResourcePhase::Published) {
            error = "scene resource not yet published: " + key;
            return false;
        }
        ++it->second.leases;
        return true;
    }
    void unlease(const std::string& key) {
        auto it = entries_.find(key);
        if(it == entries_.end() || it->second.leases == 0) return;
        --it->second.leases;
        retireIfUnused(it);
    }
    // Standard SceneLoadCoordinator adapter, with backend outliving loader.
    // The backend counts ONE lease per coordinator/key (unique external owner),
    // not one lease per scene. SceneLoadCoordinator::resourceRefCount(key)
    // separately counts the additive scenes that use that dependency.
    // For two scenes in one coordinator: backend leaseCount == 1 while
    // coordinator resourceRefCount == 2. This is intentional deduplication.
    SceneResourceAdapter coordinatorAdapter() {
        return {
            [this](const std::string& key, std::string& error) {
                return lease(key,error);
            },
            [this](const std::string& key) { unlease(key); }
        };
    }

    // Poll futures then launch at most startBudget NEW preparations; no ECS
    // writes. Limits and publication budgets are metadata, never wall clocks.
    void poll(size_t startBudget = 4) {
        for(auto it = entries_.begin(); it != entries_.end();) {
            auto& e = it->second;
            if((e.phase != SceneResourcePhase::Preparing &&
                e.phase != SceneResourcePhase::Cancelled) ||
               !e.future.valid() ||
               e.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }
            Prepared result;
            try { result = e.future.get(); }
            catch(const std::exception& ex) { result.error = ex.what(); }
            catch(...) { result.error = "unknown scene resource worker exception"; }
            if(e.phase == SceneResourcePhase::Cancelled ||
               (e.pins == 0 && e.leases == 0)) {
                releaseStaging(e);
                it = entries_.erase(it);
                continue;
            }
            if(!result.error.empty() || !result.payload) {
                releaseStaging(e);
                e.error = !result.error.empty() ? std::move(result.error)
                                                : "scene resource preparation returned null";
                e.phase = SceneResourcePhase::Failed;
            } else {
                e.payload = std::move(result.payload);
                e.phase = SceneResourcePhase::Prepared;
            }
            ++it;
        }
        size_t inflight = 0;
        for(const auto& [key,e] : entries_) {
            (void)key;
            // A canceled std::async job still occupies a worker slot until
            // poll() observes its completion and releases its reservation.
            if(e.phase == SceneResourcePhase::Preparing ||
               (e.phase == SceneResourcePhase::Cancelled && e.future.valid()))
                ++inflight;
        }
        for(auto& [key,e] : entries_) {
            if(!startBudget || inflight >= limits_.maxConcurrent) break;
            if(e.phase != SceneResourcePhase::Queued || e.pins + e.leases == 0)
                continue;
            if(e.spec.stagingBytes > limits_.stagingBytes - stagingReserved_)
                continue;
            // Captures only immutable callbacks and key, not the backend or ECS.
            const auto prepare = e.hooks.prepare;
            try {
                e.future = std::async(std::launch::async,
                    [prepare,key] {
                        Prepared result;
                        try { result.payload = prepare(key,result.error); }
                        catch(const std::exception& ex) { result.error = ex.what(); }
                        catch(...) { result.error = "unknown scene resource exception"; }
                        return result;
                    });
            } catch(const std::exception& ex) {
                e.error = ex.what();
                e.phase = SceneResourcePhase::Failed;
                continue;
            }
            e.phase = SceneResourcePhase::Preparing;
            e.stagingHeld = true;
            stagingReserved_ += e.spec.stagingBytes;
            ++inflight;
            --startBudget;
        }
    }

    // Render/physics publication barrier. At most publishOps successful or
    // attempted publications and publishBytes total estimated bandwidth.
    // A large single resource remains Prepared until given a sufficient budget.
    size_t publish(size_t publishOps, size_t publishBytes) {
        size_t done = 0;
        for(auto& [key,e] : entries_) {
            if(!publishOps) break;
            if(e.phase != SceneResourcePhase::Prepared ||
               e.pins + e.leases == 0) continue;
            if(e.spec.publishBytes > publishBytes ||
               e.spec.residentBytes > limits_.residentBytes - residentReserved_)
                continue;
            --publishOps;
            publishBytes -= e.spec.publishBytes;
            std::string error;
            bool success = false;
            try { success = e.hooks.publish(key,e.payload,error); }
            catch(const std::exception& ex) { error = ex.what(); }
            catch(...) { error = "unknown scene resource publish exception"; }
            releaseStaging(e);
            e.payload.reset();
            if(!success) {
                e.error = error.empty() ? "scene resource publication rejected" : error;
                e.phase = SceneResourcePhase::Failed;
                continue;
            }
            e.phase = SceneResourcePhase::Published;
            residentReserved_ += e.spec.residentBytes;
            ++done;
        }
        return done;
    }

    void update(size_t startBudget, size_t publishOps, size_t publishBytes) {
        poll(startBudget);
        publish(publishOps,publishBytes);
    }

    SceneResourcePhase phase(const std::string& key) const {
        auto it = entries_.find(key);
        return it == entries_.end() ? SceneResourcePhase::Unknown : it->second.phase;
    }
    bool ready(const std::vector<std::string>& keys) const {
        for(const std::string& key : keys)
            if(phase(key) != SceneResourcePhase::Published) return false;
        return true;
    }
    bool errorFor(const std::vector<std::string>& keys, std::string& out) const {
        for(const auto& key : keys) {
            auto it = entries_.find(key);
            if(it != entries_.end() && it->second.phase == SceneResourcePhase::Failed) {
                out = key + ": " + it->second.error;
                return true;
            }
        }
        return false;
    }
    SceneResourceStatus status() const {
        SceneResourceStatus s;
        s.stagingReserved = stagingReserved_;
        s.residentReserved = residentReserved_;
        for(const auto& [key,e] : entries_) {
            (void)key;
            switch(e.phase) {
                case SceneResourcePhase::Queued: ++s.pending; break;
                case SceneResourcePhase::Preparing: ++s.preparing; break;
                case SceneResourcePhase::Prepared: ++s.prepared; break;
                case SceneResourcePhase::Published: ++s.published; break;
                case SceneResourcePhase::Failed: ++s.failed; break;
                default: break;
            }
        }
        return s;
    }
    size_t leaseCount(const std::string& key) const {
        auto it = entries_.find(key);
        return it == entries_.end() ? 0 : it->second.leases;
    }
    size_t pinCount(const std::string& key) const {
        auto it = entries_.find(key);
        return it == entries_.end() ? 0 : it->second.pins;
    }
private:
    struct Prepared { ScenePreparedResource payload; std::string error; };
    struct Entry {
        SceneResourceSpec spec;
        SceneResourceHooks hooks;
        SceneResourcePhase phase = SceneResourcePhase::Unknown;
        size_t pins = 0, leases = 0;
        bool stagingHeld = false;
        std::future<Prepared> future;
        ScenePreparedResource payload;
        std::string error;
    };
    void releaseStaging(Entry& e) {
        if(e.stagingHeld) {
            stagingReserved_ -= e.spec.stagingBytes;
            e.stagingHeld = false;
        }
    }
    void retireIfUnused(std::map<std::string,Entry>::iterator it) {
        Entry& e = it->second;
        if(e.pins || e.leases) return;
        if(e.phase == SceneResourcePhase::Preparing) {
            // Don't destroy a std::async future during cancellation.
            e.phase = SceneResourcePhase::Cancelled;
            return;
        }
        if(e.phase == SceneResourcePhase::Published) {
            e.hooks.retire(it->first);  // nonthrowing by contract
            residentReserved_ -= e.spec.residentBytes;
        }
        releaseStaging(e);
        entries_.erase(it);
    }
    SceneResourceLimits limits_;
    std::map<SceneResourceKind, SceneResourceHooks> kinds_;
    std::map<std::string, SceneResourceSpec> specs_;
    std::map<std::string, Entry> entries_;
    size_t stagingReserved_ = 0, residentReserved_ = 0;
};

} // namespace aiverse::scene
