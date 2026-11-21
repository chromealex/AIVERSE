// STRATA Stage 16 Vulkan GLSL/SPIR-V contract and optional real GPU parity.
// The test is authored in the feature branch; execution is deferred to the
// owner's final native driver-enabled build. No success is presumed here.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataGpuCulling.h"
#include "engine/render/StrataVulkanCullingShaders.h"
#include "engine/render/Geometry.h"
#include "engine/gfx/vulkan/VkShaderCompiler.h"
#include "engine/gfx/vulkan/Vulkan.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;
using namespace aiverse::gfx;

// Both public Vulkan resource ABIs must coexist on one device and frame.
static void testComputeLayouts(gfx::Device& device) {
    const char* textureAbi=R"(
layout(set=0,binding=12,std430) buffer Output { uint value; } outputData;
layout(set=0,binding=16,std140) uniform Params { uint value; } params;
void main() { if(gl_GlobalInvocationID.x==0) outputData.value=params.value; }
)";
    const char* bufferAbi=R"(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
layout(set=0,binding=0,std430) buffer Output { uint value; } outputData;
layout(push_constant) uniform Params { uint value; } params;
void main() { if(gl_GlobalInvocationID.x==0) outputData.value+=params.value; }
)";
    auto texture=device.createCompute({nullptr,textureAbi,"texture resource ABI"});
    auto buffer=device.createCompute({nullptr,bufferAbi,"buffer resource ABI",
                                      VulkanComputeLayout::BufferPushConstants});
    assert(texture.valid() && buffer.valid());
    BufferDesc desc{};
    desc.type=BufferType::Storage;desc.usage=Usage::Dynamic;desc.size=16;
    const auto output=device.createBuffer(desc);
    assert(output.valid());
    device.beginFrame(64,64);
    assert(device.beginCompute(texture));
    device.setComputeBuffer(0,output);
    const std::array<uint32_t,4> initial{{37,0,0,0}};
    device.setComputeUniforms(initial.data(),sizeof(initial));
    assert(device.dispatchChecked(1,1));
    device.endCompute();
    assert(device.beginCompute(buffer));
    device.setComputeBuffer(0,output);
    const uint32_t increment=5;
    device.setComputeUniforms(&increment,sizeof(increment));
    assert(device.dispatchChecked(1,1));
    // A rejected binding must prevent submission, not silently reuse slot zero.
    device.setComputeBuffer(8,output);
    assert(!device.dispatchChecked(1,1));
    device.endCompute();
    bool ready=false;
    assert(device.readBufferAsync(output,4,[&](std::vector<uint8_t> bytes) {
        assert(bytes.size()==4);
        uint32_t value=0;std::memcpy(&value,bytes.data(),4);
        assert(value==42);ready=true;
    }));
    device.endFrame();
    for(int i=0;i<12 && !ready;++i) {
        device.beginFrame(64,64);device.endFrame();
    }
    assert(ready);
    device.destroyBuffer(output);
    device.destroyCompute(texture);device.destroyCompute(buffer);
}

static ModelAsset sample() {
    ModelAsset out;
    ModelPrimitiveAsset p{};
    p.mesh=0;p.primitive=0;p.material=2;
    p.geometry.indices={0,1,2};
    p.geometry.roots={0};
    ClusterNode node{};
    node.resident=true;
    node.materialGroup=2;
    node.firstIndex=0;
    node.indexCount=3;
    p.geometry.nodes.push_back(node);
    p.geometry.bounds.push_back({{-0.2f,-0.2f,0.2f},{0.2f,0.2f,0.8f}});
    out.primitives.push_back(std::move(p));
    return out;
}

