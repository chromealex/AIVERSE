// AIVERSE SpectraFX graph-v2: read-only, bounded scalar Data Interfaces.
// Data is sampled at the presentation's burst point and copied into the
// existing param0..param7 VM registers. No ECS, renderer or RHI ownership.
// All providers are host-injected, non-owning callbacks executed on the
// presentation owner thread. A graph never reads mutable gameplay state.
#pragma once
#include "SpectraInstanceParameters.h"
#include "engine/render/Math3D.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::spectra {

struct DataInterfaceBinding {
    std::string provider;
    std::string channel;
    float scale=1.0f;
    float offset=0.0f;
    float fallback=0.0f;
    bool required=false;
};
using DataInterfaceBindings=std::map<std::string,DataInterfaceBinding>;

inline bool loadDataInterfaceBindings(const Json& authored,
                                      const InstanceParameterTable& parameters,
                                      DataInterfaceBindings& output,
                                      std::vector<std::string>& errors) {
    DataInterfaceBindings next;
    if(authored.isNull()) {output.clear();return true;}
    if(!authored.isObject() || authored.size()>8) {
        errors.push_back("SpectraFX dataInterfaces must be a map with at most 8 bindings");
        return false;
    }
    for(const auto& [name,item]:authored.members()) {
        const auto parameter=parameters.find(name);
        if(parameter==parameters.end() || !item.isObject()) {
            errors.push_back("SpectraFX dataInterfaces: unknown instance parameter or invalid binding '"+name+"'");
            return false;
        }
        for(const auto& [key,unused]:item.members()) {
            (void)unused;
            if(key!="provider" && key!="channel" && key!="scale" &&
               key!="offset" && key!="fallback" && key!="required") {
                errors.push_back("SpectraFX dataInterfaces: unknown field '"+name+"."+key+"'");
                return false;
            }
        }
        if(!item["provider"].isString() || !item["channel"].isString() ||
           !instanceParameterNameOk(item["provider"].str()) ||
           !instanceParameterNameOk(item["channel"].str())) {
            errors.push_back("SpectraFX dataInterfaces: provider/channel must be safe nonempty identifiers");
            return false;
        }
        DataInterfaceBinding b;
        b.provider=item["provider"].str();
        b.channel=item["channel"].str();
        b.fallback=parameter->second.initial;
        const auto numeric=[&](const char* key,float& result)->bool {
            if(!item.has(key))return true;
            const Json& value=item[key];
            if(!value.isNumber())return false;
            const double n=value.toDouble();
            if(!std::isfinite(n)||std::abs(n)>1.0e7)return false;
            result=float(n);
            return true;
        };
        if(!numeric("scale",b.scale) || !numeric("offset",b.offset) ||
           !numeric("fallback",b.fallback) ||
           std::abs(b.scale)>1000.0f || std::abs(b.offset)>1.0e7f ||
           !checkInstanceParameterValue(parameter->second,b.fallback)) {
            errors.push_back("SpectraFX dataInterfaces: invalid numeric range at '"+name+"'");
            return false;
        }
        if(item.has("required")) {
            if(item["required"].type()!=Json::Type::Bool) {
                errors.push_back("SpectraFX dataInterfaces: required must be boolean");
                return false;
            }
            b.required=item["required"].boolean();
        }
        next.emplace(name,std::move(b));
    }
    output=std::move(next);
    return true;
}

class DataInterfaceRegistry final {
public:
    using Point=render::float3;
    // A false result means the provider has no sample for channel/point.
    // Every sampler must be safe to invoke on the rendering owner thread.
    using Sampler=std::function<bool(std::string_view,Point,float&)>;
    static constexpr size_t kMaxProviders=16;
    bool bind(std::string name,Sampler sample) {
        if(!instanceParameterNameOk(name)||!sample)return false;
        if(providers_.find(name)==providers_.end() &&
           providers_.size()>=kMaxProviders)return false;
        providers_[std::move(name)]=std::move(sample);
        return true;
    }
    bool unbind(const std::string& name) {
        return providers_.erase(name)!=0;
    }
    void clear() {providers_.clear();}
    size_t size() const {return providers_.size();}
    bool sample(std::string_view provider,std::string_view channel,
                Point position,float& value) const {
        if(!std::isfinite(position.x)||!std::isfinite(position.y)||
           !std::isfinite(position.z))return false;
        const auto it=providers_.find(std::string(provider));
        if(it==providers_.end())return false;
        float result=0;
        try {
            if(!it->second(channel,position,result))return false;
        } catch(...) {
            // An unexpected host exception cannot leave half-initialized
            // per-particle state. Treat it as unavailable instead.
            return false;
        }
        if(!std::isfinite(result)||std::abs(result)>1.0e7f)return false;
        value=result;
        return true;
    }
private:
    std::map<std::string,Sampler> providers_;
};

struct DataInterfaceSampleStats {
    uint32_t resolved=0;
    uint32_t fallbacks=0;
    uint32_t failedRequired=0;
};

// Transactional snapshot: never alter 'out' on validation failure. Existing
// uniforms are preserved unless the complete batch is accepted. If required
// data is absent, caller must not emit a new particle burst.
inline bool sampleDataInterfaces(const DataInterfaceBindings& bindings,
                                 const InstanceParameterTable& parameters,
                                 const DataInterfaceRegistry* registry,
                                 render::float3 at,
                                 const std::array<float,8>& previous,
                                 std::array<float,8>& out,
                                 std::string& error,
                                 DataInterfaceSampleStats* stats=nullptr) {
    std::array<float,8> next=previous;
    DataInterfaceSampleStats count{};
    for(const auto& [name,b]:bindings) {
        const auto found=parameters.find(name);
        if(found==parameters.end() || found->second.slot>=8) {
            error="SpectraFX dataInterfaces: invalid parameter slot for '"+name+"'";
            if(stats)*stats=count;
            return false;
        }
        float raw=0.0f;
        bool foundValue=registry && registry->sample(b.provider,b.channel,at,raw);
        float value=b.fallback;
        if(foundValue) {
            const double transformed=double(raw)*double(b.scale)+double(b.offset);
            if(std::isfinite(transformed) && transformed>=found->second.minimum &&
               transformed<=found->second.maximum &&
               std::abs(transformed)<=1.0e7) {
                value=float(transformed);
            } else {
                foundValue=false;
            }
        }
        if(!foundValue) {
            if(b.required) {
                error="SpectraFX dataInterfaces: required provider/channel or value unavailable for '"+name+"'";
                ++count.failedRequired;
                if(stats)*stats=count;
                return false;
            }
            ++count.fallbacks;
        } else ++count.resolved;
        next[found->second.slot]=value;
    }
    out=next;
    if(stats)*stats=count;
    error.clear();
    return true;
}

} // namespace aiverse::spectra
