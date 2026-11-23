// SpectraFX — read-only external acceleration providers for visual particles.
// Host-provided environment data (PHYSIS wind, gravity zones, turbulence)
// is sampled on the render owner thread once per dedicated emitter per frame.
// No ECS reads, physics writes, network state, RHI or gameplay dependencies.
// This is a CPU-side bridge; supported GPU and CPU twin paths receive the
// exact same final per-frame acceleration uniform.
#pragma once
#include "../render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace aiverse::fx {

class SpectraFxAccelerationFields final {
public:
    using Point = render::float3;
    // Return false when a field does not cover this point. Set acceleration
    // only after filling all axes; callback must not throw or retain pointers.
    using Sampler = std::function<bool(Point,Point&)>;
    static constexpr size_t kMaxProviders=16;
    struct Stats {
        uint32_t consulted=0,accepted=0,outside=0,invalid=0;
    };
    struct Entry {
        uint64_t id=0; // user-assigned stable identifier, not ECS entity ID
        float weight=1;
        Sampler sample;
    };

    bool bind(uint64_t id,float weight,Sampler callback) {
        if(!id || !std::isfinite(weight) || weight<0 || weight>16 ||
           !callback)return false;
        for(auto& e:entries_) {
            if(e.id==id) {
                e.weight=weight;
                e.sample=std::move(callback);
                return true;
            }
        }
        if(entries_.size()>=kMaxProviders)return false;
        entries_.push_back({id,weight,std::move(callback)});
        return true;
    }
    bool unbind(uint64_t id) {
        const auto old=entries_.size();
        entries_.erase(std::remove_if(entries_.begin(),entries_.end(),
            [id](const Entry& e){return e.id==id;}),entries_.end());
        return entries_.size()!=old;
    }
    void clear() {entries_.clear();}
    bool empty() const {return entries_.empty();}
    size_t size() const {return entries_.size();}

    // Invalid provider data cannot poison a particle state or GPU uniform.
    // Clamp final field correction to +/-1000 m/s² per axis; each provider
    // contributes at most +/-100 m/s² before weighting. Intentional limits
    // prevent one integration from creating unstable particles.
    bool evaluate(Point position,Point& out,Stats* stats=nullptr) const {
        out={0,0,0};
        Stats observed{};
        if(!finite(position)) {
            observed.invalid=1;
            if(stats)*stats=observed;
            return false;
        }
        for(const auto& e:entries_) {
            ++observed.consulted;
            Point value{};
            if(!e.sample(position,value)) {
                ++observed.outside;
                continue;
            }
            if(!finite(value) || std::abs(value.x)>100 ||
               std::abs(value.y)>100 || std::abs(value.z)>100) {
                ++observed.invalid;
                continue;
            }
            out.x=std::clamp(out.x+value.x*e.weight,-1000.f,1000.f);
            out.y=std::clamp(out.y+value.y*e.weight,-1000.f,1000.f);
            out.z=std::clamp(out.z+value.z*e.weight,-1000.f,1000.f);
            ++observed.accepted;
        }
        if(stats)*stats=observed;
        return observed.accepted!=0;
    }
private:
    static bool finite(Point p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    std::vector<Entry> entries_;
};

} // namespace aiverse::fx
