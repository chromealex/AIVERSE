// SpectraFX Stage 25: renderer-owned physical particle slot lifecycle.
// No gameplay ECS, GPU handles, or rendering state in authoritative World.
#pragma once
#include "SpectraGpuSourceParameters.h"
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::spectra::gpu_program {

struct ParticleId {
    uint32_t slot=std::numeric_limits<uint32_t>::max();
    uint32_t generation=0;
    bool valid() const {
        return generation!=0 && slot!=std::numeric_limits<uint32_t>::max();
    }
};

// Couples physical slot ownership and the Stage-24 value snapshot *before*
// the host writes its spawn seed into GPU storage. The host must follow
// acquire -> upload seed -> Spawn VM -> Update VM -> release once per lifetime.
// This adapter is NOT a renderer or an autonomous timestep/scheduler.
class ParticleSlots final {
public:
    ParticleSlots(uint32_t sourceCapacity=256,uint32_t particleCapacity=4096)
        : parameters_(sourceCapacity,particleCapacity),
          slots_(particleCapacity), free_() {
        free_.reserve(particleCapacity);
        for(uint32_t i=particleCapacity;i>0;--i)free_.push_back(i-1);
    }

    SourceParameters& parameters() {return parameters_;}
    const SourceParameters& parameters() const {return parameters_;}

    ParticleId acquire(SourceId source) {
        if(!parameters_.contains(source))return {};
        while(!free_.empty()) {
            const uint32_t slot=free_.back();
            free_.pop_back();
            Slot& s=slots_[slot];
            if(s.active||s.exhausted)continue;
            if(s.generation==std::numeric_limits<uint32_t>::max()) {
                s.exhausted=true;continue;
            }
            // Preserve atomic ownership: failed snapshot creation MUST NOT
            // leave a slot reserved or create a generation-valid handle.
            if(!parameters_.spawn(slot,source)) {
                free_.push_back(slot);return {};
            }
            ++s.generation;
            s.active=true;
            s.spawnPending=true;
            ++active_;
            if(slot+1>prefix_)prefix_=slot+1;
            return {slot,s.generation};
        }
        return {};
    }

    bool alive(ParticleId id) const {
        return id.valid() && id.slot<slots_.size() &&
            slots_[id.slot].active && slots_[id.slot].generation==id.generation;
    }
    bool needsSpawn(ParticleId id) const {
        return alive(id) && slots_[id.slot].spawnPending;
    }
    // A GPU spawn is submitted only after seed data, source snapshot and
    // their queue ordering have been committed. This method only records the
    // host's submission decision; it does not call Metal or guarantee success.
    bool markSpawnSubmitted(ParticleId id) {
        if(!needsSpawn(id))return false;
        if(!parameters_.acknowledgeSpawn(id.slot))return false;
        slots_[id.slot].spawnPending=false;
        return true;
    }
    // Never free/recycle an in-flight slot on this call alone: the host must
    // fence all GPU commands referencing it BEFORE calling release().
    bool release(ParticleId id) {
        if(!alive(id))return false;
        Slot& s=slots_[id.slot];
        if(!parameters_.kill(id.slot))return false;
        s.active=false;s.spawnPending=false;
        --active_;
        if(!s.exhausted)free_.push_back(id.slot);
        while(prefix_>0&&!slots_[prefix_-1].active)--prefix_;
        return true;
    }
    uint32_t activeCount() const {return active_;}
    uint32_t physicalPrefix() const {return prefix_;}
    uint32_t capacity() const {return uint32_t(slots_.size());}
private:
    struct Slot {
        uint32_t generation=0;
        bool active=false;
        bool exhausted=false;
        bool spawnPending=false;
    };
    SourceParameters parameters_;
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
    uint32_t active_=0,prefix_=0;
};

} // namespace aiverse::spectra::gpu_program
