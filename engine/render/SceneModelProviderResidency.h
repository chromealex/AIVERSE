// AIVERSE — shared REAL ModelAssetProvider residency for cosmetic scenes.
// A scene lease is a ref-counted claim on the whole native GPU owner, not
// a shared_ptr to an AppearanceAsset or a substitute for GPU fence proof.
// The caller must release every lease at the render/scene structural barrier.
#pragma once
#include "SceneModelProviderRetirement.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace aiverse::render {

class SceneModelProviderResidency final {
public:
    using Owner=std::unique_ptr<ModelAssetProvider>;
    struct Lease {
        std::string key;
        uint64_t generation=0;
        Lease()=default;
        Lease(std::string id,uint64_t epoch)
            :key(std::move(id)),generation(epoch) {}
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other) noexcept
            :key(std::move(other.key)),
             generation(std::exchange(other.generation,0)) {
            other.key.clear();
        }
        Lease& operator=(Lease&& other) noexcept {
            if(this!=&other) {
                key=std::move(other.key);
                generation=std::exchange(other.generation,0);
                other.key.clear();
            }
            return *this;
        }
        explicit operator bool() const noexcept {
            return generation!=0 && !key.empty();
        }
    };

    explicit SceneModelProviderResidency(
            SceneModelProviderRetirement& retirement) noexcept
        :retirement_(retirement) {}
    SceneModelProviderResidency(const SceneModelProviderResidency&)=delete;
    SceneModelProviderResidency& operator=(
        const SceneModelProviderResidency&)=delete;
    ~SceneModelProviderResidency() noexcept {
        // Without explicit release at a device barrier there is no proof
        // that submitted draws have finished. Fail closed: deliberately
        // orphan the entire owner instead of invoking GPU destructors.
        for(auto& [key,entry]:owners_) {
            (void)key;
            if(entry.owner)(void)entry.owner.release();
        }
    }

    // Adopt a newly prepared, GPU-epoch-bound provider. No duplicate key
    // can replace an existing provider while another scene holds a lease.
    // On rejection/exception incoming remains with the caller for safe
    // GPU-token retirement. Ownership transfers only on success.
    bool adopt(const std::string& key,Owner& incoming,Lease& out) {
        if(out || !incoming || key.empty() || key.size()>256 ||
           owners_.count(key) ||
           nextGeneration_==std::numeric_limits<uint64_t>::max())
            return false;
        Lease prepared{key,nextGeneration_+1};
        auto [it,inserted]=owners_.try_emplace(key);
        if(!inserted)return false;
        it->second.owner=std::move(incoming);
        it->second.generation=prepared.generation;
        it->second.references=1;
        nextGeneration_=prepared.generation;
        out=std::move(prepared);
        return true;
    }

    // A second/additive scene may borrow exactly the same provider and
    // immutable resident model set. Never mutate/invalidate existing
    // model publications while either scene has in-flight draw packets.
    bool acquire(const std::string& key,Lease& out) {
        if(out)return false;
        auto it=owners_.find(key);
        if(it==owners_.end() || !it->second.owner ||
           it->second.references==std::numeric_limits<size_t>::max())
            return false;
        Lease prepared{key,it->second.generation}; // allocate BEFORE count
        ++it->second.references;
        out=std::move(prepared);
        return true;
    }

    // Move-only by convention: a Lease is a logical refcount token. The
    // caller must not copy a live Lease or call release twice on a copy.
    const ModelAssetProvider* find(const Lease& lease) const noexcept {
        if(!lease)return nullptr;
        auto it=owners_.find(lease.key);
        return it!=owners_.end() &&
               it->second.generation==lease.generation
            ?it->second.owner.get():nullptr;
    }
    const ModelAssetProvider* find(const std::string& key) const noexcept {
        auto it=owners_.find(key);
        return it==owners_.end()?nullptr:it->second.owner.get();
    }
    size_t references(const std::string& key) const noexcept {
        auto it=owners_.find(key);
        return it==owners_.end()?0:it->second.references;
    }
    size_t ownerCount() const noexcept {return owners_.size();}

    // Release the scene's claim AFTER its cosmetic Graphs and frame-scoped
    // ModelReadLeases are detached. The LAST claim transfers the actual
    // provider to the ordered GPU retirement queue; intermediate releases
    // must never wait for idle or free GPU-visible handles.
    void release(gfx::Device& device,Lease& lease) noexcept {
        if(!lease)return;
        const auto it=owners_.find(lease.key);
        if(it!=owners_.end() && it->second.generation==lease.generation &&
           it->second.references!=0) {
            if(--it->second.references==0) {
                Owner retired=std::move(it->second.owner);
                owners_.erase(it);
                retirement_.retire(device,retired);
            }
        }
        lease.key.clear();
        lease.generation=0;
    }

private:
    struct Entry {
        Owner owner;
        uint64_t generation=0;
        size_t references=0;
    };
    SceneModelProviderRetirement& retirement_;
    std::map<std::string,Entry> owners_;
    uint64_t nextGeneration_=0;
};

} // namespace aiverse::render
