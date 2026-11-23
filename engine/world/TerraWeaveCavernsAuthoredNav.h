#pragma once
// Caverns Stage27: author-stamp-aware native nav::Source.
// Captures exactly the SAME immutable volume snapshot used to regenerate
// presentation/collision. No RTS/FPS dependencies or second pathfinding.
#include "engine/world/TerraWeaveCavernsMultiNav.h"
#include "engine/world/TerraWeaveCavernsVolumeSnapshot.h"
#include <memory>
#include <stdexcept>
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
    return CaveNavSource(snapshot->settings,config,snapshot->entrances,
        std::move(terrainHeight),
        [snapshot=std::move(snapshot)](PointCm p) {
            return caveSnapshotFieldCm(*snapshot,p);
        });
}
} // namespace aiverse::terraweave::caverns
