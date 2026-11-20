// LumaWeave bounded 3D clustered light candidate index.
// World-metre axis-aligned cells are a first portable CPU/reference path;
// they are NOT a screen-space 16x16 tile/G-buffer GPU implementation.
// Every cluster stores a capped CSR fast list. On overflow the caller MUST
// scan ALL registry lights and apply exact per-receiver radius/cone tests.
// This fail-open correctness rule avoids silently losing light energy.
#pragma once
#include "LumaWeaveLights.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

struct LumaClusterConfig {
    float3 origin{};
    float3 cellSize{8,8,8};
    int nx=8,ny=4,nz=8;
    uint32_t maxFastPerCell=48;
    size_t maxFastReferences=1u<<20;
};

struct LumaClusterBuildStats {
    size_t clusters=0,sources=0;
    size_t occupied=0,overflows=0;
    size_t references=0,spatialTests=0;
    uint64_t registryRevision=0;
};

struct LumaClusterCandidates {
    std::span<const uint32_t> fastIndices{};
    bool requiresFullScan=true; // TRUE outside grid or on per-cluster cap/budget overflow
    uint32_t potentialCount=0;
};

// ABI suitable for a Metal/Vulkan read-only structured buffer.
// Flags bit0: the fast list was truncated, MUST use full light registry.
struct LumaGpuClusterEntry {
    uint32_t first=0,count=0,total=0,flags=0;
};
static_assert(sizeof(LumaGpuClusterEntry)==16);
struct LumaClusterCsrPacket {
    LumaClusterConfig config{};
    uint64_t sourceRevision=0;
    uint64_t buildGeneration=0;
    std::vector<LumaGpuClusterEntry> cells;
    std::vector<uint32_t> indices;
};

class LumaWeaveClusters {
public:
    static constexpr size_t kMaxCells=16384;
    static constexpr uint32_t kMaxFastPerCell=64;

    LumaClusterBuildStats rebuild(const LumaWeaveRegistry& sources,
                                  LumaClusterConfig config) {
        const auto finite=[](float3 v) {
            return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);
        };
        if(!finite(config.origin)||!finite(config.cellSize)||
           config.cellSize.x<.25f||config.cellSize.y<.25f||config.cellSize.z<.25f ||
           config.cellSize.x>10000||config.cellSize.y>10000||config.cellSize.z>10000||
           config.nx<1||config.ny<1||config.nz<1||
           config.nx>256||config.ny>256||config.nz>256||
           uint64_t(config.nx)*uint64_t(config.ny)*uint64_t(config.nz)>kMaxCells ||
           !config.maxFastPerCell || config.maxFastPerCell>kMaxFastPerCell ||
           !config.maxFastReferences ||config.maxFastReferences>kMaxCells*kMaxFastPerCell)
            throw std::invalid_argument("invalid LumaWeave cluster budget/volume");

