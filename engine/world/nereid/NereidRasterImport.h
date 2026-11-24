// AIVERSE NEREID: generic, opt-in import of legacy water-cell rasters.
// Conversion is an editor/load-time operation, NEVER a simulation-tick system.
// Its input is a row-major binary raster, independent of game genre and renderer.
#pragma once
#include "NereidWater.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::lib::nereid {

struct WaterRasterImportConfig {
    Fixed cellMeters=4_fx;
    Fixed surfaceY=1_fx;
    Fixed bottomY=-5_fx;
    WaterKind kind=WaterKind::Lake; // raw baked cells do not retain sea/river identity
    int32_t priority=0;
    size_t maxRectangles=262144;
};
struct WaterRasterRectangle {
    int x0=0,z0=0,x1=0,z1=0; // half-open grid-cell coordinates
};

// Exact set cover of wet cells by disjoint, axis-aligned rectangles. Each row
// is run-length compressed; identical X intervals on consecutive rows merge.
// Deterministic row-major order and std::map, never unordered iteration.
inline std::vector<WaterRasterRectangle> rectangleWaterCells(
        const std::vector<uint8_t>& cells,int width,int height,
        size_t maxRectangles=262144) {
    if(width<=0 || height<=0 ||
       uint64_t(width)*uint64_t(height)!=uint64_t(cells.size()) ||
       uint64_t(cells.size())>4194304u || maxRectangles==0)
        throw std::invalid_argument("NEREID raster dimensions or limits invalid");
    std::vector<WaterRasterRectangle> rectangles;
    std::map<std::pair<int,int>,size_t> previous;
    for(int z=0;z<height;++z) {
        std::map<std::pair<int,int>,size_t> current;
        for(int x=0;x<width;) {
            if(!cells[size_t(z)*size_t(width)+size_t(x)]){++x;continue;}
            const int start=x;
            do{++x;}while(x<width && cells[size_t(z)*size_t(width)+size_t(x)]);
            const std::pair<int,int> span{start,x};
            const auto prior=previous.find(span);
            if(prior!=previous.end()) {
                rectangles[prior->second].z1=z+1;
                current.emplace(span,prior->second);
            }else{
                if(rectangles.size()>=maxRectangles)
                    throw std::length_error("NEREID raster exceeds maxRectangles");
                const size_t id=rectangles.size();
                rectangles.push_back({start,z,x,z+1});
                current.emplace(span,id);
            }
        }
        previous=std::move(current);
    }
    return rectangles;
}

inline std::vector<WaterBodyComponent> waterBodiesFromRaster(
        const std::vector<uint8_t>& cells,int width,int height,
        const WaterRasterImportConfig& cfg={}) {
    if(cfg.cellMeters<=Fixed{} || cfg.surfaceY<=cfg.bottomY)
        throw std::invalid_argument("NEREID raster requires positive cellMeters and depth");
    const auto rectangles=rectangleWaterCells(cells,width,height,cfg.maxRectangles);
    std::vector<WaterBodyComponent> bodies;
    bodies.reserve(rectangles.size());
    for(const auto& r:rectangles) {
        WaterBodyComponent b;
        b.kind=cfg.kind;
        b.footprint=WaterFootprint::Box;
        b.priority=cfg.priority;
        b.surfaceY=cfg.surfaceY;
        b.bottomY=cfg.bottomY;
        // Half-open grid cells become touching metre-space AABBs. The cell
        // centres are strictly inside their boxes; edges intentionally touch.
        const Fixed midX=Fixed::fromRatio(int64_t(r.x0)+r.x1,2);
        const Fixed midZ=Fixed::fromRatio(int64_t(r.z0)+r.z1,2);
        const Fixed halfX=Fixed::fromRatio(int64_t(r.x1)-r.x0,2);
        const Fixed halfZ=Fixed::fromRatio(int64_t(r.z1)-r.z0,2);
        b.center={midX*cfg.cellMeters,Fixed{},midZ*cfg.cellMeters};
        b.halfExtents={halfX*cfg.cellMeters,halfZ*cfg.cellMeters};
        bodies.push_back(b);
    }
    return bodies;
}

} // namespace aiverse::lib::nereid
