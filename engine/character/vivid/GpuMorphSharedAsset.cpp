#include "GpuMorphSharedAsset.h"
#include "GpuMorphMeshShaders.h"
#include "VividComputeShaders.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace aiverse::vivid {
namespace {
constexpr std::size_t kAssetBudget = 512ull*1024ull*1024ull;
bool addBytes(std::size_t count,std::size_t stride,std::size_t& total) noexcept {
    if(!stride || total>kAssetBudget || count>(kAssetBudget-total)/stride)
        return false;
    total+=count*stride;
    return true;
}
bool finiteVertex(const render::ModelVertex& vertex) noexcept {
    for(float value : {vertex.pos.x,vertex.pos.y,vertex.pos.z,
                       vertex.nrm.x,vertex.nrm.y,vertex.nrm.z,
                       vertex.tangent.x,vertex.tangent.y,vertex.tangent.z})
        if(!std::isfinite(value) || std::fabs(value)>100000000.0f)return false;
    return true;
}
}

std::shared_ptr<GpuMorphSharedAsset> GpuMorphSharedAsset::create(
        gfx::Device& device,const SparseMorphLibrary& library,
        std::span<const render::ModelVertex> original,std::string* error) {
    // Private constructor prevents accidental stack lifetime bypasses when
    // borrowed by multiple character instances.
    auto asset=std::shared_ptr<GpuMorphSharedAsset>(new GpuMorphSharedAsset());
    if(!asset->initialize(device,library,original)) {
        if(error)*error=asset->lastError();
        return {};
    }
    if(error)error->clear();
    return asset;
}

bool GpuMorphSharedAsset::initialize(gfx::Device& device,
                                    const SparseMorphLibrary& library,
                                    std::span<const render::ModelVertex> vertices) {
    error_.clear();
    if(!device.supportsCompute() || !device.gpuHealthy()) {
        error_="VIVID shared morph: compute unavailable/unhealthy";
        return false;
    }
    if(vertices.empty() || vertices.size()!=library.vertices() ||
       vertices.size()>std::numeric_limits<std::uint32_t>::max() ||
       library.targets()>std::numeric_limits<std::uint32_t>::max() ||
       library.deltas()>std::numeric_limits<std::uint32_t>::max() ||
       library.gpuVertexLinks().size()>std::numeric_limits<std::uint32_t>::max()) {
        error_="VIVID shared morph: vertex/shape/CSR size mismatch";
        return false;
    }
    for(const auto& vertex:vertices)if(!finiteVertex(vertex)) {
        error_="VIVID shared morph: invalid original position/normal/tangent";
        return false;
    }
    std::size_t total=0;
    if(!addBytes(vertices.size(),sizeof(GpuMorphVertex),total) ||
       !addBytes(vertices.size(),sizeof(render::ModelVertex),total) ||
       !addBytes(library.gpuDeltas().size(),sizeof(GpuMorphDelta),total) ||
       !addBytes(library.gpuVertexOffsets().size(),sizeof(std::uint32_t),total) ||
       !addBytes(library.gpuVertexLinks().size(),sizeof(GpuMorphLink),total)) {
        error_="VIVID shared morph: exceeds 512 MiB shared asset budget";
        return false;
    }

    device_=&device;
    morphKernel_=device.createCompute({kVividMorphMSL,kVividMorphGLSL,
                                        "VIVID shared morph CSR gather"});
    if(!morphKernel_.valid()) {
        error_="VIVID shared morph: morph pipeline: "+device.lastError();
        release();return false;
    }
    packKernel_=device.createCompute({kVividMeshStreamMSL,kVividMeshStreamGLSL,
                                       "VIVID shared 60B mesh packing"});
    if(!packKernel_.valid()) {
        error_="VIVID shared morph: mesh pipeline: "+device.lastError();
        release();return false;
    }

    std::vector<GpuMorphVertex> packed(vertices.size());
    for(std::size_t i=0;i<vertices.size();++i) {
        MorphVertex cpu{};
        cpu.position={vertices[i].pos.x,vertices[i].pos.y,vertices[i].pos.z};
        cpu.normal={vertices[i].nrm.x,vertices[i].nrm.y,vertices[i].nrm.z};
        cpu.tangent={vertices[i].tangent.x,vertices[i].tangent.y,
                     vertices[i].tangent.z};
        packed[i]=packMorphVertex(cpu);
    }

    auto make=[&](std::size_t slot,const void* source,
                  std::size_t count,std::size_t stride,const char* label) {
        gfx::BufferDesc desc{};
        desc.type=gfx::BufferType::Storage;
        desc.usage=gfx::Usage::Immutable;
        desc.size=count?count*stride:16;
        desc.data=count?source:nullptr;
        desc.label=label;
        data_[slot]=device.createBuffer(desc);
        return data_[slot].valid();
    };
    if(!make(0,packed.data(),packed.size(),sizeof(GpuMorphVertex),
             "VIVID shared bind pose 48B") ||
       !make(1,library.gpuDeltas().data(),library.gpuDeltas().size(),
             sizeof(GpuMorphDelta),"VIVID shared morph deltas") ||
       !make(2,library.gpuVertexOffsets().data(),
             library.gpuVertexOffsets().size(),sizeof(std::uint32_t),
             "VIVID shared CSR offsets") ||
       !make(3,library.gpuVertexLinks().data(),
             library.gpuVertexLinks().size(),sizeof(GpuMorphLink),
             "VIVID shared CSR links") ||
       !make(4,vertices.data(),vertices.size(),
             sizeof(render::ModelVertex),"VIVID shared 60B original")) {
        error_="VIVID shared morph: GPU upload failed: "+device.lastError();
        release();return false;
    }

    vertexCount_=vertices.size();
    targetCount_=library.targets();
    gpuBytes_=total;
    params_.vertices=static_cast<std::uint32_t>(vertexCount_);
    params_.targets=static_cast<std::uint32_t>(targetCount_);
    params_.deltas=static_cast<std::uint32_t>(library.deltas());
    params_.links=static_cast<std::uint32_t>(library.gpuVertexLinks().size());
    return true;
}

bool GpuMorphSharedAsset::ready() const noexcept {
    if(!device_ || !morphKernel_.valid() || !packKernel_.valid() ||
       !vertexCount_)return false;
    for(const auto& handle:data_)if(!handle.valid())return false;
    return true;
}
GpuMorphUniforms GpuMorphSharedAsset::morphUniforms(bool normalize) const noexcept {
    auto result=params_;
    result.normalizeDirections=normalize?1u:0u;
    return result;
}
GpuMorphMeshParams GpuMorphSharedAsset::meshUniforms() const noexcept {
    GpuMorphMeshParams p{};
    p.vertexCount=static_cast<std::uint32_t>(vertexCount_);
    return p;
}
void GpuMorphSharedAsset::release() noexcept {
    if(device_) {
        for(auto& handle:data_) {
            if(handle.valid())device_->destroyBuffer(handle);
            handle={};
        }
        if(packKernel_.valid())device_->destroyCompute(packKernel_);
        if(morphKernel_.valid())device_->destroyCompute(morphKernel_);
    }
    device_=nullptr;
    morphKernel_={};
    packKernel_={};
    vertexCount_=targetCount_=gpuBytes_=0;
    params_={};
}
} // namespace aiverse::vivid
