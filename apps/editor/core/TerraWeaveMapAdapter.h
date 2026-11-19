// Optional editor adapter: bake TerraWeave into the EXISTING MapDoc heightfield.
// The renderer and navmesh continue using their usual dirty-revision paths.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "engine/world/TerraWeaveHeightmap.h"
#include "engine/world/TerraWeaveLayer.h"
#include <string>
#include <stdexcept>
#include <utility>

namespace aiverse::editor {

enum class TerraWeaveExistingTerrain : uint8_t { RefuseOverwrite, ReplaceExplicitly };

inline void bakeTerraWeaveTerrain(MapDoc& doc,
        const terraweave::TerrainSettings& terrain,
        terraweave::LegacyHeightfieldOptions origin = {},
        TerraWeaveExistingTerrain overwrite =
            TerraWeaveExistingTerrain::RefuseOverwrite) {
    if (doc.data.planet())
        throw std::invalid_argument("TerraWeave flat MapDoc adapter does not support planets");
    // The existing detail surface wins over corners in the renderer.
    // Refuse hidden regeneration unless the caller explicitly accepts clearing it.
    const bool authoredSurface = !doc.data.corners.empty() ||
        (doc.data.detailScale > 0 && !doc.data.detail.empty());
    if (authoredSurface && overwrite == TerraWeaveExistingTerrain::RefuseOverwrite)
        throw std::invalid_argument("TerraWeave refuses to overwrite authored terrain/detail");
    // Build temporary buffer first; exceptions must never touch the live document.
    std::vector<int16_t> generated = terraweave::generateLegacyCorners(
        terrain, doc.data.w, doc.data.h, origin);
    doc.beginEdit();                  // undo snapshot
    doc.data.corners = std::move(generated);
    // Existing detail samples have higher render priority than corners.
    // Clearing them is necessary for the generated terrain to become visible.
    // MapDoc::beginEdit() captured the original detail/splat for Undo.
    doc.data.detailScale = 0;
    doc.data.detail.clear();
    doc.data.detailSplat.clear();
    doc.data.detailFile.clear();
    doc.touched();                    // existing view rebuild policy
    // Does not change rules.terrain.model or terrain.detail: the caller
    // explicitly chooses whether corners are gameplay or visual only.
}


// Rebuild the former procedural base and move the authored composite by
// (newBase - oldBase). Sparse sculpt changes therefore survive a generator
// seed/noise/origin edit, without introducing a duplicate terrain renderer.
// The base checksum protects against a changed generator implementation.
// A failed preflight leaves MapDoc, undo history and revision untouched.
struct TerraWeaveRebaseReport {
    size_t authoredCorners=0;
    size_t shiftedCorners=0;
    size_t shiftedDetail=0;
    std::string newBaseFingerprint;
};

inline TerraWeaveRebaseReport rebaseTerraWeaveTerrain(
        MapDoc& doc,
        const terraweave::TerrainSettings& previousTerrain,
        terraweave::LegacyHeightfieldOptions previousOrigin,
        const std::string& previousBaseFingerprint,
        const terraweave::TerrainSettings& nextTerrain,
        terraweave::LegacyHeightfieldOptions nextOrigin) {
    if(doc.data.planet() || !doc.data.fps.isNull())
        throw std::invalid_argument("TerraWeave rebase supports flat RTS maps only");
    const int w=doc.data.w, h=doc.data.h;
    const auto previous=terraweave::generateLegacyCorners(
        previousTerrain,w,h,previousOrigin);
    if(terraweave::legacyBaseFingerprint(previous,w,h)!=previousBaseFingerprint)
        throw std::invalid_argument(
            "TerraWeave previous recipe fingerprint changed; refusing to lose authored edits");
    const auto next=terraweave::generateLegacyCorners(
        nextTerrain,w,h,nextOrigin);
    const auto newFingerprint=terraweave::legacyBaseFingerprint(next,w,h);
    // Detail textures must remain paired with their in-memory height samples.
    const auto& data=doc.data;
    if((data.detailScale>0 && data.detail.empty()) ||
       (data.detailScale==0 && (!data.detail.empty() || !data.detailSplat.empty())) ||
       (!data.detailSplat.empty() &&
        data.detailSplat.size()!=data.detail.size()*4u))
        throw std::invalid_argument("TerraWeave rebase: inconsistent detail/splat layers");
    auto result=terraweave::rebaseLegacyHeightfield(
        data.corners,data.detail,w,h,data.detailScale,previous,next);
    const TerraWeaveRebaseReport report={
        result.authoredCorners,result.shiftedCorners,result.shiftedDetail,
        newFingerprint
    };
    // The only document mutations occur after ALL dimensions, generated
    // samples, fingerprints, detail data and R16 ranges have passed.
    doc.beginEdit();
    doc.data.corners=std::move(result.corners);
    if(!data.detail.empty())doc.data.detail=std::move(result.detail);
    // Keep author-painted detail splat, detailFile and scale byte-for-byte.
    doc.touched();
    return report;
}

} // namespace aiverse::editor
