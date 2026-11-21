#pragma once
// Bounded, camera-independent PRESENTATION snapshot of canonical NEREID flow.
// No simulation state, GPU handles, saved world, or independent water solver.
#include <aiverselib/water/NereidRiverGrade.h>
#include <aiverselib/water/NereidVortices.h>
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

namespace aiverse::world {
struct NereidFlowBounds { double minX=0,minZ=0,maxX=0,maxZ=0; };
struct NereidFlowMapOptions {
    double cellMetres=2;
    uint32_t maxDimension=256;
    size_t maxTexels=16384;
    // Infinite water needs an explicit bounded, world-anchored patch.
    std::optional<NereidFlowBounds> infinitePatch;
};
struct NereidFlowMapSource {
    const lib::nereid::WaterBodyComponent& body;
    const lib::nereid::WaterGeometryComponent* geometry=nullptr;
    const lib::nereid::WaterRiverGradeComponent* grade=nullptr;
    const lib::nereid::WaterCurrentFieldComponent* currents=nullptr;
    const lib::nereid::WaterVortexFieldComponent* vortices=nullptr;
    float rapidStrength=0,rapidThreshold=3;
    // Optional existing host confluence/cascade/obstruction resolver. Borrowed
    // synchronously during build; never retained by the resulting snapshot.
    std::function<Vec3(Vec3,Vec3)> resolve;
};
struct NereidFlowMap {
    double minX=0,minZ=0,cellMetres=0;
    uint32_t width=0,height=0;
    // RG: world X/Z metres/sec. B: local rapid intensity. A: valid sample.
    std::vector<render::float4> texels;
    bool empty()const{return width==0||height==0||!std::isfinite(cellMetres)||
        cellMetres<=0||!std::isfinite(minX)||!std::isfinite(minZ)||
        texels.size()!=size_t(width)*height;}
    render::float4 rect(double originX=0,double originZ=0)const {
        if(empty()||!std::isfinite(originX)||!std::isfinite(originZ))return {};
        return {float(minX-originX),float(minZ-originZ),
            float(1/(width*cellMetres)),float(1/(height*cellMetres))};
    }
    // Same clamp + bilinear convention as the native linear GPU sampler.
    render::float4 sample(double x,double z)const {
        if(empty()||!std::isfinite(x)||!std::isfinite(z))return {};
        const double px=std::clamp((x-minX)/cellMetres-.5,0.,double(width-1));
        const double pz=std::clamp((z-minZ)/cellMetres-.5,0.,double(height-1));
        const auto ix=uint32_t(px),iz=uint32_t(pz);
        const auto jx=std::min(ix+1,width-1),jz=std::min(iz+1,height-1);
        const float fx=float(px-ix),fz=float(pz-iz);
        auto mix=[](render::float4 a,render::float4 b,float t){
            return render::float4{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,
                a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};};
        return mix(mix(texels[size_t(iz)*width+ix],texels[size_t(iz)*width+jx],fx),
            mix(texels[size_t(jz)*width+ix],texels[size_t(jz)*width+jx],fx),fz);
    }
};

inline std::optional<NereidFlowBounds> nereidFlowBounds(
        const NereidFlowMapSource& src,const NereidFlowMapOptions& options) {
    using namespace lib::nereid;
    if(!src.body.enabled||!validWaterBody(src.body))return std::nullopt;
    NereidFlowBounds b;
    if(src.geometry&&src.geometry->enabled) {
        if(!validWaterGeometry(*src.geometry))return std::nullopt;
        b={INFINITY,INFINITY,-INFINITY,-INFINITY};
        for(const auto& point:src.geometry->points) {
            const double radius=src.geometry->kind==WaterGeometryKind::RiverSpline?
                point.halfWidth.toDouble():0;
            const double x=point.x.toDouble(),z=point.z.toDouble();
            b.minX=std::min(b.minX,x-radius);b.maxX=std::max(b.maxX,x+radius);
            b.minZ=std::min(b.minZ,z-radius);b.maxZ=std::max(b.maxZ,z+radius);
        }
    }else if(src.body.footprint==WaterFootprint::Infinite) {
        if(!options.infinitePatch)return std::nullopt;
        b=*options.infinitePatch;
    }else {
        const double x=src.body.center.x.toDouble(),z=src.body.center.z.toDouble();
        const double rx=src.body.footprint==WaterFootprint::Box?
            src.body.halfExtents.x.toDouble():src.body.radius.toDouble();
        const double rz=src.body.footprint==WaterFootprint::Box?
            src.body.halfExtents.y.toDouble():src.body.radius.toDouble();
        b={x-rx,z-rz,x+rx,z+rz};
    }
    if(!std::isfinite(b.minX)||!std::isfinite(b.minZ)||!std::isfinite(b.maxX)||
       !std::isfinite(b.maxZ)||b.maxX<=b.minX||b.maxZ<=b.minZ||
       std::max({std::abs(b.minX),std::abs(b.minZ),std::abs(b.maxX),std::abs(b.maxZ)})>1e10)
        throw std::invalid_argument("NEREID flow-map bounds");
    return b;
}

inline Vec3 nereidSurfaceCurrentAt(const NereidFlowMapSource& src,Vec3 point) {
    using namespace lib::nereid;
    const auto local=gradedWaterBody(src.body,src.grade,point,src.geometry);
    // Sample the local SURFACE layer, including an inclined river's height.
    point.y=local.surfaceY-Fixed::fromRatio(1,10);
    const auto ordinary=waterCurrentWithVortices(local,src.geometry,src.currents,src.vortices,point);
    return src.resolve?src.resolve(point,ordinary):ordinary;
}

inline NereidFlowMap buildNereidFlowMap(const NereidFlowMapSource& src,
        const NereidFlowMapOptions& options={}) {
    if(!std::isfinite(options.cellMetres)||options.cellMetres<=0||
       options.cellMetres>10000||options.maxDimension<2||options.maxDimension>1024||
       options.maxTexels<4||options.maxTexels>1048576||
       !std::isfinite(src.rapidStrength)||src.rapidStrength<0||src.rapidStrength>1||
       !std::isfinite(src.rapidThreshold)||src.rapidThreshold<0||src.rapidThreshold>100)
        throw std::invalid_argument("NEREID flow-map budget/style");
    const auto bounds=nereidFlowBounds(src,options);if(!bounds)return {};
    NereidFlowMap out;out.cellMetres=options.cellMetres;
    // Doubling preserves one world lattice when a moving infinite patch
    // crosses an anchor. Camera and mesh LOD never choose finite-map samples.
    for(;;) {
        out.minX=std::floor(bounds->minX/out.cellMetres)*out.cellMetres;
        out.minZ=std::floor(bounds->minZ/out.cellMetres)*out.cellMetres;
        const double nx=std::max(2.,std::ceil((bounds->maxX-out.minX)/out.cellMetres));
        const double nz=std::max(2.,std::ceil((bounds->maxZ-out.minZ)/out.cellMetres));
        if(nx<=options.maxDimension&&nz<=options.maxDimension&&nx*nz<=options.maxTexels) {
            out.width=uint32_t(nx);out.height=uint32_t(nz);break;
        }
        out.cellMetres*=2;
    }
    out.texels.reserve(size_t(out.width)*out.height);
    for(uint32_t z=0;z<out.height;++z)for(uint32_t x=0;x<out.width;++x) {
        const auto fixed=[](double value){return Fixed::fromRaw(
            int64_t(std::llround(value*Fixed::kOne)));};
        Vec3 point{fixed(out.minX+(x+.5)*out.cellMetres),{},
            fixed(out.minZ+(z+.5)*out.cellMetres)};
        const auto flow=nereidSurfaceCurrentAt(src,point);
        const float vx=float(std::clamp(flow.x.toDouble(),-1000.,1000.));
        const float vz=float(std::clamp(flow.z.toDouble(),-1000.,1000.));
        const float foam=std::clamp((std::hypot(vx,vz)-src.rapidThreshold)*.35f*
            src.rapidStrength,0.f,1.f);
        out.texels.push_back({vx,vz,foam,1});
    }
    return out;
}
} // namespace aiverse::world
