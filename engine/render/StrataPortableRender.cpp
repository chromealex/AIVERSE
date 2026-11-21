#include "StrataPortableRender.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace aiverse::render::strata {
namespace {
bool fail(std::string* error,const char* reason) {
    if(error)*error=reason;
    return false;
}
constexpr size_t kMaxIndexBytes=16u*1024u*1024u;
constexpr size_t kMaxSelectedPrimitives=65536;

bool safePrimitive(const Model& source,uint32_t mesh,uint32_t primitive,
                   uint32_t material){
    if(mesh>=source.meshes.size()||material>=source.materials.size()||
       primitive>=source.meshes[mesh].primitives.size())
        return false;
    const auto& m=source.meshes[mesh];
    const auto& p=m.primitives[primitive];
    const auto& mat=source.materials[material];
    // Root meshes may have normal authored/generated alternative LODs:
    // portable Strata replaces their runtime selection without changing
    // the still-available original source LOD chain for Legacy fallback.
    return !m.deformable &&
           p.material==int(material) && !p.morphTargets &&
           !p.jointPalette.size() && p.variantMaterial.empty() &&
           p.indexCount>0 && p.indexCount%3==0 &&
           mat.alpha==AlphaMode::Opaque && !mat.transparent &&
           !mat.shaderGraph;
}
bool safeSource(const Model& source){
    // Existing legacy GeometryData is optional source-side acceleration
    // metadata. The portable alias deliberately does NOT borrow that data,
    // so its cluster LOD cannot be applied twice. Many GeometryScene sources
    // have prepareGeometry() enabled before portable Strata staging.
    if(!source.vb.valid()||!source.ib.valid()||
       !source.skins.empty()||!source.clips.empty()||
       !source.variants.empty()||source.morphTex.valid()||
       source.nodes.empty()||source.meshes.empty()||
       source.materials.empty()||source.bindWorld.size()!=source.nodes.size()||
       source.gpuBaseVertex>uint32_t(std::numeric_limits<int32_t>::max()))
        return false;
    for(const auto& node:source.nodes)
        if(node.skin>=0 || node.runtimeMutable) return false;
    return true;
}
} // namespace

bool PortableSource::validFor(const Model& source) const {
    return source_==&source && device_ && device_->gpuHealthy() &&
           asset_ && indexBuffer_.valid() &&
           (!streaming_ ||
            (streaming_->valid() && streaming_->buffer()==indexBuffer_)) &&
           sourceGeometryRevision_==source.geometryRevision &&
           sourceMaterialRevision_==source.materialRevision &&
           source.vb.valid() && source.ib.valid() &&
           source.vb==sourceVertexBuffer_ && source.ib==sourceIndexBuffer_ &&
           source.gpuBaseVertex==baseVertex_ &&
           source.gpuFirstIndex==sourceFirstIndex_ &&
           (streaming_ ||
            (pooledIndices_.valid()
                ? auxiliaryGeometryIndicesValidFor(pooledIndices_,source)
                : dedicated_));
}

