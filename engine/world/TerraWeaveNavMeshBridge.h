// TerraWeave -> existing genre-independent nav::Mesh incremental update.
// Uses the SAME Earthworks ground heights as render and collision; walkable
// columns and links are pure fixed/integer. No RTS/FPS-specific dependencies.
#pragma once
#include "engine/world/TerraWeaveEarthworks.h"
#include "engine/nav/NavMesh.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct EarthworksNavConfig {
    int32_t width=0,depth=0; // walkable columns
    int64_t originXcm=0,originZcm=0;
    int32_t cellCm=100;     // nav column size; 25..3200cm
    int32_t maxStepCm=35;   // 0..1000cm
    uint32_t maxSlopePermille=550; // rise/run <= 8000
    int32_t tileColumns=32;
};

inline void validateEarthworksNavConfig(const EarthworksNavConfig& c) {
    if(c.width<=0 || c.depth<=0 ||
       int64_t(c.width)*c.depth>262144 ||
       c.cellCm<25 || c.cellCm>3200 ||
       c.maxStepCm<0 || c.maxStepCm>1000 ||
       c.maxSlopePermille>8000 ||
       c.tileColumns<1 || c.tileColumns>1024 ||
       c.originXcm<-kWorldLimitCm ||
       c.originZcm<-kWorldLimitCm ||
       c.originXcm>kWorldLimitCm-int64_t(c.width)*c.cellCm ||
       c.originZcm>kWorldLimitCm-int64_t(c.depth)*c.cellCm)
        throw std::invalid_argument("TerraWeave invalid navmesh grid extent");
}

inline nav::Config earthworksNavMeshConfig(const EarthworksNavConfig& cfg) {
    validateEarthworksNavConfig(cfg);
    nav::Config out;
    out.width=cfg.width;
    out.depth=cfg.depth;
    out.originX=Fixed::fromRatio(cfg.originXcm,100);
    out.originZ=Fixed::fromRatio(cfg.originZcm,100);
    out.cell=Fixed::fromRatio(cfg.cellCm,100);
    out.tile=cfg.tileColumns;
    out.flat=false; // linked sloped spans merge inside a nav tile
    out.maxSpans=uint32_t(int64_t(cfg.width)*cfg.depth);
    return out;
}

// Optional blocked callback is from an immutable snapshot of authored roads,
// obstacles or NEREID water contact. Do not query mutable ECS/physics state
// from a navmesh worker; construction + incremental refresh run at owner phase.
class EarthworksNavSource final : public nav::Source {
public:
    using Blocked=std::function<bool(int32_t,int32_t)>;

    EarthworksNavSource(const EarthworksLayer& earth,
                        EarthworksNavConfig config,Blocked blocked={})
        :earth_(earth),cfg_(config),blocked_(std::move(blocked)) {
        validateEarthworksNavConfig(cfg_);
        const int64_t pad=earth_.spacingCm();
        if(cfg_.originXcm<-kWorldLimitCm+pad ||
           cfg_.originZcm<-kWorldLimitCm+pad ||
           cfg_.originXcm>kWorldLimitCm-pad-
               int64_t(cfg_.width)*cfg_.cellCm ||
           cfg_.originZcm>kWorldLimitCm-pad-
               int64_t(cfg_.depth)*cfg_.cellCm)
            throw std::invalid_argument(
                "TerraWeave navmesh grid too close to Earthworks world boundary");
        // Caller may provide a blocked callback for water, buildings and
        // caves. One column may contain no walkable span after digging.
    }
    const EarthworksNavConfig& config()const{return cfg_;}
    int32_t height(int32_t x,int32_t z)const {
        if(x<0 || z<0 || x>=cfg_.width || z>=cfg_.depth)
            throw std::out_of_range("TerraWeave navmesh column index");
        const int64_t px=cfg_.originXcm+
            int64_t(x)*cfg_.cellCm+cfg_.cellCm/2;
        const int64_t pz=cfg_.originZcm+
            int64_t(z)*cfg_.cellCm+cfg_.cellCm/2;
        return earth_.sampleHeightCm(px,pz);
    }
    bool walkable(int32_t x,int32_t z)const {
        return x>=0 && z>=0 && x<cfg_.width && z<cfg_.depth &&
            (!blocked_ || !blocked_(x,z));
    }
    void spans(int32_t x,int32_t z,
               std::vector<nav::Span>& out)const override {
        if(!walkable(x,z))return;
        const int32_t h=height(x,z);
        out.push_back({Fixed::fromRatio(h,100),0});
    }
    bool link(int32_t x0,int32_t z0,uint32_t i,
              int32_t x1,int32_t z1,uint32_t j)const override {
        if(i || j || !walkable(x0,z0) || !walkable(x1,z1) ||
           std::abs(int64_t(x0)-x1)+std::abs(int64_t(z0)-z1)!=1)
            return false;
        const int64_t delta=std::abs(
            int64_t(height(x0,z0))-height(x1,z1));
        return delta<=cfg_.maxStepCm &&
               delta*1000<=int64_t(cfg_.cellCm)*
                           cfg_.maxSlopePermille;
    }
private:
    const EarthworksLayer& earth_;
    EarthworksNavConfig cfg_;
    Blocked blocked_;
};

// Exact half-open centimetre bounds -> inclusive column bounds. Includes a
// neighbour ring because crossing links and portals depend on both sides.
// This works with the nav::Mesh::markDirty + refresh incremental contract.
inline void markEarthworksNavDirty(nav::Mesh& mesh,
                                  const EarthworksNavSource& source,
                                  RectCm dirty) {
    if(!mesh.built() || dirty.minX>=dirty.maxX ||
       dirty.minZ>=dirty.maxZ)return;
    const auto& c=source.config();
    const auto& navCfg=mesh.config();
    if(navCfg.width!=c.width || navCfg.depth!=c.depth ||
       navCfg.originX!=Fixed::fromRatio(c.originXcm,100) ||
       navCfg.originZ!=Fixed::fromRatio(c.originZcm,100) ||
       navCfg.cell!=Fixed::fromRatio(c.cellCm,100))
        throw std::invalid_argument("TerraWeave navmesh/source configuration mismatch");
    const auto floorDiv=[](int64_t n,int64_t d)->int64_t {
        int64_t q=n/d;
        if(n%d<0)--q;
        return q;
    };
    const int64_t x0=floorDiv(dirty.minX-c.originXcm,c.cellCm)-1;
    const int64_t z0=floorDiv(dirty.minZ-c.originZcm,c.cellCm)-1;
    const int64_t x1=floorDiv(dirty.maxX-1-c.originXcm,c.cellCm)+1;
    const int64_t z1=floorDiv(dirty.maxZ-1-c.originZcm,c.cellCm)+1;
    if(x1<0 || z1<0 || x0>=c.width || z0>=c.depth)return;
    mesh.markDirty(
        int32_t(std::clamp<int64_t>(x0,0,c.width-1)),
        int32_t(std::clamp<int64_t>(z0,0,c.depth-1)),
        int32_t(std::clamp<int64_t>(x1,0,c.width-1)),
        int32_t(std::clamp<int64_t>(z1,0,c.depth-1)));
}

inline bool refreshEarthworksNav(nav::Mesh& mesh,
                                const EarthworksNavSource& source,
                                std::span<const RectCm> edits,
                                std::string* error=nullptr){
    if(!mesh.built()){
        if(error)*error="TerraWeave navmesh must be built before incremental refresh";
        return false;
    }
    for(const auto& rect:edits)
        markEarthworksNavDirty(mesh,source,rect);
    return mesh.refresh(source,error);
}

} // namespace aiverse::terraweave
