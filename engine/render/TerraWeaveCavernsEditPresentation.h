#pragma once
// Stage23: convert certified CPU edit candidates into a GeometryScene batch.
// This prepares CPU model/manifest sources only: it NEVER performs GPU upload,
// retirement or collision/navmesh commit.
#include "engine/render/TerraWeaveCavernsBatchGeometry.h"
#include "engine/world/TerraWeaveCavernsEditRuntime.h"
#include <stdexcept>
#include <vector>
namespace aiverse::terraweave::caverns {
inline CaveBatchPrepared prepareCaveEditPresentation(
    const std::vector<CavePreparedEdit>& candidates,
    uint64_t generatorVersion,
    CaveBatchSpec options={}) {
    if(!generatorVersion||candidates.empty())
        throw std::invalid_argument("Caverns edited presentation batch");
    std::vector<CaveBatchChunk> chunks;
    chunks.reserve(candidates.size());
    for(const auto& item:candidates){
        const auto& c=item.candidate;
        if(c.mesh.key!=item.dirty.key ||
           c.sourceRevision!=item.dirty.sourceRevision ||
           !c.renderRevision)
            throw std::invalid_argument("Caverns edited candidate revision");
        CaveBatchChunk chunk;
        chunk.mesh=c.mesh;
        chunk.generatorVersion=generatorVersion;
        chunk.revision=c.renderRevision;
        chunk.sourceRevision=c.sourceRevision;
        chunk.resolution=c.resolution;
        chunk.spacingCm=c.spacingCm;
        chunks.push_back(std::move(chunk));
    }
    return prepareCaveBatch(std::move(chunks),options);
}
// Suggested owner usage:
// bridge.publishReady([&](const std::vector<CavePreparedEdit>& candidates){
//     auto prepared=prepareCaveEditPresentation(candidates,generatorVersion);
//     // Do NOT return true until a real all-owner GPU + physics commit succeeds.
//     return host.stageAndCommit(std::move(prepared),candidates);
// });
// Never return true just because CPU staging succeeded.
} // namespace aiverse::terraweave::caverns
