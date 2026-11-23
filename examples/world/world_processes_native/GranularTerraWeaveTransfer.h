// GRANULAR <-> TerraWeave authoritative soil-volume transfer.
// One granular mass unit = one cubic centimetre of loose material ONLY for
// this explicit opt-in transfer. The GRANULAR renderer's visual depth is an
// independent display scale, not a second terrain height authority.
// Earthworks are changed solely through TerraWeave's canonical ECS API.
#pragma once
#include "aiverselib/world/GranularEcs.h"
#include "engine/world/TerraWeaveEcsAuthority.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace aiverse::examples::world_processes_native {

struct GranularTerraTransfer {
    Entity field=kNullEntity;
    std::uint16_t x=0,row=0;
    std::int64_t fieldOriginXcm=0,fieldOriginZcm=0;
    std::int32_t cellSizeCm=100;
    std::int32_t brushRadiusCm=200;
    std::int32_t brushDepthCm=1;
    terraweave::EarthworkMode mode=terraweave::EarthworkMode::Fill;
};

struct GranularTerraTransferResult {
    bool changed=false;
    std::uint32_t tick=0;
    std::uint64_t movedVolumeCm3=0,soilRevision=0;
    std::int64_t cellMassBefore=0,cellMassAfter=0;
};

inline bool valid(const GranularTerraTransfer& request){
    return request.field.valid()&&
           request.cellSizeCm>=10&&request.cellSizeCm<=10000&&
           request.brushRadiusCm>=1&&request.brushRadiusCm<=50000&&
           request.brushDepthCm>=1&&request.brushDepthCm<=20000&&
           request.fieldOriginXcm>=-90000000&&
           request.fieldOriginXcm<=90000000&&
           request.fieldOriginZcm>=-90000000&&
           request.fieldOriginZcm<=90000000&&
           (request.mode==terraweave::EarthworkMode::Fill||
            request.mode==terraweave::EarthworkMode::Dig);
}

// Must run at the owning authoritative fixed-tick ECS barrier, after
// GRANULAR.advance and before the host's TerraWeave read/publish phase.
// The host records this exact request for net/replay. No float Box3D or
// renderer state influences gameplay. A preflight copy checks the exact
// Earthworks moved volume before the canonical Terra ECS commit.
// Failed validation leaves both canonical granular mass and terrain unchanged.
inline bool transferGranularAndTerrain(
        World& world,const terraweave::TerraWeaveEcsAuthorityConfig& terra,
        const GranularTerraTransfer& request,std::uint32_t completedTick,
        GranularTerraTransferResult& result){
    if(!valid(request)||!world.alive(request.field))return false;
    const auto* field=static_cast<const World&>(world)
                          .tryGetConst<granular::Field>(request.field);
    const auto* soilState=world.resourceConst<terraweave::TerraWeaveAuthorityResource>();
    if(!field||!soilState||!granular::valid(*field)||
       !field->enabled||field->sampledTick!=completedTick||
       request.x>=field->width||request.row>=field->height)return false;
    const std::size_t cell=std::size_t(request.row)*field->width+request.x;
    if(field->solid[cell])return false;
    const auto x=request.fieldOriginXcm+
                 std::int64_t(request.x)*request.cellSizeCm+
                 request.cellSizeCm/2;
    const auto z=request.fieldOriginZcm+
                 std::int64_t(request.row)*request.cellSizeCm+
                 request.cellSizeCm/2;
    const auto ground=terraweave::materializeEcsTerrain(*soilState,terra);
    auto staged=ground;
    terraweave::EarthworkBrush brush;
    brush.centerXcm=x;brush.centerZcm=z;
    brush.radiusCm=request.brushRadiusCm;
    brush.depthCm=request.brushDepthCm;
    brush.mode=request.mode;
    // EarthworksLayer::sculpt has canonical per-grid falloff, maximum brush
    // radius, bedrock and height limits. Its calculated volume is the real
    // earth moved; avoid assuming brush area = pi*r^2.
    const auto preview=staged.sculpt(brush);
    const std::uint64_t moved=preview.movedVolumeCm3;
    const auto before=field->mass[cell];
    if(moved>std::uint64_t(granular::kMaxCellMass) ||
       moved>std::uint64_t(std::numeric_limits<std::int64_t>::max()))
        return false;
    const auto quantity=static_cast<std::int64_t>(moved);
    if(request.mode==terraweave::EarthworkMode::Fill) {
        if(quantity>before)return false;
    }else if(quantity>field->capacity-before)return false;
    GranularTerraTransferResult prepared{
        moved!=0,completedTick,moved,preview.revision,
        before,request.mode==terraweave::EarthworkMode::Fill?
            before-quantity:before+quantity
    };
    if(moved==0){result=prepared;return true;}

    // Pre-detach the granular COW pool BEFORE committing TerraWeave so an
    // allocation failure cannot occur after canonical soil publication.
    if(!world.prepareExistingWrite<granular::Field>())return false;
    // This function calls the native authoritative ECS TerraWeave API,
    // including its NEREID bank synchronization. It does not call the
    // renderer's TerrainMutationCoordinator or edit duplicate Earthworks.
    const auto committed=terraweave::excavateAuthoritativeEcs(
        world,terra,brush,completedTick,false);
    if(committed.earth.movedVolumeCm3!=moved)
        throw std::logic_error("GRANULAR TerraWeave transfer preflight mismatch");
    world.get<granular::Field>(request.field).mass[cell]=prepared.cellMassAfter;
    prepared.soilRevision=committed.earth.revision;
    result=prepared;
    return true;
}

} // namespace aiverse::examples::world_processes_native
