// Opt-in VIVID hardware ID pass for Metal/Vulkan/native GLES3/GL33.
// Uses the canonical gfx::Device and an ordinary offscreen RGBA8 + depth
// pass, with one true asynchronous pixel readback. Unsupported RHI paths
// fail closed and retain the existing CPU BVH result.
// This does not replace the CPU BVH picker, GPU actor pose, or ECS.
#pragma once
#include "SculptGpuPickShaders.h"
#include "SculptGpuAlphaMask.h"
#include "SculptGpuVertexSource.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace aiverse::vivid {

class SculptGpuIdPass final {
    struct Reply {
        std::vector<std::uint8_t> rgba;
        bool ready=false;
        bool depthReady=false;
        bool depthValid=false;
        float depth01=1.f;
    };
public:
    struct Completed {
        SculptGpuPickRequest request{};
        std::vector<std::uint8_t> rgba;
        bool actualOverride=false;
        bool depthValid=false; // true actual RHI Depth32Float pixel, not CPU
        float depth01=1.f; // ID triangles came from the actor's GPU 60B buffer
    };
private:
    struct Pending {
        gfx::BufferHandle vertex;
        std::vector<gfx::BufferHandle> masked;
        std::vector<gfx::BufferHandle> expandedScratch;
        bool actualOverride=false;
        bool wantsDepth=false;
        std::shared_ptr<Reply> reply;
        SculptGpuPickRequest request{};
    };
    gfx::Device* device_=nullptr; // render-owner thread, borrowed
    gfx::ShaderHandle shader_{};
    gfx::PipelineHandle pipeline_{};
    gfx::ShaderHandle maskShader_{};
    gfx::PipelineHandle maskPipeline_{};
    gfx::ComputeHandle expandCompute_{}; // native Metal real-vertex expansion
    gfx::ComputeHandle expandMaskCompute_{}; // same GPU source + UV/alpha
    gfx::TextureHandle color_,depth_;
    int width_=0,height_=0;
    std::vector<Pending> pending_;
    std::vector<SculptGpuIdVertex> vertices_;
    bool supported_=false;
public:
    SculptGpuIdPass()=default;
    SculptGpuIdPass(const SculptGpuIdPass&)=delete;
    SculptGpuIdPass& operator=(const SculptGpuIdPass&)=delete;

    bool initialize(gfx::Device& device) {
        if(device_)return false;
        // Pipeline compilation may precede beginFrame. The frame serial is
        // mandatory only when recording a click and correlating its readback.
        const auto backend=device.info().backend;
        if(backend!=gfx::Backend::Metal && backend!=gfx::Backend::Vulkan &&
           backend!=gfx::Backend::GLES3 && backend!=gfx::Backend::GL33)
            return false;
        const auto shaderDesc=sculptGpuIdShaderDesc();
        auto shader=device.createShader(shaderDesc);
        if(!shader.valid())return false;
        auto pipeline=device.createPipeline(
            sculptGpuIdPipelineDesc(shader,gfx::CullMode::None));
        if(!pipeline.valid()) {
            device.destroyShader(shader);
            return false;
        }
        const auto maskShader=device.createShader(sculptGpuMaskShaderDesc());
        if(!maskShader.valid()) {
            device.destroyPipeline(pipeline);
            device.destroyShader(shader);
            return false;
        }
        const auto maskPipeline=device.createPipeline(
            sculptGpuMaskPipelineDesc(maskShader));
        if(!maskPipeline.valid()) {
            device.destroyShader(maskShader);
            device.destroyPipeline(pipeline);
            device.destroyShader(shader);
            return false;
        }
        // Source override expansion is opt-in: if the compute kernel
        // cannot compile, the legacy CPU-fed GPU diagnostic remains usable.
        if(backend==gfx::Backend::Metal && device.supportsCompute()) {
            expandCompute_=device.createCompute({
                kSculptGpuSourceExpandMSL,nullptr,
                "VIVID ID actual 60B vertex expansion"});
            expandMaskCompute_=device.createCompute({
                kSculptGpuSourceMaskExpandMSL,nullptr,
                "VIVID ID actual GPU source alpha mask expansion"});
        }
        device_=&device;
        shader_=shader;
        pipeline_=pipeline;
        maskShader_=maskShader;
        maskPipeline_=maskPipeline;
        supported_=true;
        return true;
    }
    bool supported() const noexcept {return supported_;}
    bool supportsActualGpuVertexStream() const noexcept {
        return supported_ && expandCompute_.valid();
    }
    bool supportsActualGpuMaskedStream() const noexcept {
        return supported_ && expandMaskCompute_.valid() && maskPipeline_.valid();
    }
    bool pending() const noexcept {return !pending_.empty();}
    bool hasTargets() const noexcept {return color_.valid()&&depth_.valid();}

