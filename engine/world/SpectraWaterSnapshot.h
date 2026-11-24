// Read-only NEREID Data Interfaces; explicit world geometry.
#pragma once
#include "engine/world/SpectraFieldRegion.h"
#include "engine/fx/SpectraDataInterfaces.h"
#include "engine/world/nereid/NereidQueryIndex.h"
#include "engine/world/nereid/NereidSphereWater.h"
#include <memory>
#include <optional>
#include <vector>
namespace aiverse::world {
enum class SpectraWaterGeometry { Plane, Sphere };
class SpectraWaterSnapshot final {
    struct Sample {
        bool found=false, submerged=false;
        Vec3 surface{},up{},flow{};
        Fixed depth{},thickness{};
    };
    SpectraFieldRegion region_{};
    uint64_t tick_=0;
    std::vector<Sample> samples_;
public:
    // Capture only during the committed ECS read phase. In mixed worlds
    // publish separate provider names for each geometry.
    static std::shared_ptr<const SpectraWaterSnapshot> capture(
        const World& world,const SpectraFieldRegion& region,
        SpectraWaterGeometry geometry,uint64_t tick,Fixed seconds={},
        Vec3 simulationOffset={},std::optional<Vec3> sphereCenter={}) {
        const size_t count=region.count();
        if(!count || !validSpectraFieldOffset(simulationOffset) ||
           (sphereCenter && !validSpectraFieldOffset(*sphereCenter)) ||
           (geometry!=SpectraWaterGeometry::Plane &&
                      geometry!=SpectraWaterGeometry::Sphere))return {};
        auto out=std::shared_ptr<SpectraWaterSnapshot>(new SpectraWaterSnapshot);
        out->region_=region;out->tick_=tick;out->samples_.reserve(count);
        std::unique_ptr<lib::nereid::WaterQueryIndex> plane;
        if(geometry==SpectraWaterGeometry::Plane)
            plane=std::make_unique<lib::nereid::WaterQueryIndex>(world);
        for(unsigned z=0;z<region.dimensions[2];++z)
            for(unsigned y=0;y<region.dimensions[1];++y)
                for(unsigned x=0;x<region.dimensions[0];++x) {
                    const auto p=region.point(x,y,z);
                    const Vec3 at=Vec3{p[0],p[1],p[2]}+simulationOffset;
                    Sample v;
                    if(plane) {
                        const auto s=plane->sample(at);
                        if(s.found) {
                            v.found=true;v.submerged=s.submerged;
                            v.surface=Vec3{at.x,s.surfaceY,at.z}-simulationOffset;
                            v.up={Fixed{},Fixed::fromInt(1),Fixed{}};
                            v.flow=s.flow;v.depth=s.depth;v.thickness=s.surfaceY-s.bottomY;
                        }
                    } else {
                        const auto s=lib::nereid::sampleSphereWaterFiltered(world,at,seconds,
                            sphereCenter?&*sphereCenter:nullptr);
                        if(s.found) {
                            v.found=true;v.submerged=s.submerged;v.surface=s.surfacePoint-simulationOffset;
                            v.up=s.up;v.flow=s.flow;v.depth=s.depth;
                            v.thickness=s.surfaceRadius-s.floorRadius;
                        }
                    }
                    out->samples_.push_back(v);
                }
        return out;
    }
    uint64_t tick()const{return tick_;}
    size_t size()const{return samples_.size();}
    bool sample(std::string_view channel,render::float3 point,float& output)const {
        size_t index=0;
        if(!region_.index(point,index))return false;
        const auto& s=samples_[index];
        if(channel=="found"){output=s.found?1.f:0.f;return true;}
        if(!s.found)return false;
        if(channel=="submerged"){output=s.submerged?1.f:0.f;return true;}
        Fixed v{};
        if(channel=="depth")v=s.depth;
        else if(channel=="thickness")v=s.thickness;
        else if(channel=="surface_x")v=s.surface.x;
        else if(channel=="surface_y")v=s.surface.y;
        else if(channel=="surface_z")v=s.surface.z;
        else if(channel=="up_x")v=s.up.x;
        else if(channel=="up_y")v=s.up.y;
        else if(channel=="up_z")v=s.up.z;
        else if(channel=="flow_x")v=s.flow.x;
        else if(channel=="flow_y")v=s.flow.y;
        else if(channel=="flow_z")v=s.flow.z;
        else return false;
        const double n=v.toDouble();
        if(!std::isfinite(n)||std::abs(n)>1e7)return false;
        output=float(n);return true;
    }
private:
    SpectraWaterSnapshot()=default;
};
inline bool publishSpectraWater(spectra::DataInterfaceRegistry& registry,
        std::shared_ptr<const SpectraWaterSnapshot> snapshot,
        const std::string& name="nereid") {
    if(!spectra::instanceParameterNameOk(name))return false;
    if(!snapshot){registry.unbind(name);return true;}
    return registry.bind(name,[snapshot=std::move(snapshot)](std::string_view c,render::float3 p,float& v){
        return snapshot->sample(c,p,v);
    });
}
}
