#pragma once
// Caverns Stage27: author-stamp-aware native nav::Source.
// Captures exactly the SAME immutable volume snapshot used to regenerate
// presentation/collision. No RTS/FPS dependencies or second pathfinding.
#include "engine/world/TerraWeaveCavernsMultiNav.h"
#include "engine/world/TerraWeaveCavernsSdfSpatialIndex.h"
#include <memory>
#include <stdexcept>
#include <utility>
namespace aiverse::terraweave::caverns {
// The callback owns the immutable snapshot for the full nav build/refresh
// lifetime, including when a shadow nav::Mesh is prepared off-screen.
// All edits must produce a new snapshot and a new nav source revision.
inline CaveNavSource makeCaveAuthoredNavSource(
    std::shared_ptr<const CaveVolumeSnapshot> snapshot,
    CaveNavConfig config,CaveNavSource::Height terrainHeight) {
    if(!snapshot||!snapshot->sourceRevision||!snapshot->generatorVersion)
        throw std::invalid_argument("Caverns authored nav snapshot");
    if(!terrainHeight)throw std::invalid_argument("Caverns authored terrain height");
    // Build the index once for the entire native NavMesh rebuild, not
    // once per nav sample. The callback retains it for the source lifetime.
    auto indexed=std::make_shared<const CaveSnapshotSpatialIndex>(snapshot);
    const Settings settings=snapshot->settings;
    const auto entrances=snapshot->entrances;
    auto density=[indexed=std::move(indexed)](PointCm p) {
        return indexed->field(p);
    };
    return CaveNavSource(settings,config,entrances,
        std::move(terrainHeight),std::move(density));
}
// A game owner that already has the indexed immutable SDF can avoid a
// second spatial hash build when preparing native navigation.
inline CaveNavSource makeCaveAuthoredNavSource(
    std::shared_ptr<const CaveSnapshotSpatialIndex> indexed,
    CaveNavConfig config,CaveNavSource::Height terrainHeight) {
    if(!indexed||!indexed->revision()||!terrainHeight)
        throw std::invalid_argument("Caverns indexed native nav source");
    const Settings settings=indexed->snapshot().settings;
    const auto entrances=indexed->snapshot().entrances;
    auto density=[frozen=std::move(indexed)](PointCm p){
        return frozen->field(p);
    };
    return CaveNavSource(settings,config,entrances,
        std::move(terrainHeight),std::move(density));
}

} // namespace aiverse::terraweave::caverns