static ModelAsset temporalFixture() {
    ModelAsset source;
    ModelPrimitiveAsset primitive{};
    primitive.mesh=0;primitive.primitive=0;primitive.material=3;
    auto& g=primitive.geometry;
    g.indices={0,1,2,1,3,2,0,1,2,1,3,2};
    g.roots={0};
    g.nodes.resize(3);
    g.bounds.resize(3);
    g.nodes[0].firstChild=1;
    g.nodes[0].childCount=2;
    g.nodes[0].firstIndex=0;
    g.nodes[0].indexCount=6;
    g.nodes[0].geometricError=.25f;
    g.nodes[1].firstIndex=6;g.nodes[1].indexCount=3;
    g.nodes[2].firstIndex=9;g.nodes[2].indexCount=3;
    for(auto& node:g.nodes){node.resident=true;node.materialGroup=3;}
    for(auto& bounds:g.bounds)
        bounds={{-.5f,-.5f,.2f},{.5f,.5f,.8f}};
    source.primitives.push_back(std::move(primitive));
    return source;
}

// This tests ACTUAL GPU-computed refinement history, not a recording-RHI
// simulation. All reads are asynchronous diagnostic-only and are never
// involved in normal Renderer3D raster command submission.
static void testVulkanTemporalParity(gfx::Device& device) {
    const ModelAsset model=temporalFixture();
    std::string error;
    std::vector<uint8_t> bytes;
    assert(packGpuMetadata(model,bytes,&error));
    GpuMetadataHeader header{};
    std::memcpy(&header,bytes.data(),sizeof(header));
    header.version=2;
    std::memcpy(bytes.data(),&header,sizeof(header));
    GpuPrimitive gpuPrimitive{};
    std::memcpy(&gpuPrimitive,
        bytes.data()+header.primitivesByteOffset,sizeof(gpuPrimitive));
    gpuPrimitive.gpuFirstIndex=400;
    std::memcpy(bytes.data()+header.primitivesByteOffset,
        &gpuPrimitive,sizeof(gpuPrimitive));
    gfx::BufferDesc description{};
    description.type=gfx::BufferType::Storage;
    description.usage=gfx::Usage::Immutable;
    description.size=bytes.size();
    description.data=bytes.data();
    const auto metadata=device.createBuffer(description);
    assert(metadata.valid());

    const auto instance=makeGpuSceneInstance(mat4::identity(),0,919);
    GpuPassConfig pass{};
    pass.viewProjection=mat4::identity();
    pass.width=800;pass.height=600;
    pass.maxOutputClusters=16;
    pass.hysteresisFraction=.2f;
    pass.temporalNodeCount=3;
    pass.temporalPassId=1; // main camera
    pass.temporalRevision=77;
    pass.frustumCull=false;
    const auto& bounds=model.primitives[0].geometry.bounds[0];
    const float errorPixels=geometryErrorPixels(pass.viewProjection,
        {bounds.min[0],bounds.min[1],bounds.min[2]},
        {bounds.max[0],bounds.max[1],bounds.max[2]},
        model.primitives[0].geometry.nodes[0].geometricError,
        pass.width,pass.height,pass.clipDepthZeroToOne);
    assert(std::isfinite(errorPixels) && errorPixels>0.f);

    // The GPU must see four CONSECUTIVE frame serials to retain history.
    // Never block between frames to read the previous temporal state; all
    // four RHI reads are queued and delivered after GPU completion.
    struct FrameResult {
        GpuPassConfig config{};
        GpuSelectionReadback clusters{};
        std::vector<uint8_t> historyBytes;
        std::vector<uint8_t> previousCpuHistory;
        std::vector<uint8_t> expectedCpuHistory;
        bool clustersReady=false, historyReady=false;
    };
    std::array<FrameResult,4> frames{};
    GpuClusterCulling culling;
    std::vector<uint8_t> previous;
    const float factors[]{.95f,1.08f,1.30f,.70f};
    const uint32_t expectedRanges[]{2,2,1,2};
    for(size_t index=0;index<frames.size();++index){
        FrameResult& target=frames[index];
        target.config=pass;
        target.config.errorThresholdPixels=errorPixels*factors[index];
        target.previousCpuHistory=previous;
        ViewConfig cpuView=cpuReferenceView(target.config,instance);
        if(!previous.empty())cpuView.previousRefined=&previous;
        ViewPlan reference{};
        assert(planView(model.primitives[0].geometry,cpuView,reference,&error));
        assert(reference.refinedNodes.size()==3);
        assert(reference.ranges.size()==expectedRanges[index]);
        target.expectedCpuHistory=reference.refinedNodes;
        previous=reference.refinedNodes;

        device.beginFrame(64,64);
        GpuSelectionSubmission selection{};
        assert(culling.dispatch(device,metadata,{instance},target.config,
                               device.frameSerial(),selection));
        assert(selection.temporalApplied && selection.temporalState.valid());
        assert(culling.requestReadback(device,selection,
            [&,index](GpuSelectionReadback read) {
                frames[index].clusters=std::move(read);
                frames[index].clustersReady=true;
            }));
        assert(device.readBufferAsync(selection.temporalState,
            3*sizeof(uint32_t),
            [&,index](std::vector<uint8_t> data) {
                frames[index].historyBytes=std::move(data);
                frames[index].historyReady=true;
            }));
        device.endFrame();
    }
    for(int n=0;n<5;++n){
        bool all=true;
        for(const auto& frame:frames)
            all &= frame.clustersReady && frame.historyReady;
        if(all)break;
        device.beginFrame(64,64);
        device.endFrame();
    }
    for(size_t i=0;i<frames.size();++i){
        const auto& frame=frames[i];
        assert(frame.clustersReady && frame.clusters.valid());
        assert(frame.historyReady &&
               frame.historyBytes.size()==3*sizeof(uint32_t));
        assert(frame.clusters.ranges.size()==expectedRanges[i]);
        assert(compareGpuSelection(model,{instance},frame.config,400,
            frame.clusters,&error,
            frame.previousCpuHistory.empty()
                ? nullptr:&frame.previousCpuHistory));
        std::vector<uint8_t> observed(3,0);
        for(size_t node=0;node<3;++node){
            uint32_t flag=0;
            std::memcpy(&flag,
                frame.historyBytes.data()+node*sizeof(flag),sizeof(flag));
            assert(flag<=1);
            observed[node]=uint8_t(flag);
        }
        assert(observed==frame.expectedCpuHistory);
    }
    culling.shutdown(device);
    device.destroyBuffer(metadata);
}

