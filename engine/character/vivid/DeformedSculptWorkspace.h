// VIVID editor-owned reusable scratch for morph + skin + ray BVH picking.
// No ownership of Model/GPU state; reset on successful candidate publication.
#pragma once
#include "DeformedSculptPick.h"
#include "SculptPickBvh.h"
#include "SculptSkinningPlan.h"
#include "SculptGpuPickContract.h"

namespace aiverse::vivid {

class DeformedSculptWorkspace final {
    const render::Model* model_ = nullptr; // identity only; never dereferenced
    std::vector<float> weights_;
    std::vector<render::float3> morphBind_,morphScratch_;
    SkinnedWeightSurface surface_;
    SkinnedWeightScratch skinScratch_; // retained for legacy skin picker compatibility
    SculptSkinningPlan skinPlan_;
    SculptPickBvh bvh_;
    std::uint64_t preparedFrame_=0, preparedPublication_=0;
public:
    DeformedSculptWorkspace()=default;
    DeformedSculptWorkspace(const DeformedSculptWorkspace&)=delete;
    DeformedSculptWorkspace& operator=(const DeformedSculptWorkspace&)=delete;

    void reset() noexcept {
        model_=nullptr;
        preparedFrame_=preparedPublication_=0;
        bvh_.clear();
        skinPlan_.clear();
        surface_.ready=false;
        // Keep vector capacities for a later similarly sized asset.
        weights_.clear();
        morphBind_.clear();
        morphScratch_.clear();
        skinScratch_.staging.ready=false;
    }
    const SculptPickBvhStats& stats() const noexcept {return bvh_.stats();}
    const SkinnedWeightSurface& worldSurface() const noexcept {return surface_;}
    std::span<const render::float3> morphedBindPositions() const noexcept {
        return morphBind_;
    }
    std::size_t vertexCapacity() const noexcept {
        return morphBind_.capacity()+morphScratch_.capacity()+
               surface_.world.capacity()+skinScratch_.staging.world.capacity();
    }
    // Called only after SculptGpuPickProtocol has validated its exact
    // request token/actor revision/frame/viewport and decoded the triangle
    // ID. The CPU surface must belong to the very same captured pose.
    // This does NOT perform GPU readback or re-run the GPU depth test.
    SculptPick resolveGpuTriangle(const render::Model& model,
                                  std::size_t meshId,
                                  std::uint32_t triangleOrdinal,
                                  render::float3 origin,
                                  render::float3 direction,
                                  std::size_t sourceVertexCount,
                                  std::span<const std::uint32_t> authorIds={},
                                  std::uint64_t capturedFrame=0,
                                  std::uint64_t publicationSerial=0) const noexcept {
        if(!capturedFrame || !publicationSerial ||
           capturedFrame!=preparedFrame_ ||
           publicationSerial!=preparedPublication_ ||
           model_!=&model || !skinPlan_.ready() || !surface_.ready ||
           surface_.mesh!=meshId || bvh_.mesh()!=meshId)
            return {};
        return resolveSculptGpuTriangle(model,surface_,triangleOrdinal,
                                       origin,direction,sourceVertexCount,
                                       authorIds);
    }
    SculptPick pick(const render::Model& model,
                    std::size_t meshId,
                    std::span<const render::mat4> nodeWorld,
                    const SparseMorphLibrary& morphs,
                    const FaceShapeBinding& binding,
                    const FaceFrame& face,
                    render::float3 origin,
                    render::float3 direction,
                    std::size_t sourceVertexCount,
                    std::span<const std::uint32_t> sourceVertexIds={},
                    std::uint64_t preparedFrame=0,
                    std::uint64_t publicationSerial=0) {
        SculptPick noHit;
        if((preparedFrame==0)!=(publicationSerial==0))
            return noHit;
        if(sourceVertexCount==0 ||
           sourceVertexCount>model.vertices.size() ||
           binding.targets()!=morphs.targets() ||
           (!sourceVertexIds.empty() &&
            sourceVertexIds.size()!=model.vertices.size()))
            return noHit;
        if(model_!=&model || !bvh_.ready() ||
           bvh_.mesh()!=meshId || bvh_.vertices()!=model.vertices.size()) {
            bvh_.clear();
            model_=nullptr;
            if(!bvh_.build(model,meshId) ||
               !skinPlan_.build(model,meshId)){
                bvh_.clear();
                skinPlan_.clear();
                return noHit;
            }
            model_=&model;
        }
        weights_.resize(morphs.targets());
        if(!binding.evaluate(face,weights_))return noHit;
        if(!prepareSculptMorphPositions(model,morphs,weights_,
                                        morphBind_,&morphScratch_))
            return noHit;
        if(!skinPlan_.evaluate(model,nodeWorld,morphBind_,
                               surface_,skinScratch_.staging))return noHit;
        if(!bvh_.refit(surface_))return noHit;
        preparedFrame_=preparedFrame;
        preparedPublication_=publicationSerial;
        return bvh_.pick(surface_,origin,direction,
                         sourceVertexCount,sourceVertexIds);
    }
};
} // namespace aiverse::vivid
