#include "GpuActorStream.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace aiverse::vivid {

bool GpuActorStream::initialize(gfx::Device& device,
                                const render::Model& model,
                                const SparseMorphLibrary& library,
                                std::span<const render::ModelVertex> vertices) {
    if(device_ || shared_ || weightsBuffer_.valid() || packedOutput_.valid()) {
        error_="VIVID actor: previous resources must be safely retired first";
        return false;
    }
    std::string message;
    auto asset=GpuMorphSharedAsset::create(device,library,vertices,&message);
    if(!asset) {
        error_="VIVID actor: shared GPU asset initialization failed: "+message;
        return false;
    }
    return initializeShared(device,model,std::move(asset));
}

bool GpuActorStream::initializeShared(gfx::Device& device,
                                     const render::Model& model,
                                     std::shared_ptr<GpuMorphSharedAsset> asset) {
    if(device_ || shared_ || weightsBuffer_.valid() || packedOutput_.valid()) {
        error_="VIVID actor: previous resources must be safely retired first";
        return false;
    }
    error_.clear();
    if(!asset || !asset->ready() || asset->device()!=&device ||
       !device.supportsCompute() || !device.gpuHealthy()) {
        // Buffers can be prepared before the first frame. A nonzero serial
        // is required by dispatch(), not by resource initialization.
        error_="VIVID actor: shared GPU asset missing or compute unavailable";
        return false;
    }
    if(asset->vertexCount()==0 ||
       asset->vertexCount()>std::numeric_limits<std::uint32_t>::max() ||
       render::modelVertexOverrideStatus(model,model.vb,
            static_cast<std::uint32_t>(asset->vertexCount()))!=
            render::ModelVertexOverrideStatus::Ready) {
        error_="VIVID actor: shared asset/model vertex range incompatible or pooled";
        return false;
    }
    const std::size_t kLimit=512ull*1024ull*1024ull;
    const std::size_t vertices=asset->vertexCount(),targets=asset->targetCount();
    if(targets>std::numeric_limits<std::uint32_t>::max() ||
       vertices > kLimit/(sizeof(GpuMorphVertex)+sizeof(render::ModelVertex)) ||
       targets > (kLimit-vertices*(sizeof(GpuMorphVertex)+
                                   sizeof(render::ModelVertex)))/sizeof(float)) {
        error_="VIVID actor: GPU per-instance buffers exceed 512 MiB budget";
        return false;
    }

    std::vector<float> zeroWeights(std::max<std::size_t>(targets,1),0.0f);
    // Allocate instance-owned CPU weights before allocating GPU resources;
    // after this point publication uses only noexcept moves/assignments.
    std::vector<float> actorWeights(targets,0.0f);
    gfx::BufferDesc desc{};
    desc.type=gfx::BufferType::Storage;
    desc.usage=gfx::Usage::Dynamic;
    desc.size=zeroWeights.size()*sizeof(float);
    desc.data=zeroWeights.data();
    desc.label="VIVID instance shape weights";
    weightsBuffer_=device.createBuffer(desc);
    if(!weightsBuffer_.valid()) {
        error_="VIVID actor: per-instance weights allocation failed: "+device.lastError();
        return false;
    }

    desc.usage=gfx::Usage::Immutable;
    desc.data=nullptr;
    desc.size=vertices*sizeof(GpuMorphVertex);
    desc.label="VIVID instance 48B deformed mesh";
    morphOutput_=device.createBuffer(desc);
    if(!morphOutput_.valid()) {
        error_="VIVID actor: per-instance morph output allocation failed: "+device.lastError();
        device.destroyBuffer(weightsBuffer_);
        weightsBuffer_={};
        return false;
    }
    desc.size=vertices*sizeof(render::ModelVertex);
    desc.label="VIVID instance 60B render stream";
    packedOutput_=device.createBuffer(desc);
    if(!packedOutput_.valid()) {
        error_="VIVID actor: per-instance mesh output allocation failed: "+device.lastError();
        device.destroyBuffer(weightsBuffer_);
        device.destroyBuffer(morphOutput_);
        weightsBuffer_=morphOutput_={};
        return false;
    }
    device_=&device;
    model_=&model;
    shared_=std::move(asset);
    weights_=std::move(actorWeights);
    lastAttemptedFrame_=lastCompletedFrame_=0;
    return true;
}

