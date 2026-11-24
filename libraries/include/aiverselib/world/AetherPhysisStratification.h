// Optional bounded gameplay stratification, NOT atmospheric CFD.
// PHYSIS owns gravity and temperature; AETHER owns quantized gas masses.
#pragma once
#include "AetherEcs.h"
#include "WorldProcessSurfaceFrame.h"
#include "engine/core/FixedMath.h"
#include "engine/sim/CommonComponents.h"
#include "engine/physis/PhysisGravity.h"

namespace aiverse::aether {
struct StratificationSettings {
    // Effective buoyancy, not molecular density: positive rises opposite
    // gravity, negative settles along it. Units are gameplay permille.
    std::array<int32_t,3> buoyancyPermille{350,-350,0};
    int32_t thermalPerDegree=5;
    int32_t referenceCelsius=20;
};
inline bool valid(const StratificationSettings& s){
    for(auto b:s.buoyancyPermille)if(b< -1000||b>1000)return false;
    return s.thermalPerDegree>=0&&s.thermalPerDegree<=20&&
        s.referenceCelsius>=-100&&s.referenceCelsius<=200;
}
namespace stratification_detail {
struct Sample {
    Vec3 position{},gravity{};
    int body=-1;
    std::optional<Fixed> temperature;
};
inline std::optional<Sample> sample(const World& world,const game::Surface* surface,Entity e){
    const auto* tr=world.tryGetConst<Transform>(e);
    if(!tr)return {};
    constexpr auto bound=Fixed::fromInt(1000000);
    for(auto v:{tr->pos.x,tr->pos.y,tr->pos.z})if(v< -bound||v>bound)return {};
    const auto frame=lib::worldprocess::surfaceFrame(surface,tr->pos);
    if(!frame)return {};
    Sample result{tr->pos,physis::gravityAt(world,tr->pos),frame->body,{}};
    // Never infer planetary weather from a global planar PHYSIS cell.
    // An explicit co-located authoritative Atmosphere is optional.
    if(const auto* air=world.tryGetConst<physis::Atmosphere>(e))
        result.temperature=fxClamp(air->temperature,Fixed::fromInt(-100),Fixed::fromInt(200));
    return result;
}
inline Fixed riseFactor(const Sample& from,const Sample& to){
    const Vec3 direction=fxNormalize(to.position-from.position);
    const auto average=(from.gravity+to.gravity)*Fixed::fromRatio(1,2);
    return fxClamp(-average.dot(direction)/Fixed::fromInt(10),
        Fixed::fromInt(-1),Fixed::fromInt(1));
}
inline int64_t thermalBias(const Sample& a,const Sample& b,const StratificationSettings& s){
    if(!a.temperature||!b.temperature)return 0;
    const auto mean=(*a.temperature+*b.temperature)*Fixed::fromRatio(1,2);
    return ((mean-Fixed::fromInt(s.referenceCelsius))*s.thermalPerDegree).toInt();
}
} // namespace stratification_detail
inline bool advanceWithStratification(World& world,const game::Surface* surface,
        uint32_t tick,const StratificationSettings& settings={}){
    if(!valid(settings))return false;
    namespace wp=lib::worldprocess;
    return advanceWithTransport(world,tick,[&](const World& read,
            const std::vector<Entity>& cells,const std::vector<bool>& enabled,
            std::array<std::vector<Mass>,3>& species,const std::vector<wp::Flow>& flows){
        std::vector<std::optional<stratification_detail::Sample>> samples(cells.size());
        // Only endpoints of actual nonzero ducts require spatial data.
        for(const auto& f:flows){
            if(!f.cap)continue;
            for(auto i:{f.from,f.to}){
                if(!enabled[i])return false;
                if(!samples[i])samples[i]=stratification_detail::sample(read,surface,cells[i]);
                if(!samples[i])return false;
            }
            // An interplanetary transfer is a separate explicit owner action,
            // not a local atmospheric duct. Fail atomically on bad topology.
            if(samples[f.from]->body!=samples[f.to]->body)return false;
        }
        return wp::diffuseSpeciesWithHead(species,flows,
            [&](size_t kind,wp::Flow f,Mass from,Mass to)->std::optional<Mass>{
                const auto& a=*samples[f.from];const auto& b=*samples[f.to];
                const auto buoyancy=std::clamp<int64_t>(
                    settings.buoyancyPermille[kind]+stratification_detail::thermalBias(a,b,settings),
                    -1000,1000);
                // Opposite directions get opposite heads; no double transfer
                // on a bidirectional duct. Starting masses <= 1e9.
                const auto bias=(stratification_detail::riseFactor(a,b)*buoyancy).toInt();
                const Mass mean=(from+to)/2;
                return from-to+mean*bias/1000;
            });
    });
}
} // namespace aiverse::aether