bool PortableSource::stage(gfx::Device& dev,const Model& source,
                           const ModelAsset& asset,std::string* error,
                           size_t indexBudgetBytes,bool preferIncremental,
                           size_t uploadBudgetBytes,
                           std::shared_ptr<DiskIndexPages> diskPages){
    if(error)error->clear();
    if(device_ || source_ || indexBuffer_.valid() || pooledIndices_.valid())
        return fail(error,"portable Strata source already staged; shutdown first");
    if((dev.info().backend!=gfx::Backend::Metal &&
        dev.info().backend!=gfx::Backend::Vulkan &&
        dev.info().backend!=gfx::Backend::GLES3 &&
        dev.info().backend!=gfx::Backend::GL33) ||
       !dev.gpuHealthy() || !safeSource(source))
        return fail(error,"backend or source is not compatible with portable rigid Strata");
    if(asset.primitives.empty() || !asset.skipped.empty())
        return fail(error,"partial Strata staged model must use legacy renderer");

    // Whole-source opt-in only: no silent loss of primitives or materials.
    // RHI source CPU streams may have been cleared by uploadModel().
    std::set<std::pair<uint32_t,uint32_t>> covered;
    uint64_t sourceVertexCeiling=0, stagedIndices=0;
    for(size_t mesh=0;mesh<source.meshes.size();++mesh){
        const auto& m=source.meshes[mesh];
        for(const auto& p:m.primitives){
            const uint64_t end=uint64_t(p.firstVertex)+p.vertexCount;
            sourceVertexCeiling=std::max(sourceVertexCeiling,end);
        }
    }
    if(!sourceVertexCeiling || sourceVertexCeiling>uint32_t(-1))
        return fail(error,"source vertex bounds unavailable after upload");

    std::vector<uint32_t> combined;
    std::vector<uint32_t> offsets;
    offsets.reserve(asset.primitives.size());
    for(const auto& entry:asset.primitives){
        const auto key=std::pair{entry.mesh,entry.primitive};
        if(!covered.insert(key).second ||
           !safePrimitive(source,entry.mesh,entry.primitive,entry.material) ||
           !validate(entry.geometry.nodes,entry.geometry.roots) ||
           entry.geometry.bounds.size()!=entry.geometry.nodes.size())
            return fail(error,"invalid or duplicate staged rigid material primitive");
        if(entry.geometry.indices.empty() ||
           stagedIndices+entry.geometry.indices.size()>kMaxIndexBytes/sizeof(uint32_t))
            return fail(error,"portable Strata source index budget exceeded");
        for(uint32_t idx:entry.geometry.indices)
            if(idx>=sourceVertexCeiling)
                return fail(error,"Strata index exceeds original vertex allocation");
        // Whole-primitive coverage is essential: an importer/streamer that
        // accidentally drops one leaf must never silently erase source
        // triangles. The CPU vertex/index data can be gone after upload,
        // but the immutable original Primitive::indexCount remains known.
        const auto& g=entry.geometry;
        uint64_t leafIndices=0;
        for(size_t nodeId=0;nodeId<g.nodes.size();++nodeId){
            const auto& n=g.nodes[nodeId];
            const auto& b=g.bounds[nodeId];
            if(!n.resident || n.materialGroup!=entry.material ||
               n.firstIndex>g.indices.size() ||
               n.indexCount>g.indices.size()-n.firstIndex)
                return fail(error,"Strata primitive includes nonresident or foreign material clusters");
            for(unsigned axis=0;axis<3;++axis)
                if(!std::isfinite(b.min[axis]) ||
                   !std::isfinite(b.max[axis]) ||
                   b.min[axis]>b.max[axis])
                    return fail(error,"nonfinite or inverted Strata cluster bounds");
            if(!n.childCount){
                leafIndices+=n.indexCount;
                continue;
            }
            for(uint32_t k=0;k<n.childCount;++k){
                const size_t childId=size_t(n.firstChild)+k;
                const auto& child=g.nodes[childId];
                const auto& cb=g.bounds[childId];
                if(n.geometricError<child.geometricError)
                    return fail(error,"nonconservative Strata parent LOD error");
                for(unsigned axis=0;axis<3;++axis)
                    if(b.min[axis]>cb.min[axis] ||
                       b.max[axis]<cb.max[axis])
                        return fail(error,"Strata parent bounds omit child geometry");
            }
        }
        if(leafIndices!=source.meshes[entry.mesh].primitives[entry.primitive].indexCount)
            return fail(error,"Strata leaf frontier does not cover full source primitive");
        offsets.push_back(uint32_t(stagedIndices));
        stagedIndices+=entry.geometry.indices.size();
        combined.insert(combined.end(),entry.geometry.indices.begin(),
                        entry.geometry.indices.end());
    }
    // A regular prepared Model may contain generated alternate LOD meshes
    // appended by upload preparation. They are not independent source
    // objects. Require complete coverage of only meshes actually bound
    // by original Model nodes, leaving those alternate LODs available to
    // the untouched Legacy renderer on unsupported/fallback draw.
    for(const auto& node:source.nodes){
        if(node.mesh<0)continue;
        if(size_t(node.mesh)>=source.meshes.size())
            return fail(error,"source node references invalid mesh");
        for(uint32_t primitive=0;
            primitive<source.meshes[size_t(node.mesh)].primitives.size();
            ++primitive)
            if(!covered.count({uint32_t(node.mesh),primitive}))
                return fail(error,"Strata stage misses a live source primitive");
    }
    if(combined.empty())return fail(error,"no portable Strata indices");

    GeometryPoolIndexSpan pooled;
    gfx::BufferHandle buffer;
    bool dedicated=false;
    uint32_t base=0;
    // Prefer the original source's pool, even on Vulkan without compute.
    // GLES3 normally has dedicated vertex/index buffers; allocate ONLY an
    // immutable auxiliary index stream, NEVER duplicate its vertex data.
    std::unique_ptr<IndexResidency> streaming;
    if(indexBudgetBytes){
        streaming=std::make_unique<IndexResidency>();
        if(!streaming->initialize(dev,asset,indexBudgetBytes,error,
                                  preferIncremental,uploadBudgetBytes,
                                  std::move(diskPages)))
            return false;
        buffer=streaming->buffer();
        // The residency manager owns this dedicated index-only buffer.
        // Never hand it to the pooled allocator or destroy it twice.
    }else if(source.geometryPoolPage &&
       uploadAuxiliaryGeometryIndices(dev,source,combined.data(),
                                      combined.size(),pooled)){
        buffer=pooled.buffer;
        base=pooled.firstIndex;
    }else{
        gfx::BufferDesc desc{};
        desc.type=gfx::BufferType::Index;
        desc.usage=gfx::Usage::Immutable;
        desc.size=combined.size()*sizeof(uint32_t);
        desc.data=combined.data();
        desc.label="Strata portable GLES3/Vulkan indexed LOD sidecar";
        buffer=dev.createBuffer(desc);
        if(!buffer.valid())
            return fail(error,"portable Strata index-only GPU upload failed");
        dedicated=true;
    }

    device_=&dev;
    source_=&source;
    asset_=&asset;
    sourceVertexBuffer_=source.vb;
    sourceIndexBuffer_=source.ib;
    sourceFirstIndex_=source.gpuFirstIndex;
    indexBuffer_=buffer;
    firstIndex_=base;
    baseVertex_=source.gpuBaseVertex;
    indexCount_=uint32_t(stagedIndices);
    sourceGeometryRevision_=source.geometryRevision;
    sourceMaterialRevision_=source.materialRevision;
    primitiveOffsets_=std::move(offsets);
    pooledIndices_=std::move(pooled);
    dedicated_=dedicated;
    streaming_=std::move(streaming);
    stats_={};
    stats_.pooledIndices=pooledIndices_.valid();
    stats_.dedicatedIndices=dedicated || bool(streaming_);
    stats_.streamingIndices=bool(streaming_);
    stats_.residentIndexBytes=streaming_ ? streaming_->stats().activeBytes
                                       : uint64_t(indexCount_)*sizeof(uint32_t);
    return true;
}

