#pragma once
// Opt-in bridge from AIVERSE's existing CPU Model to Strata import assets.
// This deliberately does not create render buffers or submit draw commands.
#include "StrataBuilder.h"
#include "Model.h"

#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace aiverse::render::strata {

struct ModelPrimitiveAsset {
    uint32_t mesh = 0;
    uint32_t primitive = 0;
    uint32_t material = 0;
    Asset geometry; // owned CPU indices/meshlets/hierarchy
};

struct ModelPrimitiveSkip {
    uint32_t mesh = 0;
    uint32_t primitive = 0;
    std::string reason;
};
struct ModelAsset {
    std::vector<ModelPrimitiveAsset> primitives;
    std::vector<ModelPrimitiveSkip> skipped;
    // Stage17.2: only buildModelAsset's source-wide positional seam audit
    // may certify compatibility between different material/mesh partitions.
    bool sourceSeamsCertified=false;
    uint64_t sharedGeometricBoundarySegments=0;
};

// Asset extraction is read-only and transactional. Preserves LOD0, node
// hierarchy, material IDs, geometry attributes and all Model indices.
// Excludes incompatible dynamic, translucent and skin/morph inputs.
// Does NOT automatically activate Strata in the production renderer.
bool buildModelAsset(const Model& model, const BuildOptions& options,
                     ModelAsset& out, std::string* error = nullptr,
                     bool allowPreparedRootLods = false);

// Per-GeometryScene optional CPU staging. Keeps exactly one Strata asset per
// *source model*, independent of the number of scene instances. No GPU handles,
// draw submission, ECS state, renderer ownership or per-frame work.
struct StagingStats {
    uint32_t stagedSources = 0;
    uint32_t skippedSources = 0;
    uint32_t failedSources = 0;
    uint32_t stagedPrimitives = 0;
    uint32_t skippedPrimitives = 0;
    uint64_t clusterNodes = 0;
    uint64_t meshlets = 0;
    uint64_t indexBytes = 0; // auxiliary Strata flat index storage only
    // Stage17: offline-only seam certification; not an estimate of
    // actual VRAM or shader-level pixel/depth parity.
    uint64_t seamCertifiedPrimitives = 0, seamRejectedParents = 0;
    uint64_t crossMaterialMatchedSeams = 0;
    uint32_t crossMaterialCertifiedSources = 0;
};

class SceneStaging {
public:
    // Conservative bounds cap staging work on unexpectedly massive models.
    // A skipped or failed source never prevents the existing renderer drawing it.
    struct Limits {
        size_t maxVerticesPerSource = 250000;
        size_t maxIndicesPerSource = 750000;
    };
    SceneStaging() = default;
    explicit SceneStaging(const BuildOptions& options) : options_(options) {}
    bool stage(const std::string& key, const Model& model);
    void clear();
    // Offline/editor export of staged, immutable index-page archives. Writes
    // one deterministic hashed filename per source key. Never performs GPU
    // allocation or uses game-thread rendering resources.
    bool writeDiskArchives(const std::string& directory,
                           std::vector<std::string>* failures=nullptr) const;
    // The owning scene calls this ONLY after an archive has been verified
    // AND pinned roots uploaded, and only without GPU metadata/diagnostic
    // consumers that require the full CPU index payload. Returns bytes
    // physically released from the staged STRATA asset (not Model buffers).
    size_t discardArchivedCpuIndices(const std::string& key);
    const ModelAsset* find(const std::string& key) const;
    const StagingStats& stats() const { return stats_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    void setLimits(Limits value) { limits_ = value; }
    // Portable mode prepares the CPU model BEFORE staging, then stages only
    // meshes referenced directly by source nodes, not generated LOD children.
    // Default false retains older diagnostic import exclusion rules.
    void setAllowPreparedRootLods(bool value) { allowPreparedRootLods_ = value; }
private:
    BuildOptions options_{};
    Limits limits_{};
    StagingStats stats_{};
    bool allowPreparedRootLods_ = false;
    std::map<std::string, ModelAsset> assets_;
    std::vector<std::string> warnings_;
};

} // namespace aiverse::render::strata