    // Pixel callback marks Reply only: never captures CreatorApp or accesses
    // the Device off-thread. A later owner frame polls and retires buffers.
    bool takeCompleted(Completed& out) {
        if(!device_ || pending_.empty() ||
           !pending_.front().reply->ready ||
           (pending_.front().wantsDepth &&
            !pending_.front().reply->depthReady))return false;
        auto p=std::move(pending_.front());
        pending_.erase(pending_.begin());
        // The completed callback proves the corresponding command buffer
        // finished, so its immutable vertex allocation can be released.
        if(p.vertex.valid())device_->destroyBuffer(p.vertex);
        for(auto handle:p.masked)device_->destroyBuffer(handle);
        for(auto handle:p.expandedScratch)device_->destroyBuffer(handle);
        out.request=p.request;
        out.actualOverride=p.actualOverride;
        out.rgba=std::move(p.reply->rgba);
        out.depthValid=p.wantsDepth && p.reply->depthValid;
        out.depth01=out.depthValid?p.reply->depth01:1.f;
        return true;
    }

    // Recording occurs only on the owner thread during a frame, outside any
    // other render/compute pass. x/y use top-left frame coordinates. Callback
    // will be delivered on a later beginFrame and consumed on an owner frame.
    // Opaque triangles and zero-ID scene occluders are drawn first.
    // Eligible standard alpha-masked materials use their original GPU
    // base texture and alphaCutoff in a second depth-tested shader,
    // discarding transparent pixels. Unsupported/custom/Blend paths
    // continue as conservative zero-ID depth blockers. Renderer3D's
    // actual vertex override, LOD and arbitrary external scene inputs
    // remain separate acceptance gates.
    bool record(const render::Model& model,
                const SkinnedWeightSurface& surface,
                std::size_t authorCount,
                std::span<const std::uint32_t> authorIds,
                const render::mat4& viewProj,
                const SculptGpuPickRequest& request,
                std::span<const SculptGpuIdVertex> occluders={},
                std::span<const SculptGpuMaskBatch> maskedOccluders={},
                const SculptGpuVertexSource* actualGpuSource=nullptr,
                bool captureDepth=false,
                const SculptGpuVertexSource* gpuMaskOccluder=nullptr,
                std::span<const SculptGpuVertexSource> gpuMaskOccluders={}) {
        if(!device_||!supported_||!request.valid()||
           !device_->frameSerial() ||
           device_->frameSerial()!=request.frameSerial ||
           request.meshId!=surface.mesh ||
           !pending_.empty() || !retiredWithoutCallback_.empty() ||
           request.width>4096u || request.height>4096u ||
           !device_->gpuHealthy())return false;
        // Stage 7BA: bounded set of independent GPU Mask blockers. Keep the
        // legacy single pointer for ABI/source-compatible existing callers.
        std::vector<const SculptGpuVertexSource*> gpuMaskSources;
        gpuMaskSources.reserve(kSculptGpuMaxMaskOccluders);
        if(gpuMaskOccluder)gpuMaskSources.push_back(gpuMaskOccluder);
        if(gpuMaskOccluders.size()>
           kSculptGpuMaxMaskOccluders-gpuMaskSources.size())return false;
        for(const auto& blocker:gpuMaskOccluders)
            gpuMaskSources.push_back(&blocker);
        if(!gpuMaskSources.empty() &&
           (!actualGpuSource || !supportsActualGpuMaskedStream()))
            return false;
        // No shared source handle may masquerade as independent actors.
        if(!gpuMaskSources.empty() &&
           !sculptGpuMaskSourceSetValid(
               std::span<const SculptGpuVertexSource* const>(
                   gpuMaskSources.data(),gpuMaskSources.size()),
               actualGpuSource->actualVertexOverride))
            return false;
        vertices_.clear();
        std::vector<SculptGpuMaskBatch> grouped;
        if(!appendSculptGpuMaterialGeometry(
                model,surface,authorCount,authorIds,true,
                vertices_,grouped))return false;
        const auto selectedOpaqueCount=vertices_.size();
        std::size_t selectedMaskedCount=0;
        for(const auto& batch:grouped)selectedMaskedCount+=batch.vertices.size();
        const auto selectedSourceCount=selectedOpaqueCount+selectedMaskedCount;
        if(actualGpuSource) {
            // Only the Metal compute path reads actual 60B ModelVertex.
            // Standard homogeneous Mask surfaces use 32B GPU-expanded
            // UV/alpha vertices; mixed/custom material paths fail closed.
            const auto& source=*actualGpuSource;
            if(!device_->supportsCompute() ||
               device_->info().backend!=gfx::Backend::Metal ||
               !source.actualVertexOverride.valid() ||
               source.sourceVertexCount!=model.vertices.size() ||
               source.params.vertices!=model.vertices.size() ||
               source.params.count!=selectedSourceCount ||
               source.indices.size()!=selectedSourceCount ||
               !selectedSourceCount ||
               source.params.skinned!=
                   (source.jointMatrices.empty()?0u:1u) ||
               source.params.jointMatrices!=source.jointMatrices.size() ||
               (source.params.skinned &&
                source.influences.size()!=model.vertices.size()))
                return false;
            if(source.maskedSelection) {
                if(!supportsActualGpuMaskedStream() ||
                   !selectedMaskedCount || selectedOpaqueCount ||
                   !source.maskBaseTexture.valid() ||
                   source.maskInputs.size()!=model.vertices.size())
                    return false;
                // Source alpha and UV are immutable CPU metadata, sampled
                // against the actual renderer-owned GPU-morphed positions.
                std::size_t n=0;
                for(const auto& batch:grouped) {
                    if(batch.baseTexture!=source.maskBaseTexture)
                        return false;
                    for(int k=0;k<4;++k)
                        if(batch.uniform.alpha[k]!=source.maskUniform.alpha[k] ||
                           batch.uniform.uv0[k]!=source.maskUniform.uv0[k] ||
                           batch.uniform.uv1[k]!=source.maskUniform.uv1[k])
                            return false;
                    for(const auto& v:batch.vertices) {
                        if(n>=source.indices.size())return false;
                        const auto index=source.indices[n++];
                        if(index.vertex>=source.maskInputs.size())
                            return false;
                        for(int k=0;k<4;++k) {
                            if(v.rgba[k]!=std::uint8_t(
                                    index.triangleId>>(8u*k)))
                                return false;
                            // The actual GPU position is borrowed, but
                            // static UV and vertex alpha must correspond
                            // to the exact same indexed ModelVertex.
                            if(v.uvAlpha[k]!=
                               source.maskInputs[index.vertex].uvAlpha[k])
                                return false;
                        }
                    }
                }
                for(const auto& value:source.maskInputs)
                    for(float component:value.uvAlpha)
                        if(!std::isfinite(component))return false;
            }else if(!expandCompute_.valid() || !grouped.empty() ||
                     !source.maskInputs.empty())
                return false;
            // Require exact topology/triangle IDs from the CPU-side
            // renderer-selected source, but do not read CPU vertex positions.
            std::size_t sourceOrdinal=0;
            for(const auto& prim:model.meshes[request.meshId].primitives) {
                for(std::size_t i=prim.firstIndex;
                    i<std::size_t(prim.firstIndex)+prim.indexCount;++i) {
                    if(sourceOrdinal>=source.indices.size() ||
                       source.indices[sourceOrdinal++].vertex!=model.indices[i])
                        return false;
                }
            }
            // Selected Mask IDs are in grouped[] instead of vertices_.
            // Comparing to selectedOpaqueCount rejected every legal GPU
            // masked source even when original indices matched exactly.
            if(sourceOrdinal!=selectedSourceCount)return false;
            for(const auto& matrix:source.jointMatrices)
                for(float value:matrix.m)
                    if(!std::isfinite(value))return false;
            for(const auto& v:source.influences)
                for(float weight:v.weights)
                    if(!std::isfinite(weight) ||
                       weight<0.f || weight>1.f)return false;
            for(std::size_t i=0;i<selectedSourceCount;++i) {
                const auto& requestVertex=source.indices[i];
                if(requestVertex.vertex>=model.vertices.size())return false;
                if(!source.maskedSelection) {
                    const auto id=requestVertex.triangleId;
                    const auto& encoded=vertices_[i].rgba;
                    if(encoded[0]!=std::uint8_t(id) ||
                       encoded[1]!=std::uint8_t(id>>8u) ||
                       encoded[2]!=std::uint8_t(id>>16u) ||
                       encoded[3]!=std::uint8_t(id>>24u))return false;
                }
            }
            // Selected Mask triangles will now come exclusively from the
            // GPU-expanded source, not the legacy CPU masked batches.
            if(source.maskedSelection)grouped.clear();
        }
        // Every GPU-sourced mask blocker has a separate live 60B buffer.
        // The external caller must also prove its actual Renderer3D colour
        // submission/LOD/material provenance before calling this ID pass.
        if(maskedOccluders.size()+grouped.size()>256u)return false;
        std::size_t total=vertices_.size()+occluders.size();
        // Homogeneous selected GPU Mask geometry was intentionally
        // removed from grouped[] (CPU positions). Its true GPU-expanded
        // triangles still count toward the scene budget, and a selected
        // Mask with no unrelated occluders is NOT an empty scene.
        if(actualGpuSource && actualGpuSource->maskedSelection) {
            if(actualGpuSource->params.count>3000000u-total)return false;
            total+=actualGpuSource->params.count;
        }
        if(total>3000000u)return false;
        for(const auto& batch:grouped) {
            if(batch.vertices.size()>3000000u-total)return false;
            total+=batch.vertices.size();
        }
        for(const auto& batch:maskedOccluders) {
            if(!batch.baseTexture.valid() || batch.vertices.empty() ||
               batch.vertices.size()%3u ||
               batch.vertices.size()>3000000u-total)
                return false;
            for(const auto& v:batch.vertices) {
                if(v.rgba[0]||v.rgba[1]||v.rgba[2]||v.rgba[3]||
                   !std::isfinite(v.world.x)||
                   !std::isfinite(v.world.y)||
                   !std::isfinite(v.world.z))return false;
                for(float value:v.uvAlpha)
                    if(!std::isfinite(value))return false;
            }
            for(float value:batch.uniform.alpha)
                if(!std::isfinite(value))return false;
            for(float value:batch.uniform.uv0)
                if(!std::isfinite(value))return false;
            for(float value:batch.uniform.uv1)
                if(!std::isfinite(value))return false;
            total+=batch.vertices.size();
        }
        for(const auto* blocker:gpuMaskSources) {
            if(blocker->params.count>3000000u-total)return false;
            total+=blocker->params.count;
        }
        if(!total)return false;
        for(const auto& batch:maskedOccluders)
            grouped.push_back(batch);
        // Explicit scene occluders must never carry a selectable ID.
        // Reject mismatched data instead of accidentally returning the
        // ordinal of a hair/floor/background triangle.
        for(const auto& vertex:occluders) {
            if(vertex.rgba[0]||vertex.rgba[1]||
               vertex.rgba[2]||vertex.rgba[3]||
               !std::isfinite(vertex.world.x)||
               !std::isfinite(vertex.world.y)||
               !std::isfinite(vertex.world.z))return false;
        }
        vertices_.insert(vertices_.end(),occluders.begin(),occluders.end());
        // The selected source triangle positions now come ONLY from the
        // borrowed GPU ModelVertex buffer. Leave CPU-supplied zero-ID scene
        // occluders in this separate legacy vertex allocation.
        if(actualGpuSource)
            vertices_.erase(vertices_.begin(),
                            vertices_.begin()+selectedOpaqueCount);
        for(float v:viewProj.m)if(!std::isfinite(v))return false;
        const int w=int(request.width),h=int(request.height);
        if((!color_.valid()||!depth_.valid()) || w!=width_ || h!=height_) {
            // Resizing is rare. GPU owner must prove the prior submitted
            // frame no longer uses old RTs before reallocating them.
            if((color_.valid()||depth_.valid()) &&
               !device_->waitForResourceIdle())return false;
            if(color_.valid())device_->destroyTexture(color_);
            if(depth_.valid())device_->destroyTexture(depth_);
            color_=depth_={};
            width_=height_=0;
            gfx::TextureDesc d;
            d.width=w;d.height=h;
            d.renderTarget=true;
            d.filter=gfx::Filter::Nearest;
            d.format=gfx::PixelFormat::RGBA8;
            d.label="VIVID sculpt ID color RT";
            auto color=device_->createTexture(d);
            d.format=gfx::PixelFormat::Depth;
            d.label="VIVID sculpt depth RT";
            auto depth=device_->createTexture(d);
            if(!color.valid()||!depth.valid()) {
                if(color.valid())device_->destroyTexture(color);
                if(depth.valid())device_->destroyTexture(depth);
                return false;
            }
            color_=color;depth_=depth;
            width_=w;height_=h;
        }
        gfx::BufferDesc v;
        v.type=gfx::BufferType::Vertex;
        v.usage=gfx::Usage::Immutable;
        gfx::BufferHandle vertex{};
        if(!vertices_.empty()) {
            v.size=vertices_.size()*sizeof(SculptGpuIdVertex);
            v.data=vertices_.data();
            v.label="VIVID opaque triangle IDs (one click)";
            vertex=device_->createBuffer(v);
            if(!vertex.valid())return false;
        }
        std::vector<gfx::BufferHandle> maskBuffers;
        maskBuffers.reserve(grouped.size());
        for(const auto& batch:grouped) {
            if(!batch.baseTexture.valid() || batch.vertices.empty() ||
               batch.vertices.size()%3u) {
                if(vertex.valid())device_->destroyBuffer(vertex);
                for(auto handle:maskBuffers)device_->destroyBuffer(handle);
                return false;
            }
            v.size=batch.vertices.size()*sizeof(SculptGpuMaskVertex);
            v.data=batch.vertices.data();
            v.label="VIVID masked triangle IDs (one click)";
            auto buffer=device_->createBuffer(v);
            if(!buffer.valid()) {
                if(vertex.valid())device_->destroyBuffer(vertex);
                for(auto handle:maskBuffers)device_->destroyBuffer(handle);
                return false; // no GPU commands have been recorded
            }
            maskBuffers.push_back(buffer);
        }
        std::vector<gfx::BufferHandle> expandedScratch;
        struct GpuMaskOccluderDraw {
            const SculptGpuVertexSource* source=nullptr;
            gfx::BufferHandle request{},skin{},rows{},uv{},output{};
        };
        std::vector<GpuMaskOccluderDraw> gpuOccluderDraws;
        gpuOccluderDraws.reserve(gpuMaskSources.size());
        gfx::BufferHandle expandedVertex{};
        if(actualGpuSource) {
            const auto& source=*actualGpuSource;
            expandedScratch.reserve(
                (source.maskedSelection?5u:4u)+gpuMaskSources.size()*5u);
            auto makeInput=[&](const void* data,std::size_t size,
                               const char* label)->gfx::BufferHandle {
                gfx::BufferDesc d{};
                d.type=gfx::BufferType::Storage;
                d.usage=gfx::Usage::Immutable;
                d.size=size;
                d.data=data;
                d.label=label;
                const auto handle=device_->createBuffer(d);
                if(handle.valid())expandedScratch.push_back(handle);
                return handle;
            };
            const std::array<SculptGpuSkinInfluence,1> rigidSkin{};
            const std::array<SculptGpuSkinRows,1> rigidRows{};
            // Upload the same 3x4 row convention as native Renderer3D U3.
            // Position source is still the borrowed real 60B GPU buffer.
            const auto rendererRows=sculptGpuRendererSkinRows(
                std::span<const render::mat4>(source.jointMatrices));
            const auto requestBuffer=makeInput(
                source.indices.data(),
                source.indices.size()*sizeof(SculptGpuSourceIndex),
                "VIVID GPU source triangle index+ID");
            const auto skinInput=source.influences.empty()
                ?std::span<const SculptGpuSkinInfluence>(rigidSkin)
                :std::span<const SculptGpuSkinInfluence>(source.influences);
            const auto skinBuffer=makeInput(
                skinInput.data(),skinInput.size_bytes(),
                "VIVID GPU source vertex joint weights");
            const auto jointInput=rendererRows.empty()
                ?std::span<const SculptGpuSkinRows>(rigidRows)
                :std::span<const SculptGpuSkinRows>(rendererRows);
            const auto matrixBuffer=makeInput(
                jointInput.data(),jointInput.size_bytes(),
                "VIVID MotionWeave joint transform palette");
            gfx::BufferHandle maskInput{};
            if(source.maskedSelection)
                maskInput=makeInput(
                    source.maskInputs.data(),source.maskInputs.size()*
                        sizeof(SculptGpuSourceMaskInput),
                    "VIVID selected source UV and vertex alpha");
            expandedVertex=makeInput(
                nullptr,source.indices.size()*
                    (source.maskedSelection?sizeof(SculptGpuMaskVertex):
                                            sizeof(SculptGpuIdVertex)),
                "VIVID GPU-source morphed/skinned triangle IDs");
            // One immutable GPU index/UV/palette stream and 32-byte
            // expanded output per blocker. Every triangle ID is forced
            // to zero; the GPU will write depth, never a selectable hit.
            for(const auto* blocker:gpuMaskSources) {
                GpuMaskOccluderDraw draw;
                draw.source=blocker;
                auto zeroIds=blocker->indices;
                for(auto& idx:zeroIds)idx.triangleId=0u;
                const auto occMatrices=sculptGpuRendererSkinRows(
                    std::span<const render::mat4>(blocker->jointMatrices));
                const auto occSkinInput=blocker->influences.empty()
                    ?std::span<const SculptGpuSkinInfluence>(rigidSkin)
                    :std::span<const SculptGpuSkinInfluence>(blocker->influences);
                const auto occRowsInput=occMatrices.empty()
                    ?std::span<const SculptGpuSkinRows>(rigidRows)
                    :std::span<const SculptGpuSkinRows>(occMatrices);
                draw.request=makeInput(
                    zeroIds.data(),zeroIds.size()*sizeof(SculptGpuSourceIndex),
                    "VIVID GPU mask blocker immutable ZERO indices");
                draw.skin=makeInput(
                    occSkinInput.data(),occSkinInput.size_bytes(),
                    "VIVID GPU mask blocker original skin weights");
                draw.rows=makeInput(
                    occRowsInput.data(),occRowsInput.size_bytes(),
                    "VIVID GPU mask blocker native skin 3x4 rows");
                draw.uv=makeInput(
                    blocker->maskInputs.data(),
                    blocker->maskInputs.size()*sizeof(SculptGpuSourceMaskInput),
                    "VIVID GPU mask blocker sampled UV and alpha");
                draw.output=makeInput(
                    nullptr,zeroIds.size()*sizeof(SculptGpuMaskVertex),
                    "VIVID GPU mask blocker depth-only output");
                gpuOccluderDraws.push_back(draw);
            }
            const auto compute=source.maskedSelection?
                expandMaskCompute_:expandCompute_;
            if(!requestBuffer.valid()||!skinBuffer.valid()||
               !matrixBuffer.valid()||!expandedVertex.valid()||
               (source.maskedSelection&&!maskInput.valid())||
               std::any_of(gpuOccluderDraws.begin(),
                           gpuOccluderDraws.end(),[](const auto& draw) {
                   return !draw.request.valid() || !draw.skin.valid() ||
                          !draw.rows.valid() || !draw.uv.valid() ||
                          !draw.output.valid();
               }) ||
               !device_->beginCompute(compute)) {
                if(vertex.valid())device_->destroyBuffer(vertex);
                for(auto handle:maskBuffers)device_->destroyBuffer(handle);
                for(auto handle:expandedScratch)device_->destroyBuffer(handle);
                return false; // no compute/draw work was encoded yet
            }
            device_->setComputeBuffer(0,source.actualVertexOverride);
            device_->setComputeBuffer(1,requestBuffer);
            device_->setComputeBuffer(2,skinBuffer);
            device_->setComputeBuffer(3,matrixBuffer);
            device_->setComputeBuffer(4,expandedVertex);
            if(source.maskedSelection)device_->setComputeBuffer(5,maskInput);
            device_->setComputeUniforms(&source.params,sizeof(source.params));
            device_->dispatch(source.params.count);
            device_->endCompute();
            for(const auto& draw:gpuOccluderDraws) {
                // Already encoded the selected actor's compute. Failed
                // later source expansion must retain ALL input/output
                // buffers until a genuine GPU owner-thread idle barrier.
                if(!device_->beginCompute(expandMaskCompute_)) {
                    if(vertex.valid())retiredWithoutCallback_.push_back(vertex);
                    retiredWithoutCallback_.insert(
                        retiredWithoutCallback_.end(),
                        maskBuffers.begin(),maskBuffers.end());
                    retiredWithoutCallback_.insert(
                        retiredWithoutCallback_.end(),
                        expandedScratch.begin(),expandedScratch.end());
                    supported_=false;
                    return false;
                }
                device_->setComputeBuffer(0,draw.source->actualVertexOverride);
                device_->setComputeBuffer(1,draw.request);
                device_->setComputeBuffer(2,draw.skin);
                device_->setComputeBuffer(3,draw.rows);
                device_->setComputeBuffer(4,draw.output);
                device_->setComputeBuffer(5,draw.uv);
                device_->setComputeUniforms(&draw.source->params,
                                            sizeof(draw.source->params));
                device_->dispatch(draw.source->params.count);
                device_->endCompute();
            }
            // The actor's published GPU positions and ID expansion
            // share the gfx-owned ordered Metal command queue. Creator
            // runs the ID after actual Renderer3D colour submission;
            // both passes read the same borrowed current-frame source.
        }
        auto reply=std::make_shared<Reply>();
        // Metal viewport/scissor use the top-left convention exposed by
        // gfx::Device; no framebuffer origin conversion belongs in VIVID.
        gfx::PassDesc pass;
        pass.colorTarget=color_;
        pass.depthTarget=depth_;
        pass.clearColor[0]=pass.clearColor[1]=
            pass.clearColor[2]=pass.clearColor[3]=0;
        device_->beginPass(pass);
        device_->setViewport({0,0,w,h});
        device_->setScissor({int(request.x),int(request.y),1,1});
        if(expandedVertex.valid()) {
            const bool sourceMask=actualGpuSource->maskedSelection;
            device_->setPipeline(sourceMask?maskPipeline_:pipeline_);
            device_->setVertexBuffer(expandedVertex);
            if(sourceMask) {
                device_->setTexture(0,actualGpuSource->maskBaseTexture);
                device_->setUniforms(1,&actualGpuSource->maskUniform,
                                    sizeof(actualGpuSource->maskUniform));
            }
            device_->setUniforms(0,viewProj.m,sizeof(viewProj.m));
            device_->draw(std::uint32_t(actualGpuSource->params.count));
        }
        for(const auto& draw:gpuOccluderDraws) {
            // Independently sampled masks write depth according to their
            // actual GPU-deformed positions; all IDs remain zero.
            device_->setPipeline(maskPipeline_);
            device_->setVertexBuffer(draw.output);
            device_->setTexture(0,draw.source->maskBaseTexture);
            device_->setUniforms(1,&draw.source->maskUniform,
                                sizeof(draw.source->maskUniform));
            device_->setUniforms(0,viewProj.m,sizeof(viewProj.m));
            device_->draw(draw.source->params.count);
        }
        if(vertex.valid()) {
            device_->setPipeline(pipeline_);
            device_->setVertexBuffer(vertex);
            device_->setUniforms(0,viewProj.m,sizeof(viewProj.m));
            device_->draw(std::uint32_t(vertices_.size()));
        }
        // Masked fragments use native GPU sampling and discard. Draw AFTER
        // opaque/zero-ID blockers, sharing their hardware depth buffer.
        for(std::size_t i=0;i<grouped.size();++i) {
            const auto& batch=grouped[i];
            device_->setPipeline(maskPipeline_);
            device_->setVertexBuffer(maskBuffers[i]);
            device_->setTexture(0,batch.baseTexture);
            device_->setUniforms(0,viewProj.m,sizeof(viewProj.m));
            device_->setUniforms(1,&batch.uniform,sizeof(batch.uniform));
            device_->draw(std::uint32_t(batch.vertices.size()));
        }
        device_->endPass();
        // Completion is genuinely asynchronous on Metal. This may fail if
        // a device is unsupported/lost, but the draw already references the
        // buffer; retain it until a verified idle barrier on shutdown.
        bool read=device_->readTexturePixelAsync(
            color_,int(request.x),int(request.y),
            [reply](std::vector<std::uint8_t> bytes) {
                reply->rgba=std::move(bytes);
                reply->ready=true;
            });
        if(!read) {
            if(vertex.valid())retiredWithoutCallback_.push_back(vertex);
            retiredWithoutCallback_.insert(retiredWithoutCallback_.end(),
                                           maskBuffers.begin(),maskBuffers.end());
            retiredWithoutCallback_.insert(retiredWithoutCallback_.end(),
                                           expandedScratch.begin(),
                                           expandedScratch.end());
            // A readback failed AFTER GPU draw encoding. Disable further
            // submissions until shutdown confirms an idle barrier; retrying
            // could otherwise accumulate unbounded 1M-triangle buffers.
            supported_=false;
            return false;
        }
        if(captureDepth && !device_->readTextureDepthPixelAsync(
                depth_,int(request.x),int(request.y),
                [reply](bool valid,float depth) {
                    reply->depthValid=valid;
                    reply->depth01=valid?depth:1.f;
                    reply->depthReady=true;
                })) {
            // A colour readback may already be in-flight; still retain
            // all scratch until the verified GPU-idle shutdown barrier.
            if(vertex.valid())retiredWithoutCallback_.push_back(vertex);
            retiredWithoutCallback_.insert(retiredWithoutCallback_.end(),
                                           maskBuffers.begin(),maskBuffers.end());
            retiredWithoutCallback_.insert(retiredWithoutCallback_.end(),
                                           expandedScratch.begin(),
                                           expandedScratch.end());
            supported_=false;
            return false;
        }
        pending_.push_back({vertex,std::move(maskBuffers),
                            std::move(expandedScratch),
                            actualGpuSource!=nullptr,captureDepth,
                            std::move(reply),request});
        return true;
    }

