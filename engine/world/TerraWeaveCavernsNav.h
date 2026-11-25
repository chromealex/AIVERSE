#pragma once
// Cave surface holes -> existing TerraWeave EarthworksNavSource/ nav::Mesh.
// This blocks unsupported surface columns at the entrance; it does NOT
// make the interior of a vertical shaft traversable.
#include "engine/world/TerraWeaveCavernsEntrances.h"
#include "engine/world/TerraWeaveNavMeshBridge.h"
#include <span>
namespace aiverse::terraweave::caverns {
// Build immutable snapshot callback BEFORE constructing EarthworksNavSource.
// Preserve any other authored / NEREID blockers by combining with their
// existing callback in the host. Entrances must remain alive and unchanged
// until the next source rebuild/refresh.
inline terraweave::EarthworksNavSource::Blocked
surfaceEntranceBlocked(const terraweave::EarthworksNavConfig& cfg,
                       const std::vector<Entrance>& entrances,
                       terraweave::EarthworksNavSource::Blocked previous={}) {
    terraweave::validateEarthworksNavConfig(cfg);
    return [cfg,entrances,previous=std::move(previous)](int32_t x,int32_t z){
        if(previous&&previous(x,z))return true;
        if(x<0||z<0||x>=cfg.width||z>=cfg.depth)return true;
        const int64_t px=cfg.originXcm+int64_t(x)*cfg.cellCm+cfg.cellCm/2;
        const int64_t pz=cfg.originZcm+int64_t(z)*cfg.cellCm+cfg.cellCm/2;
        // Conservative column-level exclusion around open shaft.
        return surfaceHoleAt(entrances,px,pz);
    };
}
// Call after a successful terrain/physics publication, using one immutable
// nav source that includes the new holes. Failed refresh leaves nav dirty.
inline bool refreshCavernsSurfaceNav(nav::Mesh& mesh,
                                    const terraweave::EarthworksNavSource& source,
                                    std::span<const SurfaceRectCm> dirty,
                                    std::string* error=nullptr) {
    std::vector<terraweave::RectCm> edits;
    edits.reserve(dirty.size());
    for(const auto& d:dirty)
        edits.push_back({d.minX,d.minZ,d.maxX,d.maxZ});
    return terraweave::refreshEarthworksNav(mesh,source,edits,error);
}
} // namespace aiverse::terraweave::caverns
