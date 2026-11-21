// AIVERSE SpectraFX Stage 24: presentation-only generation-safe GPU VM
// parameter snapshots. This CPU mirror is intended for *future* GPU upload;
// neither the authoritative ECS nor default Metal emitter depends on it.
#pragma once
#include "SpectraParticleProgram.h"
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace aiverse::spectra::gpu_program {

// Packed buffer-friendly snapshot. Particle slot metadata remains separate
// from the 64-byte Metal particle state and cannot change its vertex ABI.
struct alignas(16) ParticleParams {
    std::array<float,8> values{};
    uint32_t sourceSlot = 0;
    uint32_t generation = 0;
    uint32_t active = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(ParticleParams)==48);
static_assert(alignof(ParticleParams)==16);

struct SourceId {
    uint32_t slot = std::numeric_limits<uint32_t>::max();
    uint32_t generation = 0;
    bool valid() const {return generation!=0 && slot!=std::numeric_limits<uint32_t>::max();}
};

class SourceParameters final {
public:
    explicit SourceParameters(uint32_t sourceCapacity=256, uint32_t particleCapacity=4096)
        : sources_(sourceCapacity), particles_(particleCapacity) {}

    SourceId create(const std::array<float,8>& initial={}) {
        if(!validValues(initial))return {};
        for(uint32_t i=0;i<sources_.size();++i) {
            auto& s=sources_[i];
            if(s.active || s.exhausted)continue;
            // Never wrap generations: stale handles must never become valid.
            if(s.generation==std::numeric_limits<uint32_t>::max()) {
                s.exhausted=true;continue;
            }
            ++s.generation;
            s.active=true;
            s.values=initial;
            ++revision_;
            return {i,s.generation};
        }
        return {};
    }

    bool contains(SourceId id) const {
        return id.valid() && id.slot<sources_.size() &&
            sources_[id.slot].active &&
            sources_[id.slot].generation==id.generation;
    }

    // Retiring a source disallows further mutation/spawn but leaves already
    // emitted particles' values intact until those particles die.
    bool retire(SourceId id) {
        if(!contains(id))return false;
        sources_[id.slot].active=false;
        ++revision_;
        return true;
    }

    // Each new particle captures the current source's eight values.
    bool spawn(uint32_t particleSlot, SourceId id) {
        if(!contains(id)||particleSlot>=particles_.size() ||
           particles_[particleSlot].active)return false;
        auto& p=particles_[particleSlot];
        p.values=sources_[id.slot].values;
        p.sourceSlot=id.slot;
        p.generation=id.generation;
        p.active=1;
        p.reserved=1; // only pending births execute the Spawn VM
        ++revision_;
        return true;
    }

    // Host acknowledges the GPU Spawn dispatch only after its commands have
    // been successfully submitted. Does NOT wait for GPU execution.
    bool acknowledgeSpawn(uint32_t particleSlot) {
        if(particleSlot>=particles_.size() ||
           !particles_[particleSlot].active ||
           particles_[particleSlot].reserved!=1)return false;
        particles_[particleSlot].reserved=0;
        ++revision_;
        return true;
    }

    bool kill(uint32_t particleSlot) {
        if(particleSlot>=particles_.size() || !particles_[particleSlot].active)return false;
        particles_[particleSlot]=ParticleParams{};
        ++revision_;
        return true;
    }

    bool set(SourceId id,uint32_t parameterSlot,float value,bool affectExisting=false) {
        if(!contains(id)||parameterSlot>=8||!program::number(value))return false;
        sources_[id.slot].values[parameterSlot]=value;
        if(affectExisting) {
            for(auto& particle:particles_) {
                if(particle.active && particle.sourceSlot==id.slot &&
                   particle.generation==id.generation)
                    particle.values[parameterSlot]=value;
            }
        }
        ++revision_;
        return true;
    }

    bool setAll(SourceId id,const std::array<float,8>& values,bool affectExisting=false) {
        if(!contains(id)||!validValues(values))return false;
        sources_[id.slot].values=values;
        if(affectExisting) {
            for(auto& particle:particles_) {
                if(particle.active && particle.sourceSlot==id.slot &&
                   particle.generation==id.generation)
                    particle.values=values;
            }
        }
        ++revision_;
        return true;
    }

    const ParticleParams* particle(uint32_t slot) const {
        return slot<particles_.size() && particles_[slot].active
            ? &particles_[slot]:nullptr;
    }
    const std::array<float,8>* values(SourceId id) const {
        return contains(id)?&sources_[id.slot].values:nullptr;
    }
    const std::vector<ParticleParams>& particleBuffer() const {return particles_;}
    uint64_t revision() const {return revision_;}
    uint32_t particleCapacity() const {return uint32_t(particles_.size());}
    uint32_t sourceCapacity() const {return uint32_t(sources_.size());}

    void clearParticles() {
        for(auto& p:particles_)p=ParticleParams{};
        ++revision_;
    }

private:
    struct Source {
        std::array<float,8> values{};
        uint32_t generation=0;
        bool active=false;
        bool exhausted=false;
    };
    static bool validValues(const std::array<float,8>& values) {
        for(float value:values)if(!program::number(value))return false;
        return true;
    }
    std::vector<Source> sources_;
    std::vector<ParticleParams> particles_;
    uint64_t revision_=0;
};

} // namespace aiverse::spectra::gpu_program
