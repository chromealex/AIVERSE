// VIVID cross-RHI CPU fallback for GLES3/WebGL2 and non-compute devices.
// Keeps the same full ModelVertex override contract as GPU Actor Stream.
#pragma once
#include "FaceShapeBinding.h"
#include "SparseMorph.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/ModelVertexStreamOverride.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aiverse::vivid {
class CpuActorStream {
public:
    bool initialize(gfx::Device& device,const render::Model& model,
                    const SparseMorphLibrary& morphs,
                    std::span<const render::ModelVertex> base) {
        if(device_ || base.empty() || base.size()!=morphs.vertices() ||
           base.size()>std::numeric_limits<std::uint32_t>::max() ||
           morphs.bufferBytes()>512ull*1024ull*1024ull ||
           base.size()>(512ull*1024ull*1024ull-morphs.bufferBytes())/
               (sizeof(render::ModelVertex)*2+sizeof(MorphVertex)*2) ||
           render::modelVertexOverrideStatus(model,model.vb,
               std::uint32_t(base.size()))!=render::ModelVertexOverrideStatus::Ready) {
            error_="VIVID CPU actor: invalid vertex stream or already initialized";
            return false;
        }
        base_.assign(base.begin(),base.end());
        output_=base_;
        originalMorph_.resize(base.size());
        currentMorph_.resize(base.size());
        for(std::size_t i=0;i<base.size();++i) {
            const auto& v=base[i];
            originalMorph_[i].position={v.pos.x,v.pos.y,v.pos.z};
            originalMorph_[i].normal={v.nrm.x,v.nrm.y,v.nrm.z};
            originalMorph_[i].tangent={v.tangent.x,v.tangent.y,v.tangent.z};
        }
        weights_.resize(morphs.targets());
        // Authoring publishes HumanPreviewAsset by moving its value fields.
        // Retain an immutable snapshot rather than a pointer into that value.
        // Allocate before creating GPU resources so exceptions cannot leak VB.
        morphs_=std::make_unique<const SparseMorphLibrary>(morphs);
        gfx::BufferDesc desc{};
        desc.type=gfx::BufferType::Vertex;
        desc.usage=gfx::Usage::Dynamic;
        desc.snapshotUpdates=true;
        desc.size=output_.size()*sizeof(render::ModelVertex);
        desc.data=output_.data();
        desc.label="VIVID CPU cross-backend facial stream";
        buffer_=device.createBuffer(desc);
        if(!buffer_.valid()) {
            base_.clear();output_.clear();originalMorph_.clear();
            currentMorph_.clear();weights_.clear();
            morphs_.reset();
            error_="VIVID CPU actor: vertex stream allocation failed";
            return false;
        }
        model_=&model;device_=&device;
        error_.clear();
        return true;
    }
    bool dispatch(const FaceFrame& face,const FaceShapeBinding& binding,
                  float influence=1.0f) {
        if(!ready() || !device_->gpuHealthy() ||
           !device_->canUpdateSharedBuffers() ||
           !binding.evaluate(face,weights_,influence)) {
            error_="VIVID CPU actor: unavailable or invalid facial weights";
            return false;
        }
        const auto frame=device_->frameSerial();
        // Some GLES3/WebGL2 devices have no frame serial. The host calls
        // dispatch once per presentation frame; serials are checked when
        // the backend advertises them, but are not mandatory for CPU mode.
        if(frame && frame==lastFrame_) {
            error_="VIVID CPU actor: duplicate frame";
            return false;
        }
        if(!morphs_->evaluateGather(originalMorph_,weights_,currentMorph_)) {
            error_="VIVID CPU actor: CPU morph evaluation failed";
            return false;
        }
        for(std::size_t i=0;i<output_.size();++i) {
            output_[i]=base_[i];
            const auto& v=currentMorph_[i];
            output_[i].pos={v.position[0],v.position[1],v.position[2]};
            output_[i].nrm={v.normal[0],v.normal[1],v.normal[2]};
            output_[i].tangent.x=v.tangent[0];
            output_[i].tangent.y=v.tangent[1];
            output_[i].tangent.z=v.tangent[2];
        }
        device_->updateBuffer(buffer_,output_.data(),output_.size()*sizeof(render::ModelVertex));
        lastFrame_=frame;
        hasOutput_=true;
        error_.clear();
        return true;
    }
    bool attach(render::ModelDraw& draw) const noexcept {
        if(!ready() || draw.model!=model_ ||
           draw.vertexOverride.valid() || draw.vertexOverrideCount ||
           !hasOutput_ ||
           (device_->frameSerial()&&device_->frameSerial()!=lastFrame_) ||
           render::modelVertexOverrideStatus(*model_,buffer_,
               std::uint32_t(output_.size()))!=render::ModelVertexOverrideStatus::Ready)
            return false;
        draw.vertexOverride=buffer_;
        draw.vertexOverrideCount=std::uint32_t(output_.size());
        return true;
    }
    bool shutdownAfterIdle() {
        if(!device_)return true;
        if(!device_->waitForResourceIdle())return false;
        releaseAfterGpuCompletion();
        return true;
    }
    bool ready() const noexcept {return device_&&model_&&morphs_&&buffer_.valid();}
    const std::string& lastError() const noexcept{return error_;}
private:
    friend class HumanRetirementQueue;
    // Called ONLY by a completed GPU token or explicit idle barrier.
    void releaseAfterGpuCompletion() noexcept {
        if(device_&&buffer_.valid())device_->destroyBuffer(buffer_);
        buffer_={};device_=nullptr;model_=nullptr;morphs_=nullptr;
        base_.clear();output_.clear();originalMorph_.clear();
        currentMorph_.clear();weights_.clear();lastFrame_=0;
        hasOutput_=false;
    }
    gfx::Device* device_=nullptr;
    const render::Model* model_=nullptr;
    std::unique_ptr<const SparseMorphLibrary> morphs_;
    gfx::BufferHandle buffer_{};
    std::vector<render::ModelVertex> base_,output_;
    std::vector<MorphVertex> originalMorph_,currentMorph_;
    std::vector<float> weights_;
    std::uint64_t lastFrame_=0;
    bool hasOutput_=false;
    std::string error_;
};
} // namespace aiverse::vivid