int main(int argc,char** argv) {
    const bool compileOnly=argc==2 && std::string(argv[1])=="--compile-only";
    const bool requireDevice=argc==2 && std::string(argv[1])=="--require-device";
    if(argc>2 || (argc==2 && !compileOnly && !requireDevice)) {
        std::cerr<<"usage: strata_vulkan_kernel [--compile-only|--require-device]\n";
        return 2;
    }
    std::string error;
    std::vector<uint32_t> spirv;
    assert(aiverse::gfx::vk::compileComputeProgram(
        kVulkanGpuCulling,spirv,&error));
    assert(error.empty() && spirv.size()>20);
    assert(spirv.front()==0x07230203u); // SPIR-V magic
    const auto saved=spirv;
    assert(!aiverse::gfx::vk::compileComputeProgram(
        "#version 450\nvoid main(){missing_function();}",spirv,&error));
    assert(!error.empty() && spirv==saved);
    std::cout<<"[strata-vulkan] GLSL/SPIR-V compiler contract PASS\n";
    if(compileOnly)return 0;

    auto device=createVulkanDevice(VulkanWindow{},&error);
    if(!device) {
        std::cout<<"[strata-vulkan] "<<(requireDevice ? "FAIL" : "SKIP")
                 <<" no device: "<<error<<"\n";
        return requireDevice ? 1 : 77;
    }
    if(!device->supportsCompute()) {
        std::cout<<"[strata-vulkan] "<<(requireDevice ? "FAIL" : "SKIP")
                 <<" compute unsupported\n";
        return requireDevice ? 1 : 77;
    }
    testComputeLayouts(*device);
    const auto asset=sample();
    std::vector<uint8_t> metadata;
    assert(packGpuMetadata(asset,metadata,&error));
    GpuMetadataHeader header{};
    std::memcpy(&header,metadata.data(),sizeof(header));
    header.version=2;
    std::memcpy(metadata.data(),&header,sizeof(header));
    GpuPrimitive primitive{};
    std::memcpy(&primitive,metadata.data()+header.primitivesByteOffset,
                sizeof(primitive));
    primitive.gpuFirstIndex=400;
    std::memcpy(metadata.data()+header.primitivesByteOffset,
                &primitive,sizeof(primitive));
    gfx::BufferDesc desc{};
    desc.type=gfx::BufferType::Storage;
    desc.usage=gfx::Usage::Immutable;
    desc.data=metadata.data();
    desc.size=metadata.size();
    desc.label="strata Vulkan metadata validation fixture";
    const auto gpuMetadata=device->createBuffer(desc);
    assert(gpuMetadata.valid());

    GpuClusterCulling culling;
    auto instance=makeGpuSceneInstance(mat4::identity(),0,121);
    GpuPassConfig view;
    view.viewProjection=mat4::identity();
    view.width=640;view.height=480;
    view.maxOutputClusters=16;
    view.errorThresholdPixels=1.0f;
    view.baseVertex=7; // verify indexed Vulkan vertexOffset propagation
    bool callbackCalled=false;
    GpuSelectionReadback result;
    device->beginFrame(64,64);
    GpuSelectionSubmission submission;
    const uint64_t serial=device->frameSerial();
    assert(culling.dispatch(*device,gpuMetadata,{instance},
                            view,serial,submission));
    assert(submission.valid() && submission.gpuIndirectPrepared());
    assert(culling.requestReadback(*device,submission,
        [&](GpuSelectionReadback read){
            callbackCalled=true;
            result=std::move(read);
        }));
    bool commandsReady=false;
    std::vector<uint8_t> commands;
    assert(device->readBufferAsync(submission.indirect,
        sizeof(gfx::DrawIndexedIndirectCommand)*view.maxOutputClusters,
        [&](std::vector<uint8_t> bytes){
            commands=std::move(bytes);
            commandsReady=true;
        }));
    assert(!callbackCalled && !commandsReady);
    device->endFrame();
    // Frame slots are fence-guarded. Two subsequent submissions allow
    // async Vulkan readback callback to arrive when the originating slot
    // has completed, with no explicit CPU GPU-wait in the render path.
    for(int frame=0;frame<3 && (!callbackCalled || !commandsReady);++frame){
        device->beginFrame(64,64);
        device->endFrame();
    }
    assert(callbackCalled && result.valid());
    assert(result.counters.invalid==0 && result.counters.overflow==0);
    assert(compareGpuSelection(asset,{instance},view,400,result,&error));
    // Stage16.4c: real headless GPU selection on two *different* cameras
    // (spot-depth projection and mirrored planar view). No main-view
    // range or indirect buffer may be treated as their valid output.
    const mat4 auxiliaryCameras[]={
        mat4::scale({.75f,.65f,1.f}),
        mat4::scale({-1.f,1.f,1.f})
    };
    for(const auto& projection:auxiliaryCameras){
        auto perPass=view;
        perPass.viewProjection=projection;
        bool passReady=false;
        GpuSelectionReadback selected{};
        device->beginFrame(64,64);
        GpuSelectionSubmission submitted{};
        assert(culling.dispatch(*device,gpuMetadata,{instance},perPass,
                                device->frameSerial(),submitted));
        assert(submitted.gpuIndirectPrepared());
        assert(culling.requestReadback(*device,submitted,
            [&](GpuSelectionReadback read){
                selected=std::move(read);
                passReady=true;
            }));
        device->endFrame();
        for(int n=0;n<3 && !passReady;++n){
            device->beginFrame(64,64);
            device->endFrame();
        }
        assert(passReady && selected.valid());
        assert(compareGpuSelection(asset,{instance},perPass,400,
                                   selected,&error));
    }
    assert(commandsReady && commands.size()==
           sizeof(gfx::DrawIndexedIndirectCommand)*view.maxOutputClusters);
    gfx::DrawIndexedIndirectCommand first{};
    std::memcpy(&first,commands.data(),sizeof(first));
    assert(first.indexCount==3 && first.instanceCount==1);
    assert(first.firstIndex==400 && first.baseVertex==7);
    assert(first.firstInstance==0);
    for(uint32_t i=1;i<view.maxOutputClusters;++i){
        gfx::DrawIndexedIndirectCommand zero{};
        std::memcpy(&zero,commands.data()+
                    size_t(i)*sizeof(zero),sizeof(zero));
        assert(zero.indexCount==0 && zero.instanceCount==0);
    }
    // GPU-side overflow/invalid guard is independent of CPU readback:
    // malformed primitive references must zero every indirect command.
    const auto invalidInstance=makeGpuSceneInstance(mat4::identity(),99,121);
    device->beginFrame(64,64);
    GpuSelectionSubmission rejected;
    assert(culling.dispatch(*device,gpuMetadata,{invalidInstance},view,
                            device->frameSerial(),rejected));
    assert(rejected.gpuIndirectPrepared());
    std::vector<uint8_t> rejectedCommands;
    bool rejectedReady=false;
    assert(device->readBufferAsync(rejected.indirect,
        sizeof(gfx::DrawIndexedIndirectCommand)*view.maxOutputClusters,
        [&](std::vector<uint8_t> bytes){
            rejectedCommands=std::move(bytes);
            rejectedReady=true;
        }));
    device->endFrame();
    for(int frame=0;frame<3 && !rejectedReady;++frame){
        device->beginFrame(64,64);
        device->endFrame();
    }
    assert(rejectedReady);
    for(uint32_t i=0;i<view.maxOutputClusters;++i){
        gfx::DrawIndexedIndirectCommand command{};
        std::memcpy(&command,rejectedCommands.data()+
                    size_t(i)*sizeof(command),sizeof(command));
        assert(command.indexCount==0);
    }
    // With a strictly validated full-primitive fallback contract, invalid
    // GPU selection must submit ONE original source command instead of
    // leaving an empty frame. This test only checks GPU command bytes; the
    // material and transform gate is tested independently by the router.
    auto guarded=view;
    guarded.gpuFallbackEnabled=true;
    guarded.gpuFallbackFirstIndex=123;
    guarded.gpuFallbackIndexCount=3;
    guarded.gpuFallbackBaseVertex=7;
    device->beginFrame(64,64);
    GpuSelectionSubmission fallback;
    assert(culling.dispatch(*device,gpuMetadata,{invalidInstance},guarded,
                            device->frameSerial(),fallback));
    assert(fallback.gpuIndirectPrepared());
    bool fallbackReady=false;
    std::vector<uint8_t> fallbackCommands;
    assert(device->readBufferAsync(fallback.indirect,
        sizeof(gfx::DrawIndexedIndirectCommand)*guarded.maxOutputClusters,
        [&](std::vector<uint8_t> bytes){
            fallbackReady=true;
            fallbackCommands=std::move(bytes);
        }));
    device->endFrame();
    for(int frame=0;frame<3 && !fallbackReady;++frame){
        device->beginFrame(64,64);
        device->endFrame();
    }
    assert(fallbackReady && fallbackCommands.size()==
           guarded.maxOutputClusters*sizeof(gfx::DrawIndexedIndirectCommand));
    gfx::DrawIndexedIndirectCommand original{};
    std::memcpy(&original,fallbackCommands.data(),sizeof(original));
    assert(original.indexCount==3 && original.instanceCount==1);
    assert(original.firstIndex==123 && original.baseVertex==7);
    for(uint32_t i=1;i<guarded.maxOutputClusters;++i){
        gfx::DrawIndexedIndirectCommand empty{};
        std::memcpy(&empty,fallbackCommands.data()+
                    size_t(i)*sizeof(empty),sizeof(empty));
        assert(empty.indexCount==0 && empty.instanceCount==0);
    }

    culling.shutdown(*device);
    device->destroyBuffer(gpuMetadata);
    testVulkanTemporalParity(*device);
    std::cout<<"[strata-vulkan] compute GPU selection parity PASS\n";
    return 0;
}
