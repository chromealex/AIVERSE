#include "StrataModelAdapter.h"
#include "StrataSourceSeams.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace aiverse::render::strata {
namespace {
constexpr float kWeights[15] = {
    0.5f, 0.5f, 0.5f,  // vertex normals
    1.f, 1.f,          // UV0
    0.25f, 0.25f, 0.25f, 0.25f, // tangent + handedness
    1.f, 1.f, 1.f, 1.f, // vertex RGBA
    1.f, 1.f           // packed lightmap UV1
};

std::vector<std::array<float, 15>> attributesFor(const Model& model) {
    std::vector<std::array<float, 15>> attributes;
    attributes.reserve(model.vertices.size());
    for (const auto& v : model.vertices) {
        const uint32_t lmU = uint32_t(v.joints[0]) | (uint32_t(v.joints[1]) << 8);
        const uint32_t lmV = uint32_t(v.joints[2]) | (uint32_t(v.joints[3]) << 8);
        attributes.push_back({
            v.nrm.x, v.nrm.y, v.nrm.z, v.uv.x, v.uv.y,
            v.tangent.x, v.tangent.y, v.tangent.z, v.tangent.w,
            float((v.color >> 0) & 255u) / 255.f,
            float((v.color >> 8) & 255u) / 255.f,
            float((v.color >> 16) & 255u) / 255.f,
            float((v.color >> 24) & 255u) / 255.f,
            float(lmU) / 65535.f, float(lmV) / 65535.f
        });
    }
    return attributes;
}
} // namespace

bool buildModelAsset(const Model& model, const BuildOptions& options,
                     ModelAsset& out, std::string* error,
                     bool allowPreparedRootLods) {
    if (error) error->clear();
    ModelAsset next;
    // Presentation-only integration: GeometryScene's immutable rigid CPU
    // source is the supported path. Never create independent animated copies.
    const bool modelEligible = model.skins.empty() && model.clips.empty() &&
        !model.vb.valid() && !model.ib.valid() &&
        !model.vertices.empty() && !model.indices.empty();
    const bool hasMutableNodes = [&]() {
        for (const auto& node : model.nodes)
            if (node.runtimeMutable || node.skin >= 0) return true;
        return false;
    }();
    std::vector<std::array<float, 15>> attributes;
    bool builtAttributes = false;
    std::set<size_t> rootMeshes;
    if (allowPreparedRootLods)
        for (const auto& node : model.nodes)
            if (node.mesh >= 0 && size_t(node.mesh) < model.meshes.size())
                rootMeshes.insert(size_t(node.mesh));
    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        // After prepareModelUpload, automatically generated LOD alternative
        // meshes are not original sources unless a real Model node uses them.
        if (allowPreparedRootLods && !rootMeshes.count(mi)) continue;
        const auto& mesh = model.meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            const auto& p = mesh.primitives[pi];
            const char* reason = nullptr;
            if (!modelEligible) reason = "requires immutable non-uploaded rigid CPU Model";
            else if (hasMutableNodes || mesh.deformable)
                reason = "mutable/deformable model is not supported";
            else if (!allowPreparedRootLods && !mesh.lods.empty())
                reason = "existing authored/generated mesh LOD must be preserved";
            else if (!p.jointPalette.empty() || p.morphTargets ||
                     !p.variantMaterial.empty())
                reason = "skin, morph or material variants are not supported";
            else if (p.material < 0 || size_t(p.material) >= model.materials.size())
                reason = "invalid material index";
            else {
                const auto& material = model.materials[size_t(p.material)];
                if (material.alpha != AlphaMode::Opaque || material.transparent ||
                    material.shaderGraph)
                    reason = "non-opaque or custom shader material";
            }
            if (!reason && (p.indexCount == 0 || p.indexCount % 3 != 0 ||
                            size_t(p.firstIndex) > model.indices.size() ||
                            size_t(p.indexCount) >
                                model.indices.size() - size_t(p.firstIndex)))
                reason = "invalid primitive index range";
            if (reason) {
                next.skipped.push_back({
                    uint32_t(mi), uint32_t(pi), reason
                });
                continue;
            }
            if (!builtAttributes) {
                attributes = attributesFor(model);
                builtAttributes = true;
            }
            BuildInput input;
            input.indices = model.indices.data() + p.firstIndex;
            input.indexCount = p.indexCount;
            input.positions = &model.vertices.front().pos.x;
            input.vertexCount = model.vertices.size();
            input.positionStride = sizeof(ModelVertex);
            input.attributes = attributes.front().data();
            input.attributeStride = sizeof(attributes.front());
            input.attributeWeights = kWeights;
            input.attributeComponents = 15;
            input.materialGroup = uint32_t(p.material);
            ModelPrimitiveAsset prepared;
            prepared.mesh = uint32_t(mi);
            prepared.primitive = uint32_t(pi);
            prepared.material = uint32_t(p.material);
            std::string why;
            if (!build(input, options, prepared.geometry, &why)) {
                if (error) *error = "mesh=" + std::to_string(mi) +
                    ", primitive=" + std::to_string(pi) + ": " + why;
                return false; // Preserve old output on failure
            }
            next.primitives.push_back(std::move(prepared));
        }
    }
    if(!next.primitives.empty()){
        SourceSeamAudit seamReport{};
        std::string seamError;
        if(!auditModelSourceSeams(model,next,seamReport,&seamError)){
            if(error)*error="source-wide seam validation: "+seamError;
            return false; // preserve the caller's previous asset on failure
        }
        next.sourceSeamsCertified=true;
        next.sharedGeometricBoundarySegments=seamReport.matchedSegments;
    }
    out = std::move(next);
    return true;
}