    // Caller must be at a safe render-owner shutdown/idle point. If native
    // GPU completion cannot be proven, do not release in-flight allocations.
    bool shutdownAfterIdle() {
        if(!device_)return true;
        if(!device_->waitForResourceIdle())return false;
        for(auto& p:pending_) {
            if(p.vertex.valid())device_->destroyBuffer(p.vertex);
            for(auto buffer:p.masked)device_->destroyBuffer(buffer);
            for(auto buffer:p.expandedScratch)device_->destroyBuffer(buffer);
        }
        pending_.clear();
        for(auto b:retiredWithoutCallback_)device_->destroyBuffer(b);
        retiredWithoutCallback_.clear();
        if(color_.valid())device_->destroyTexture(color_);
        if(depth_.valid())device_->destroyTexture(depth_);
        if(maskPipeline_.valid())device_->destroyPipeline(maskPipeline_);
        if(maskShader_.valid())device_->destroyShader(maskShader_);
        if(expandCompute_.valid())device_->destroyCompute(expandCompute_);
        if(expandMaskCompute_.valid())device_->destroyCompute(expandMaskCompute_);
        if(pipeline_.valid())device_->destroyPipeline(pipeline_);
        if(shader_.valid())device_->destroyShader(shader_);
        color_=depth_={};
        pipeline_= {};
        shader_={};
        maskPipeline_={};
        maskShader_={};
        expandCompute_={};
        expandMaskCompute_={};
        device_=nullptr;
        supported_=false;
        width_=height_=0;
        return true;
    }
private:
    // Rare command submission failures: retain these GPU buffers until
    // a verified idle barrier. Never free a buffer in the same GPU frame
    // merely because the async readback request was rejected.
    std::vector<gfx::BufferHandle> retiredWithoutCallback_;
};

} // namespace aiverse::vivid