bool PortableSource::collectResidencyDemands(
        const mat4& instanceWorld,
        const std::vector<PortablePassView>& passes,
        bool forceFinestForUnspecifiedPass,
        std::vector<IndexDemand>& output,
        const PortableLodHistory* temporalHistory,
        float hysteresisFraction) const {
    if (!streaming_ || !source_ || !validFor(*source_) ||
        (!forceFinestForUnspecifiedPass && passes.empty()) ||
        passes.size()>8 || !std::isfinite(hysteresisFraction) ||
        hysteresisFraction<0.f || hysteresisFraction>.45f ||
        (hysteresisFraction>0.f && !temporalHistory))return false;
    for(float v:instanceWorld.m)if(!std::isfinite(v))return false;
    std::vector<IndexDemand> demands;
    for (size_t i=0;i<asset_->primitives.size();++i) {
        const auto& entry=asset_->primitives[i];
        const auto& g=entry.geometry;
        if(forceFinestForUnspecifiedPass){
            for(uint32_t n=0;n<g.nodes.size();++n)
                if(!g.nodes[n].childCount)
                    demands.push_back({uint32_t(i),n,3.f});
            continue;
        }
        bool used=false;
        for(size_t node=0;node<source_->nodes.size();++node) {
            if(source_->nodes[node].mesh!=int(entry.mesh))continue;
            used=true;
            for(size_t passSlot=0;passSlot<passes.size();++passSlot) {
                const auto& pass=passes[passSlot];
                ViewConfig view{};
                view.objectToClip=pass.viewProjection*instanceWorld*
                                  source_->bindWorld[node];
                view.width=pass.width;
                view.height=pass.height;
                view.clipDepthZeroToOne=pass.clipDepthZeroToOne;
                view.errorThresholdPixels=pass.errorThresholdPixels;
                if(hysteresisFraction>0.f){
                    view.hysteresisFraction=hysteresisFraction;
                    view.previousRefined=temporalHistory->previous(
                        asset_,sourceGeometryRevision_,uint32_t(passes.size()),
                        PortableLodHistory::Key{uint32_t(i),uint32_t(node),
                                                 uint32_t(passSlot)},
                        g.nodes.size());
                }
                view.frustumCull=false; // shadow/reflection keep coverage
                view.maxSelectedNodes=uint32_t(kMaxSelectedPrimitives);
                ViewPlan plan;
                if(!planView(g,view,plan,nullptr) || plan.budgetExceeded)
                    return false;
                for(const auto& range:plan.ranges)
                    demands.push_back({uint32_t(i),range.node,2.f});
            }
        }
        if(!used)return false;
    }
    output.insert(output.end(),demands.begin(),demands.end());
    return true;
}