bool SceneStaging::stage(const std::string& key, const Model& model) {
    // Model IDs are unique within a scene and the current GeometryScene
    // owns their lifetime. Refuse duplicate staging instead of rebuilding.
    if (key.empty()) {
        ++stats_.failedSources;
        warnings_.push_back("Strata staging: source key must not be empty");
        return false;
    }
    if (assets_.find(key) != assets_.end()) return true;
    if (model.vertices.size() > limits_.maxVerticesPerSource ||
        model.indices.size() > limits_.maxIndicesPerSource) {
        ++stats_.skippedSources;
        warnings_.push_back("Strata staging skipped '" + key +
                            "': source exceeds staging geometry budget");
        return false;
    }
    ModelAsset built;
    std::string error;
    if (!buildModelAsset(model, options_, built, &error,
                         allowPreparedRootLods_)) {
        ++stats_.failedSources;
        warnings_.push_back("Strata staging failed '" + key + "': " + error);
        return false;
    }
    stats_.skippedPrimitives += uint32_t(built.skipped.size());
    if (built.primitives.empty()) {
        ++stats_.skippedSources;
        return false;
    }
    if(!built.sourceSeamsCertified){
        ++stats_.failedSources;
        warnings_.push_back("Strata staging '" + key +
            "': cross-material/source seam topology is not certified");
        return false;
    }
    for (const auto& primitive : built.primitives) {
        // A new builder is REQUIRED to supply exact seam certification
        // before this asset is ever used by production STRATA. Raw legacy
        // GPU/direct test fixtures can still use their separate APIs.
        if(!primitive.geometry.seamCertified) {
            ++stats_.failedSources;
            warnings_.push_back("Strata staging '" + key +
                "': LOD seam topology is not certified; retain Legacy");
            return false;
        }
    }
    for (const auto& primitive : built.primitives) {
        ++stats_.stagedPrimitives;
        stats_.clusterNodes += primitive.geometry.nodes.size();
        stats_.meshlets += primitive.geometry.meshlets.size();
        stats_.indexBytes += primitive.geometry.indices.size() * sizeof(uint32_t);
        ++stats_.seamCertifiedPrimitives;
        stats_.seamRejectedParents += primitive.geometry.seamRejectedParents;
    }
    stats_.crossMaterialMatchedSeams+=
        built.sharedGeometricBoundarySegments;
    ++stats_.crossMaterialCertifiedSources;
    assets_.emplace(key, std::move(built));
    ++stats_.stagedSources;
    return true;
}

void SceneStaging::clear() {
    assets_.clear();
    warnings_.clear();
    stats_ = {};
}

size_t SceneStaging::discardArchivedCpuIndices(const std::string& key) {
    const auto it=assets_.find(key);
    if(it==assets_.end())return 0;
    size_t released=0;
    for(auto& entry:it->second.primitives)
        released+=entry.geometry.discardCpuIndexPayload();
    return released;
}

const ModelAsset* SceneStaging::find(const std::string& key) const {
    const auto found = assets_.find(key);
    return found == assets_.end() ? nullptr : &found->second;
}

} // namespace aiverse::render::strata