bool GpuActorStream::dispatch(const FaceFrame& face,
                              const FaceShapeBinding& mapping,
                              float influence, bool normalizeDirections) {
    if(!ready() || !model_ || !device_->gpuHealthy()) {
        error_="VIVID actor: not initialized or graphics device unhealthy";
        return false;
    }
    const auto frame=device_->frameSerial();
    if(!frame || frame==lastAttemptedFrame_) {
        error_="VIVID actor: only one submission per actor per active frame";
        return false;
    }
    if(mapping.targets()!=weights_.size() ||
       !mapping.evaluate(face,weights_,influence)) {
        error_="VIVID actor: invalid semantic morph mapping";
        return false;
    }
    if(!device_->canUpdateSharedBuffers()) {
        error_="VIVID actor: compute cannot safely update weights during a render pass";
        return false;
    }
    lastAttemptedFrame_=frame;
    if(!weights_.empty())
        device_->updateBuffer(weightsBuffer_,weights_.data(),
                              weights_.size()*sizeof(float));
    const auto morphParams=shared_->morphUniforms(normalizeDirections);
    if(!device_->beginCompute(shared_->morphCompute())) {
        error_="VIVID actor: begin morph compute outside frame/inside pass";
        return false;
    }
    for(int slot=0;slot<4;++slot)
        device_->setComputeBuffer(slot,shared_->morphInput(std::size_t(slot)));
    device_->setComputeBuffer(4,weightsBuffer_);
    device_->setComputeBuffer(5,morphOutput_);
    device_->setComputeUniforms(&morphParams,sizeof(morphParams));
    device_->dispatch(static_cast<std::uint32_t>(vertexCount()));
    device_->endCompute();

    const auto meshParams=shared_->meshUniforms();
    if(!device_->beginCompute(shared_->meshCompute())) {
        error_="VIVID actor: begin render stream packing compute failed";
        return false;
    }
    device_->setComputeBuffer(0,shared_->originalModelVertices());
    device_->setComputeBuffer(1,morphOutput_);
    device_->setComputeBuffer(2,packedOutput_);
    device_->setComputeUniforms(&meshParams,sizeof(meshParams));
    device_->dispatch(static_cast<std::uint32_t>(vertexCount()));
    device_->endCompute();
    lastCompletedFrame_=frame;
    error_.clear();
    return true;
}

bool GpuActorStream::attach(render::ModelDraw& draw) const noexcept {
    if(!ready() || draw.model!=model_ || draw.vertexOverride.valid() ||
       draw.vertexOverrideCount || !lastCompletedFrame_ ||
       device_->frameSerial()!=lastCompletedFrame_ ||
       render::modelVertexOverrideStatus(*model_,packedOutput_,
            static_cast<std::uint32_t>(vertexCount()))!=
            render::ModelVertexOverrideStatus::Ready)
        return false;
    draw.vertexOverride=packedOutput_;
    draw.vertexOverrideCount=static_cast<std::uint32_t>(vertexCount());
    return true;
}

bool GpuActorStream::download(std::span<render::ModelVertex> result) const {
    return ready() && result.size()==vertexCount() &&
           device_->readBuffer(packedOutput_,result.data(),result.size_bytes());
}

bool GpuActorStream::shutdownAfterIdle() {
    if(!device_)return true;
    if(!device_->waitForResourceIdle()) {
        error_="VIVID actor: GPU idle barrier failed; resources retained";
        return false;
    }
    releaseAfterGpuCompletion();
    return true;
}

void GpuActorStream::releaseAfterGpuCompletion() noexcept {
    if(device_) {
        if(weightsBuffer_.valid())device_->destroyBuffer(weightsBuffer_);
        if(morphOutput_.valid())device_->destroyBuffer(morphOutput_);
        if(packedOutput_.valid())device_->destroyBuffer(packedOutput_);
    }
    weightsBuffer_=morphOutput_=packedOutput_={};
    shared_.reset(); // Last shared reference can now safely retire immutable data.
    device_=nullptr;
    model_=nullptr;
    weights_.clear();
    lastAttemptedFrame_=lastCompletedFrame_=0;
    error_.clear();
}
} // namespace aiverse::vivid
