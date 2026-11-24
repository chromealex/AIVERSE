// Opt-in host bridge. Capture on the committed PHYSIS owner thread; consume
// immutable copied samples on the SpectraFX owner thread. No ECS access at burst.
#pragma once
#include "engine/fx/SpectraDataInterfaces.h"
#include "engine/physis/PhysisPresentation.h"
#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace aiverse::world {

struct SpectraPhysisRegion {
    render::float3 origin{}; // world metres, X/Y/Z; not planet-local altitude
    float spacing=16;
    std::array<unsigned,3> dimensions{1,1,1};
};

class SpectraPhysisSnapshot final {
public:
    static constexpr size_t kMaxSamples=4096;
    // One region per host publication. Invalid regions / uninitialized PHYSIS
    // return null; never publish a half-filled snapshot. Capture after ECS jobs.
    static std::shared_ptr<const SpectraPhysisSnapshot> capture(
            const World& world, const SpectraPhysisRegion& region) {
        const auto* state=world.resourceConst<physis::State>();
        if(!state || !std::isfinite(region.spacing) ||
           region.spacing<0.01f || region.spacing>1.0e6f)return {};
        const std::array<float,3> origin{region.origin.x,region.origin.y,region.origin.z};
        size_t count=1;
        for(size_t axis=0;axis<3;++axis) {
            const auto n=region.dimensions[axis];
            if(n<1 || n>64 || count>kMaxSamples/n ||
               !std::isfinite(origin[axis]) || std::abs(origin[axis])>1.0e7f ||
               std::abs(double(origin[axis])+double(n-1)*region.spacing)>1.0e7)
                return {};
            count*=n;
        }
        auto result=std::shared_ptr<SpectraPhysisSnapshot>(new SpectraPhysisSnapshot);
        result->region_=region;
        result->tick_=state->tick;
        result->samples_.reserve(count);
        const auto fixed=[](double metres) {
            return Fixed::fromRaw(static_cast<int64_t>(std::llround(metres*Fixed::kOne)));
        };
        for(unsigned z=0;z<region.dimensions[2];++z)
            for(unsigned y=0;y<region.dimensions[1];++y)
                for(unsigned x=0;x<region.dimensions[0];++x) {
                    physis::NatureFieldSample value;
                    const Vec3 point{
                        fixed(double(origin[0])+double(x)*region.spacing),
                        fixed(double(origin[1])+double(y)*region.spacing),
                        fixed(double(origin[2])+double(z)*region.spacing)};
                    if(!physis::sampleNatureField(world,point,value) ||
                       value.tick!=result->tick_)return {};
                    result->samples_.push_back(value);
                }
        return result;
    }
    uint64_t tick() const {return tick_;}
    size_t size() const {return samples_.size();}

    // Nearest sampled cell, with half-spacing coverage around boundary nodes.
    // No extrapolation outside this bounded 3D region; missing channels retain
    // the graph's normal required/fallback semantics. No live World pointer.
    bool sample(std::string_view channel,render::float3 position,float& output) const {
        const std::array<float,3> origin{region_.origin.x,region_.origin.y,region_.origin.z};
        const std::array<float,3> point{position.x,position.y,position.z};
        std::array<size_t,3> index{};
        for(size_t axis=0;axis<3;++axis) {
            const double t=(double(point[axis])-origin[axis])/region_.spacing;
            if(!std::isfinite(t) || t<-.5 ||
               t>=double(region_.dimensions[axis])-.5)return false;
            index[axis]=size_t(std::floor(t+.5));
        }
        const auto& s=samples_[(index[2]*region_.dimensions[1]+index[1])*
                              region_.dimensions[0]+index[0]];
        Fixed v{};
        if(channel=="wind_x")v=s.windVelocity.x;
        else if(channel=="wind_y")v=s.windVelocity.y;
        else if(channel=="wind_z")v=s.windVelocity.z;
        else if(channel=="gravity_x")v=s.gravityAcceleration.x;
        else if(channel=="gravity_y")v=s.gravityAcceleration.y;
        else if(channel=="gravity_z")v=s.gravityAcceleration.z;
        else if(channel=="fire" && s.hasHazards)v=s.hazards.fire;
        else if(s.hasAtmosphere) {
            const auto& e=s.environment;
            if(channel=="temperature")v=e.temperature;
            else if(channel=="humidity")v=e.humidity;
            else if(channel=="rain")v=e.rain;
            else if(channel=="snow")v=e.snow;
            else if(channel=="wetness")v=e.wetness;
            else if(channel=="snowpack")v=e.snowpack;
            else if(channel=="ice")v=e.ice;
            else if(channel=="pressure_hpa")v=s.pressureHpa;
            else return false;
        } else return false;
        const double n=v.toDouble();
        if(!std::isfinite(n) || std::abs(n)>1.0e7)return false;
        output=float(n);
        return true;
    }
private:
    SpectraPhysisSnapshot()=default;
    SpectraPhysisRegion region_{};
    uint64_t tick_=0;
    std::vector<physis::NatureFieldSample> samples_;
};

// Call on the GraphRuntime owner thread. The registry owns the immutable
// snapshot via the callback. Null explicitly removes the provider (unload,
// rollback, disconnected environment); republish after any discontinuity.
inline bool publishSpectraPhysis(spectra::DataInterfaceRegistry& registry,
        std::shared_ptr<const SpectraPhysisSnapshot> snapshot,
        const std::string& provider="physis") {
    if(!spectra::instanceParameterNameOk(provider))return false;
    if(!snapshot) {registry.unbind(provider);return true;}
    return registry.bind(provider,[snapshot=std::move(snapshot)](
            std::string_view channel,render::float3 point,float& value) {
        return snapshot->sample(channel,point,value);
    });
}

} // namespace aiverse::world
