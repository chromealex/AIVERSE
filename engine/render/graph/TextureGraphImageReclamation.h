// Explicit host-proven reachability for procedural model images.
// No extra GPU owner. Existing rollback pool retires detached textures.
#pragma once
#include "TextureGraphMaterialBinding.h"
#include <limits>
#include <span>
#include <type_traits>

namespace aiverse::render::texture_graph {
inline constexpr size_t maxLiveProceduralImages=24;
inline constexpr size_t maxHistoricalProceduralSlots=4096;

struct GraphImageRoots {
    // Host certification: ALL current and retained material copies, snapshot
    // pins, queued packets, LOD/attachment consumers and any raw index/handle
    // holders for this Model are enumerated or detached. No concurrent writer
    // or pending image transaction. An incomplete set must never collect.
    bool complete=false;
    size_t activeReaders=std::numeric_limits<size_t>::max();
    std::span<const std::span<const Material>> extra{};
};
struct GraphImageReclaimResult {
    size_t detached=0,live=0,tombstones=0,pendingRetirement=0;
};
inline bool graphGeneratedImage(const Image& image){
    return image.proceduralGraphImage||image.name.rfind("#texture-graph/",0)==0;
}
inline GraphImageReclaimResult graphImageStats(const Model& model,const ModelImageRollbackPool& rollback){
    GraphImageReclaimResult result;
    for(const auto& image:model.images)if(graphGeneratedImage(image)){
        if(image.proceduralGraphRetired)++result.tombstones;else ++result.live;
    }
    result.pendingRetirement=rollback.pendingImages();return result;
}
// Existing base materials are always roots. The explicit extra spans are
// borrowed only during this synchronous joined frame-boundary operation.
// This is a CPU reachability proof, NOT a GPU fence: detach first, then
// defer destruction through the existing pool, never destroyTexture here.
inline GraphImageReclaimResult reclaimGraphImages(Model& model,gfx::Device& device,
        const RenderDeviceLifetime::Lease& lease,ModelImageRollbackPool& rollback,
        const GraphImageRoots& roots){
    require(roots.complete&&roots.activeReaders==0,"complete detached material roots and joined readers required");
    require(lease.validFor(device)&&device.gpuHealthy(),"graph image reclamation requires healthy device lease");
    require(!rollback.unrecoverableBatches(),"image retirement pool has unrecoverable batches");
    auto result=graphImageStats(model,rollback);
    std::vector<bool> referenced(model.images.size());
    auto mark=[&](int index){
        if(index==-1)return;
        require(index>=0&&size_t(index)<model.images.size(),"invalid material image reference during reclamation");
        require(!model.images[size_t(index)].proceduralGraphRetired,"retired graph image still referenced");
        referenced[size_t(index)]=true;
    };
    auto scan=[&](std::span<const Material> materials){
        for(const auto& m:materials){
            for(int i:{m.baseTex,m.mrTex,m.normalTex,m.occlusionTex,m.emissiveTex,m.teamTex})mark(i);
            for(int i:m.graphTextures)mark(i);
        }
    };
    scan(model.materials);for(auto materials:roots.extra)scan(materials);
    std::vector<size_t> indices;indices.reserve(maxLiveProceduralImages);
    for(size_t i=0;i<model.images.size();++i){
        const auto& image=model.images[i];
        if(referenced[i]||!image.proceduralGraphImage||image.proceduralGraphRetired)continue;
        // Do not infer ownership from a name, or touch borrowed/shared/VT images.
        if(!image.tex.valid()||image.borrowedTexture||image.textureCache||image.textureLease||
           !image.textureKey.empty()||image.virtualAlphaMask.valid()||image.virtualFallbackMaxDimension||
           image.virtualFallbackPreserveAlpha||image.gpuCompressed||image.compressed)continue;
        if(image.tex==model.morphTex||image.tex==model.baked.tex)continue;
        bool alias=false;
        for(size_t j=0;j<model.images.size();++j)if(j!=i&&
            (model.images[j].tex==image.tex||model.images[j].virtualAlphaMask==image.tex)){alias=true;break;}
        if(!alias)indices.push_back(i);
    }
    if(indices.empty())return result;
    // Backpressure includes all pending images in this shared pool; the
    // conservative host-wide bound may defer reclamation across models.
    if(result.pendingRetirement>=maxLiveProceduralImages||
       indices.size()>maxLiveProceduralImages-result.pendingRetirement)
        throw std::runtime_error("graph image retirement pool backpressured");
    ModelImageRollbackPool::Batch batch;batch.reserve(indices.size());
    static_assert(std::is_nothrow_move_constructible_v<Image>&&std::is_nothrow_move_assignable_v<Image>);
    for(auto i:indices){
        auto& image=model.images[i];
        batch.push_back(std::move(image));
        image=Image{};image.proceduralGraphImage=true;image.proceduralGraphRetired=true;
    }
    model.markMaterialChanged();
    rollback.retire(device,std::move(batch));
    result=graphImageStats(model,rollback);result.detached=indices.size();return result;
}
} // namespace aiverse::render::texture_graph