        struct Bucket {std::vector<uint32_t> indices;uint32_t total=0;bool overflow=false;};
        const size_t n=size_t(config.nx)*size_t(config.ny)*size_t(config.nz);
        std::vector<Bucket> buckets(n);
        LumaClusterBuildStats stats;
        stats.clusters=n;
        stats.sources=sources.lights().size();
        stats.registryRevision=sources.revision();
        size_t used=0;
        const auto axis=[](float3 v,int a){return a==0?v.x:a==1?v.y:v.z;};
        const int dims[3]={config.nx,config.ny,config.nz};
        for(uint32_t id=0;id<sources.lights().size();++id) {
            const auto& light=sources.lights()[id];
            int lower[3]={},upper[3]={};
            bool intersectsGrid=true;
            for(int a=0;a<3;++a) {
                const double low=(double(axis(light.position,a))-light.radius-
                                  axis(config.origin,a))/axis(config.cellSize,a);
                const double high=(double(axis(light.position,a))+light.radius-
                                   axis(config.origin,a))/axis(config.cellSize,a);
                if(high<0 || low>=dims[a]){intersectsGrid=false;break;}
                lower[a]=int(std::clamp(std::floor(low),0.0,double(dims[a]-1)));
                upper[a]=int(std::clamp(std::floor(high),0.0,double(dims[a]-1)));
            }
            if(!intersectsGrid)continue;
            for(int z=lower[2];z<=upper[2];++z)
            for(int y=lower[1];y<=upper[1];++y)
            for(int x=lower[0];x<=upper[0];++x) {
                const float3 lo{config.origin.x+x*config.cellSize.x,
                                config.origin.y+y*config.cellSize.y,
                                config.origin.z+z*config.cellSize.z};
                const float3 hi=lo+config.cellSize;
                ++stats.spatialTests;
                double dsquared=0;
                for(int a=0;a<3;++a) {
                    const double p=axis(light.position,a);
                    const double d=std::max({double(axis(lo,a))-p,0.0,
                                             p-double(axis(hi,a))});
                    dsquared+=d*d;
                }
                if(dsquared>double(light.radius)*light.radius)continue;
                const size_t linear=size_t(x)+size_t(config.nx)*
                                    (size_t(y)+size_t(config.ny)*size_t(z));
                auto& b=buckets[linear];
                ++b.total;
                if(b.indices.size()<config.maxFastPerCell &&
                   used<config.maxFastReferences) {
                    b.indices.push_back(id);
                    ++used;
                } else b.overflow=true;
            }
        }
        std::vector<Cell> next;
        next.reserve(n);
        std::vector<uint32_t> nextIndices;
        nextIndices.reserve(used);
        for(const auto& b:buckets) {
            if(b.total)++stats.occupied;
            if(b.overflow)++stats.overflows;
            next.push_back({uint32_t(nextIndices.size()),uint32_t(b.indices.size()),
                            b.total,b.overflow});
            nextIndices.insert(nextIndices.end(),b.indices.begin(),b.indices.end());
        }
        stats.references=nextIndices.size();
        if(buildGeneration_==std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("LumaWeave cluster build generation exhausted");
        cells_.swap(next);
        fast_.swap(nextIndices);
        config_=config;
        revision_=sources.revision();
        ++buildGeneration_;
        return stats;
    }

    bool ready() const {return !cells_.empty();}
    uint64_t sourceRevision() const {return revision_;}
    uint64_t buildGeneration() const {return buildGeneration_;}
    LumaClusterConfig config() const {return config_;}
    size_t references() const {return fast_.size();}

    LumaClusterCsrPacket exportCsr(const LumaWeaveRegistry& registry) const {
        if (!ready() || registry.revision()!=revision_)
            throw std::logic_error("cannot export stale/unconfigured LumaWeave clusters");
        LumaClusterCsrPacket result;
        result.config=config_;
        result.sourceRevision=revision_;
        result.buildGeneration=buildGeneration_;
        result.indices=fast_;
        result.cells.reserve(cells_.size());
        for(const auto& c:cells_) result.cells.push_back(
            {c.first,c.count,c.total,c.overflow?1u:0u});
        return result;
    }

    LumaClusterCandidates query(float3 p,const LumaWeaveRegistry& sources) const {
        if(sources.revision()!=revision_)
            throw std::logic_error("stale LumaWeave cluster/light snapshot");
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            throw std::invalid_argument("nonfinite LumaWeave receiver position");
        if(!ready())return {};
        const auto index=[&](double v,double o,double cell,int dim)->int {
            const double rel=(v-o)/cell;
            if(rel<0 || rel>=dim)return -1;
            return int(rel);
        };
        const int x=index(p.x,config_.origin.x,config_.cellSize.x,config_.nx);
        const int y=index(p.y,config_.origin.y,config_.cellSize.y,config_.ny);
        const int z=index(p.z,config_.origin.z,config_.cellSize.z,config_.nz);
        if(x<0||y<0||z<0)return {}; // outside index: full slow path, never black
        const auto& cell=cells_[size_t(x)+size_t(config_.nx)*
                               (size_t(y)+size_t(config_.ny)*size_t(z))];
        if (!cell.count) return {{},cell.overflow,cell.total};
        return {std::span<const uint32_t>(fast_.data()+cell.first,cell.count),
                cell.overflow,cell.total};
    }

private:
    struct Cell {uint32_t first=0,count=0,total=0;bool overflow=false;};
    LumaClusterConfig config_{};
    std::vector<Cell> cells_;
    std::vector<uint32_t> fast_;
    uint64_t revision_=0,buildGeneration_=0;
};

} // namespace aiverse::render
