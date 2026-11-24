// Read-only ECS -> NEREID presentation bridge. No ECS mutation or
// genre-specific state; the caller chooses when to publish the snapshot.
#pragma once
#include "engine/world/NereidWaterScene.h"
#include "engine/world/nereid/NereidWaterSystems.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::world {
struct NereidEcsPresentationOptions {
    lib::nereid::WaterSurfaceMeshOptions mesh{};
    lib::nereid::WaterSurfaceStyle style{};
    bool includeDisabled=false;
};

inline std::vector<NereidSceneSurface> snapshotNereidEcsWater(
        const World& world,
        const NereidEcsPresentationOptions& options={}) {
    std::vector<NereidSceneSurface> result;
    world.eachExistingConst<lib::nereid::WaterBodyComponent>(
        [&](Entity e,const lib::nereid::WaterBodyComponent& body) {
            if(!world.alive(e)||(!body.enabled&&!options.includeDisabled))return;
            if(result.size()==NereidWaterScene::kMaxSurfaces)
                throw std::invalid_argument("nereid.ecs: too many visible water bodies");
            NereidSceneSurface surface;
            // Entity::packed includes generation; +1 reserves zero as the
            // legacy manifest/index identifier sentinel.
            const auto packed=uint64_t(e.packed());
            if(packed==UINT64_MAX)
                throw std::invalid_argument("nereid.ecs: invalid packed entity");
            surface.id=packed+1;
            surface.body=body;
            surface.mesh=options.mesh;
            surface.style=options.style;
            if(const auto* grade=world.tryGetConst<lib::nereid::WaterRiverGradeComponent>(e)){surface.mesh.useGrade=grade->enabled;surface.mesh.grade=*grade;}
            if(const auto* eddies=world.tryGetConst<lib::nereid::WaterVortexFieldComponent>(e)) {
                surface.useVortices=true;
                surface.vortices=*eddies;
            }
            if(const auto* field=world.tryGetConst<lib::nereid::WaterCurrentFieldComponent>(e)) {
                surface.useCurrentField=true;
                surface.currentField=*field;
            }
            if(const auto* geometry=world.tryGetConst<lib::nereid::WaterGeometryComponent>(e)) {
                surface.useGeometry=true;
                surface.geometry=*geometry;
            }
            result.push_back(std::move(surface));
        });
    std::sort(result.begin(),result.end(),
              [](const auto& a,const auto& b){return a.id<b.id;});
    return result;
}
} // namespace aiverse::world