bool PortableSource::updateResidency(
        const std::vector<IndexDemand>& demands,std::string* error,
        size_t uploadAllowanceBytes) {
    if(!streaming_ || !source_ || !validFor(*source_))
        return fail(error,"portable Strata index streaming is unavailable");
    // All instances of this source are preflighted before ANY drawModel()
    // submission in this epoch. Never repack a buffer mid-Renderer3D render.
    if(!streaming_->update(demands,error,uploadAllowanceBytes))
        return false;
    indexBuffer_=streaming_->buffer();
    stats_.residentIndexBytes=streaming_->stats().activeBytes;
    return true;
}

bool PortableSource::prepareModel(
        const mat4& instanceWorld,
        const std::vector<PortablePassView>& passes,
        bool forceFinestForUnspecifiedPass,
        Model& output,std::string* error,
        PortableLodHistory* temporalHistory,float hysteresisFraction){
    if(error)error->clear();
    const auto reject=[&](const char* reason){
        ++stats_.fallbackPreparations;
        return fail(error,reason);
    };
    if(!source_ || !validFor(*source_) || !safeSource(*source_) ||
       (!forceFinestForUnspecifiedPass && passes.empty()) ||
       passes.size()>8 ||
       !std::isfinite(hysteresisFraction) || hysteresisFraction<0.f ||
       hysteresisFraction>.45f ||
       (hysteresisFraction>0.f && !temporalHistory))
        return reject("stale Strata source or unsupported pass configuration");
    for(const auto& p:passes)
        if(p.width<=0 || p.height<=0 ||
           p.width>32768 || p.height>32768 ||
           !std::isfinite(p.errorThresholdPixels) ||
           p.errorThresholdPixels<=0)
            return reject("invalid portable Strata pass");
    for(const float x:instanceWorld.m)
        if(!std::isfinite(x))return reject("invalid instance transform");

    // Source meshes may be referenced by multiple nodes. The alias carries
    // a single primitive layout; refine to the deepest frontier requested
    // by ANY node in ANY pass. Renderer3D then owns main/shadow/reflection
    // culling independently, preventing one pass from losing geometry.
    Model alias;
    alias.path=source_->path+"#strata-portable";
    alias.batching=source_->batching;
    alias.batching.autoLod=false;
    alias.geometryRevision=source_->geometryRevision;
    alias.materialRevision=source_->materialRevision;
    alias.nodes=source_->nodes;
    alias.order=source_->order;
    alias.bindWorld=source_->bindWorld;
    alias.meshes=source_->meshes;
    alias.materials=source_->materials;
    alias.images=source_->images;
    for(auto& img:alias.images)img.borrowedTexture=true;
    alias.boundsMin=source_->boundsMin;
    alias.boundsMax=source_->boundsMax;
    alias.vb=source_->vb;
    alias.ib=indexBuffer_;
    alias.gpuFirstIndex=firstIndex_;
    alias.gpuBaseVertex=baseVertex_;
    // A dedicated GLES3/Vulkan fallback IB is NOT a shared pool allocation,
    // even if the source VB happens to be pooled. Only an actual pooled
    // auxiliary span may inherit the page for Vulkan canonical draw indexing.
    alias.geometryPoolPage=pooledIndices_.valid()
        ? source_->geometryPoolPage : nullptr;
    for(auto& m:alias.meshes){
        m.primitives.clear();
        m.lods.clear();
        m.shadowBatch={};
        m.materialBatches.clear();
    }
    // Staged writeback only: output and hysteresis history must remain
    // unchanged if even the LAST material/primitive cannot render.
    using HistoryKey=PortableLodHistory::Key;
    std::vector<std::pair<HistoryKey,std::vector<uint8_t>>> historyUpdates;
    uint64_t selected=0;
    std::vector<SelectedCluster> debugPicks; // published only on success
    for(size_t i=0;i<asset_->primitives.size();++i){
        const auto& entry=asset_->primitives[i];
        if(!safePrimitive(*source_,entry.mesh,entry.primitive,entry.material))
            return reject("live material or source became incompatible");
        const auto& g=entry.geometry;
        std::vector<uint32_t> parent(g.nodes.size(),invalidNode);
        for(uint32_t n=0;n<g.nodes.size();++n)
            for(uint32_t ch=0;ch<g.nodes[n].childCount;++ch){
                const uint32_t id=g.nodes[n].firstChild+ch;
                if(id>=parent.size() || parent[id]!=invalidNode)
                    return reject("malformed cluster parent topology");
                parent[id]=n;
            }
        std::vector<uint8_t> refine(g.nodes.size(),0);
        std::vector<uint8_t> gpuMask;
        if(streaming_){
            gpuMask.resize(g.nodes.size());
            for(uint32_t n=0;n<g.nodes.size();++n)
                gpuMask[n]=streaming_->resident(uint32_t(i),n) ? 1 : 0;
        }
        if(forceFinestForUnspecifiedPass){
            // Unknown future reflection/capture views use the original
            // finest resident meshlets. Never cull by the main camera alone.
            for(uint32_t n=0;n<g.nodes.size();++n){
                const auto& candidate=g.nodes[n];
                bool allChildrenResident=candidate.childCount>0;
                for(uint32_t k=0;k<candidate.childCount;++k)
                    if(streaming_ && !gpuMask[candidate.firstChild+k])
                        allChildrenResident=false;
                if(allChildrenResident)refine[n]=1;
            }
        }else{
            bool wasUsed=false;
            for(size_t n=0;n<source_->nodes.size();++n){
                if(source_->nodes[n].mesh!=int(entry.mesh))continue;
                wasUsed=true;
                for(size_t passSlot=0;passSlot<passes.size();++passSlot){
                    const auto& pass=passes[passSlot];
                    ViewConfig view{};
                    view.objectToClip=pass.viewProjection*instanceWorld*
                                      source_->bindWorld[n];
                    view.width=pass.width;
                    view.height=pass.height;
                    view.clipDepthZeroToOne=pass.clipDepthZeroToOne;
                    view.errorThresholdPixels=pass.errorThresholdPixels;
                    if(hysteresisFraction>0.f){
                        view.hysteresisFraction=hysteresisFraction;
                        view.previousRefined=temporalHistory->previous(
                            asset_,sourceGeometryRevision_,
                            uint32_t(passes.size()),
                            HistoryKey{uint32_t(i),uint32_t(n),
                                       uint32_t(passSlot)},
                            g.nodes.size());
                    }
                    // The existing renderer independently culls the per-pass
                    // model primitives. Never select only main-camera geometry.
                    view.frustumCull=false;
                    view.maxSelectedNodes=uint32_t(kMaxSelectedPrimitives);
                    ViewPlan plan;
                    if(!planView(g,view,plan,nullptr,
                                 streaming_ ? &gpuMask : nullptr) ||
                       plan.budgetExceeded)
                        return reject("portable Strata CPU LOD plan failed");
                    if(hysteresisFraction>0.f)
                        historyUpdates.emplace_back(
                            HistoryKey{uint32_t(i),uint32_t(n),
                                       uint32_t(passSlot)},
                            std::move(plan.refinedNodes));
                    for(const auto& range:plan.ranges){
                        uint32_t ancestor=parent[range.node];
                        while(ancestor!=invalidNode){
                            refine[ancestor]=1;
                            ancestor=parent[ancestor];
                        }
                    }
                }
            }
            if(!wasUsed)return reject("staged material mesh is unused by model nodes");
        }
        std::vector<uint32_t> pending(g.roots.rbegin(),g.roots.rend());
        while(!pending.empty()){
            const uint32_t n=pending.back();pending.pop_back();
            const auto& node=g.nodes[n];
            if(refine[n] && node.childCount){
                bool childrenResident=true;
                for(uint32_t child=0;child<node.childCount;++child)
                    if(!g.nodes[node.firstChild+child].resident)
                        childrenResident=false;
                if(!childrenResident)
                    return reject("no resident children for requested Strata LOD");
                for(uint32_t child=node.childCount;child>0;--child)
                    pending.push_back(node.firstChild+child-1);
                continue;
            }
            if(!node.resident || node.materialGroup!=entry.material ||
               selected>=kMaxSelectedPrimitives ||
               uint64_t(primitiveOffsets_[i])+node.firstIndex+node.indexCount>
                   uint64_t(indexCount_))
                return reject("invalid portable Strata selected index frontier");
            Primitive prim=source_->meshes[entry.mesh].primitives[entry.primitive];
            if(streaming_){
                const auto gpu=streaming_->span(uint32_t(i),n);
                if(!gpu.valid() || gpu.indexCount!=node.indexCount)
                    return reject("selected Strata node has no GPU-resident index range");
                prim.firstIndex=gpu.firstIndex;
            }else{
                prim.firstIndex=primitiveOffsets_[i]+node.firstIndex;
            }
            prim.indexCount=node.indexCount;
            prim.shadowBatched=false;
            const auto& bounds=g.bounds[n];
            prim.boundsMin={bounds.min[0],bounds.min[1],bounds.min[2]};
            prim.boundsMax={bounds.max[0],bounds.max[1],bounds.max[2]};
            alias.meshes[entry.mesh].primitives.push_back(std::move(prim));
            if(debugCapture_)debugPicks.push_back({uint32_t(i),n});
            ++selected;
        }
    }
    if(!selected)return reject("no resident selected Strata clusters");
    // Publication boundary: all primitive ranges are drawable, so it is
    // finally safe to swap temporal states. A change in source/pass layout
    // discards every older per-camera record instead of reusing stale LODs.
    if(temporalHistory){
        if(forceFinestForUnspecifiedPass || hysteresisFraction==0.f){
            temporalHistory->reset();
        }else{
            if(temporalHistory->source!=asset_ ||
               temporalHistory->geometryRevision!=sourceGeometryRevision_ ||
               temporalHistory->passCount!=passes.size()){
                temporalHistory->reset();
                temporalHistory->source=asset_;
                temporalHistory->geometryRevision=sourceGeometryRevision_;
                temporalHistory->passCount=uint32_t(passes.size());
            }
            for(auto& [key,refined]:historyUpdates){
                auto& previous=temporalHistory->states[key];
                if(previous.size()==refined.size())
                    for(size_t id=0;id<refined.size();++id)
                        stats_.hysteresisTransitions+=previous[id]!=refined[id];
                previous=std::move(refined);
            }
        }
    }
    output=std::move(alias); // transaction: original output untouched on error
    if(debugCapture_)debugSelection_=std::move(debugPicks);
    ++stats_.drawPreparations;
    stats_.selectedClusters+=selected;
    stats_.forcedFinest=forceFinestForUnspecifiedPass;
    return true;
}

void PortableSource::shutdown(gfx::Device& dev){
    // A buffer handle is scoped to its creating RHI device. Never destroy
    // that numeric handle on another device (which may own unrelated data).
    if(device_ && device_!=&dev)return;
    if(streaming_){
        streaming_->shutdown(dev);
        streaming_.reset();
    }
    if(pooledIndices_.valid())
        releaseAuxiliaryGeometryIndices(dev,pooledIndices_);
    if(dedicated_&&indexBuffer_.valid())
        dev.destroyBuffer(indexBuffer_);
    device_=nullptr;source_=nullptr;asset_=nullptr;indexBuffer_={};
    sourceVertexBuffer_={};sourceIndexBuffer_={};sourceFirstIndex_=0;
    pooledIndices_={};dedicated_=false;
    firstIndex_=indexCount_=baseVertex_=0;
    sourceGeometryRevision_=sourceMaterialRevision_=0;
    primitiveOffsets_.clear();
    stats_={};
    std::vector<SelectedCluster>().swap(debugSelection_);
}
} // namespace aiverse::render::strata
