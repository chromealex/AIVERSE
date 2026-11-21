// Strata Stage 9: no-double-draw routing preflight on recording RHI.
// Tests command packing only. Never invokes render pass/Renderer3D draws.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataRenderRouting.h"
#include "engine/render/StrataRendererPreview.h"
#include "engine/render/StrataVulkanIndirectContract.h"
#include "engine/render/GlobalGeometryPool.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <utility>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

class RecordingDevice final : public gfx::Device {
public:
    gfx::DeviceInfo meta{gfx::Backend::Metal, true, false, "Strata route mock"};
    std::map<uint32_t, std::vector<uint8_t>> buffers;
    std::string last;
    uint32_t id = 1;
    uint32_t rasterDraws = 0, indirectDraws = 0;
    const gfx::DeviceInfo& info() const override { return meta; }
    const std::string& lastError() const override { return last; }
    bool supportsMultiDrawIndirect() const override { return true; }
    uint64_t frameSerial() const override { return 101; }
    bool supportsCompute() const override { return true; }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& desc) override {
        if (desc.size == 0 || desc.size > 8*1024*1024) return {};
        auto handle = gfx::BufferHandle{id++};
        buffers[handle.id].assign(desc.size, 0);
        if (desc.data)
            std::memcpy(buffers.at(handle.id).data(), desc.data, desc.size);
        return handle;
    }
    void updateBuffer(gfx::BufferHandle handle, const void* data, size_t bytes,
                      size_t offset = 0) override {
        auto& b = buffers.at(handle.id);
        assert(data && offset <= b.size() && bytes <= b.size()-offset);
        std::memcpy(b.data()+offset, data, bytes);
    }
    bool updateSharedBufferChecked(gfx::BufferHandle handle,const void* data,
                                   size_t bytes,size_t offset=0) override {
        if (!buffers.count(handle.id) || !data || !bytes ||
            offset > buffers.at(handle.id).size() ||
            bytes > buffers.at(handle.id).size() - offset) return false;
        updateBuffer(handle,data,bytes,offset);
        return true;
    }
    void destroyBuffer(gfx::BufferHandle handle) override {
        buffers.erase(handle.id);
    }
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override {return {};}
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override {}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override {return {};}
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override {return {};}
    void destroyPipeline(gfx::PipelineHandle) override {}
    void beginFrame(int,int) override {}
    void endFrame() override {}
    void beginPass(const gfx::PassDesc&) override {}
    void endPass() override {}
    void setViewport(const gfx::Rect&) override {}
    void setScissor(const gfx::Rect&) override {}
    void setPipeline(gfx::PipelineHandle) override {}
    void setVertexBuffer(gfx::BufferHandle,size_t=0) override {}
    void setIndexBuffer(gfx::BufferHandle,size_t=0) override {}
    void setTexture(int,gfx::TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t=0) override {++rasterDraws;}
    void drawIndexed(uint32_t,uint32_t=0,int32_t=0,uint32_t=1) override {
        ++rasterDraws;
    }
    bool drawIndexedIndirect(gfx::BufferHandle,size_t,uint32_t,
                             uint32_t=sizeof(gfx::DrawIndexedIndirectCommand)) override {
        ++indirectDraws;
        return true;
    }
};

static ModelAsset buildSourceAsset() {
    ModelAsset source;
    ModelPrimitiveAsset p;
    p.mesh = 0;
    p.primitive = 0;
    p.material = 0;
    auto& a = p.geometry;
    a.indices = {0,1,2, 0,1,2, 1,3,2};
    a.nodes.resize(3);
    a.bounds.resize(3);
    a.roots={0};
    for(uint32_t i=0;i<3;++i) {
        a.nodes[i].firstIndex=i*3;
        a.nodes[i].indexCount=3;
        a.nodes[i].materialGroup=0;
        a.nodes[i].resident=true;
    }
    a.nodes[0].firstChild=1;
    a.nodes[0].childCount=2;
    a.nodes[0].geometricError=.2f;
    a.bounds[0]={{-.5f,-.5f,.2f},{.5f,.5f,.8f}};
    a.bounds[1]={{-.5f,-.5f,.2f},{0.f,.5f,.8f}};
    a.bounds[2]={{0.f,-.5f,.2f},{.5f,.5f,.8f}};
    source.primitives.push_back(std::move(p));
    return source;
}

static void testMultiMaterialGpuPackets() {
    RecordingDevice device;
    device.meta.backend=gfx::Backend::Vulkan;
    Model model;
    model.batching.autoLod=false;
    model.vertices.resize(8);
    model.indices={0,1,2,1,3,2,4,5,6,5,7,6};
    model.materials.resize(2);
    model.materials[0].baseColor={1.f,.1f,.1f,1.f};
    model.materials[1].baseColor={.1f,.3f,1.f,1.f};
    model.meshes.resize(1);
    for(uint32_t i=0;i<2;++i){
        Primitive p{};
        p.firstIndex=i*6;
        p.indexCount=6;
        p.firstVertex=i*4;
        p.vertexCount=4;
        p.material=int(i);
        model.meshes[0].primitives.push_back(p);
    }
    Node node{};
    node.mesh=0;
    model.nodes.push_back(node);
    model.order={0};
    model.bindWorld={mat4::identity()};
    ModelAsset asset;
    for(uint32_t i=0;i<2;++i){
        ModelPrimitiveAsset entry{};
        entry.mesh=0;entry.primitive=i;entry.material=i;
        auto& g=entry.geometry;
        g.indices.assign(model.indices.begin()+i*6,
                         model.indices.begin()+i*6+6);
        g.roots={0};
        ClusterNode leaf{};
        leaf.resident=true;
        leaf.indexCount=6;
        leaf.materialGroup=i;
        g.nodes.push_back(leaf);
        g.bounds.push_back({{-1.f,-1.f,.1f},{1.f,1.f,.9f}});
        asset.primitives.push_back(std::move(entry));
    }
    assert(uploadModelToGeometryPool(device,model));
    GpuMetadataScene metadata;
    assert(metadata.stage(device,"two/materials",model,asset));
    GpuMetadataScene::IndexBinding binding{};
    assert(metadata.indexBinding("two/materials",model,binding));
    assert(binding.indexCount==12);
    GpuPassConfig camera{};
    camera.viewProjection=mat4::identity();
    camera.width=1280;
    camera.height=720;
    camera.maxOutputClusters=16;
    std::string error;
    ModelDraw draw{};
    draw.model=&model;
    draw.flags=kDrawStatic;
    for(uint32_t ordinal=0;ordinal<2;++ordinal){
        VulkanIndirectContract contract{};
        GpuPassConfig guarded{};
        assert(prepareVulkanIndirectPrimitiveContract(device,model,asset,
            binding,ordinal,camera,false,contract,guarded,&error));
        assert(contract.primitiveOrdinal==ordinal);
        assert(contract.expectedMaterial==ordinal);
        assert(guarded.gpuFallbackEnabled);
        assert(guarded.gpuFallbackIndexCount==6);
        assert(guarded.gpuFallbackFirstIndex==
               model.gpuFirstIndex+ordinal*6);
        GpuIndexedSelectionPacket packet{};
        packet.commands={100+ordinal};
        packet.vertex=contract.vertex;
        packet.index=contract.index;
        packet.selectedWorld=mat4::identity();
        packet.viewProjection=camera.viewProjection;
        packet.viewWidth=camera.width;
        packet.viewHeight=camera.height;
        packet.pass=GpuIndexedPass::MainOpaque;
        packet.frameSerial=contract.expectedFrameSerial;
        packet.geometryRevision=contract.geometryRevision;
        packet.materialRevision=contract.materialRevision;
        packet.commandCapacity=16;
        packet.sourceFirstIndex=ordinal*6;
        packet.sourceIndexCount=6;
        packet.sourceMesh=0;
        packet.sourcePrimitive=ordinal;
        packet.expectedMaterial=ordinal;
        packet.sourceBaseVertex=contract.pooledBaseVertex;
        packet.exclusive=true;
        draw.gpuIndexedPrimitives.push_back(packet);
    }
    assert(draw.gpuIndexedPrimitives.size()==2);
    // A sun-shadow camera has its OWN projection and GPU slots. It must
    // never alias the main-camera indirect buffer or be reused in spot
    // shadow/planar reflection passes.
    GpuPassConfig sun=camera;
    sun.viewProjection=mat4::scale({.5f,.75f,1.f});
    sun.width=1024;
    sun.height=1024;
    for(uint32_t ordinal=0;ordinal<2;++ordinal){
        VulkanIndirectContract shadowContract{};
        GpuPassConfig guarded{};
        assert(prepareVulkanIndirectPrimitiveContract(device,model,asset,
            binding,ordinal,sun,false,shadowContract,guarded,&error));
        auto shadow=draw.gpuIndexedPrimitives[ordinal];
        shadow.commands={300+ordinal};
        shadow.pass=GpuIndexedPass::SunShadow;
        shadow.viewProjection=guarded.viewProjection;
        shadow.viewWidth=guarded.width;
        shadow.viewHeight=guarded.height;
        draw.gpuShadowPrimitives.push_back(shadow);
    }
    assert(draw.gpuShadowPrimitives.size()==2);
    assert(draw.gpuShadowPrimitives[0].commands!=
           draw.gpuIndexedPrimitives[0].commands);
    assert(draw.gpuShadowPrimitives[0].viewWidth==1024);
    assert(draw.gpuShadowPrimitives[0].pass==GpuIndexedPass::SunShadow);
    assert(draw.gpuIndexedPrimitives[0].matchesView(
        GpuIndexedPass::MainOpaque,camera.viewProjection,1280,720));
    assert(!draw.gpuIndexedPrimitives[0].matchesView(
        GpuIndexedPass::SunShadow,sun.viewProjection,1024,1024));
    assert(draw.gpuShadowPrimitives[0].matchesView(
        GpuIndexedPass::SunShadow,sun.viewProjection,1024,1024));
    assert(!draw.gpuShadowPrimitives[0].matchesView(
        GpuIndexedPass::SunShadow,camera.viewProjection,1024,1024));
    assert(!draw.gpuShadowPrimitives[0].matchesView(
        GpuIndexedPass::PlanarReflection,sun.viewProjection,1024,1024));
    // Spot-shadow and reflection use independent camera selections,
    // NEVER the earlier sun/main array even when source geometry is equal.
    auto spot=draw.gpuShadowPrimitives.front();
    spot.pass=GpuIndexedPass::SpotShadow;
    spot.viewProjection=mat4::scale({.7f,.6f,1.f});
    spot.viewWidth=spot.viewHeight=512;
    spot.commands={567};
    assert(spot.matchesView(GpuIndexedPass::SpotShadow,
        spot.viewProjection,512,512));
    assert(!spot.matchesView(GpuIndexedPass::SunShadow,
        sun.viewProjection,1024,1024));
    auto mirror=draw.gpuIndexedPrimitives.front();
    mirror.pass=GpuIndexedPass::PlanarReflection;
    mirror.viewProjection=mat4::scale({-1.f,1.f,1.f});
    mirror.commands={568};
    assert(mirror.matchesView(GpuIndexedPass::PlanarReflection,
        mirror.viewProjection,camera.width,camera.height));
    assert(!mirror.matchesView(GpuIndexedPass::MainOpaque,
        camera.viewProjection,camera.width,camera.height));
    assert(spot.commands!=mirror.commands);
    assert(draw.gpuIndexedPrimitives[0].commands!=
           draw.gpuIndexedPrimitives[1].commands);
    ModelDraw anotherInstance=draw;
    anotherInstance.world=mat4::translate({4.f,0.f,0.f});
    anotherInstance.gpuIndexedPrimitives[0].selectedWorld=
        anotherInstance.world;
    assert(draw.gpuIndexedPrimitives[0].selectedWorld.m[12]==0.f);
    assert(anotherInstance.gpuIndexedPrimitives[0].selectedWorld.m[12]==4.f);
    VulkanIndirectContract legacyContract{};
    GpuPassConfig legacyGuarded{};
    assert(!prepareVulkanIndirectContract(device,model,asset,binding,
        camera,false,legacyContract,legacyGuarded,&error)); // old API rejects
    model.materials[1].alpha=AlphaMode::Blend;
    VulkanIndirectContract rejected{};
    GpuPassConfig guarded{};
    assert(!prepareVulkanIndirectPrimitiveContract(device,model,asset,
        binding,0,camera,false,rejected,guarded,&error));
    model.materials[1].alpha=AlphaMode::Opaque;
    model.markMaterialChanged();
    GpuSelectionSubmission submission{};
    submission.ranges={199};submission.counters={200};
    submission.indirect={201};
    submission.capacity=16;submission.submittedInstances=1;
    submission.frameSerial=device.frameSerial();
    VulkanIndirectContract fresh{};
    assert(prepareVulkanIndirectPrimitiveContract(device,model,asset,
        binding,1,camera,false,fresh,guarded,&error));
    assert(validateVulkanIndirectSubmission(device,model,fresh,
        submission,&error));
    model.markMaterialChanged();
    assert(!validateVulkanIndirectSubmission(device,model,fresh,
        submission,&error));
    metadata.shutdown(device);
    releaseModelGeometryPool(device,model);
    assert(device.buffers.empty());
}

int main() {
    testMultiMaterialGpuPackets();
    RecordingDevice device;
    Model model;
    model.materials.resize(1);
    model.vertices.resize(4);
    model.indices = {0,1,2,1,3,2};
    model.meshes.resize(1);
    Primitive original;
    original.indexCount=6;
    original.material=0;
    model.meshes[0].primitives.push_back(original);
    Node node;
    node.mesh=0;
    model.nodes.push_back(node);

    const auto asset=buildSourceAsset();
    RenderCandidate candidate;
    std::string error;
    GpuMetadataScene residency;
    ViewConfig view;
    view.width=800; view.height=600;
    view.errorThresholdPixels=1.f;
    assert(!prepareRenderCandidate(model,asset,residency,"static",0,0,
                                   RenderPass::MainOpaque,view,candidate,&error));
    assert(!error.empty());
    assert(uploadModelToGeometryPool(device,model));
    assert(residency.stage(device,"static",model,asset));

    // Stage 10 renderer dry-run: evaluate the same ModelDraw against
    // separate camera and sun projections while making ZERO draw calls.
    model.order.push_back(0);
    model.bindWorld.push_back(mat4::identity());
    ModelDraw draw;
    draw.model = &model;
    draw.flags = kDrawStatic;
    PreviewContext preview{};
    preview.mainViewProjection = mat4::identity();
    preview.sunViewProjection = mat4::identity();
    preview.mainWidth = 800; preview.mainHeight = 600;
    preview.sunWidth = 1024; preview.sunHeight = 1024;
    preview.sunEnabled = true;
    const auto previewResult = previewModelDraw(
        draw, asset, residency, "static", preview);
    assert(previewResult.attempted == 2 && previewResult.ready == 2);
    assert(previewResult.selectedClusters == 4);
    assert(previewResult.rejected == 0);
    assert(device.rasterDraws == 0 && device.indirectDraws == 0);

    // Unsupported blend/tint settings are NEVER 'preview-ready', but
    // the actual Renderer3D::drawModel path remains untouched.
    draw.tint.w = 0.5f;
    const auto unsafe = previewModelDraw(
        draw, asset, residency, "static", preview);
    assert(unsafe.skippedUnsafeDraws == 1 && unsafe.attempted == 0);
    draw.tint.w = 1.f;
    model.materials[0].alpha = AlphaMode::Blend;
    const auto materialRejected = previewModelDraw(
        draw, asset, residency, "static", preview);
    assert(materialRejected.ready == 0 &&
           materialRejected.rejected == 2);
    model.materials[0].alpha = AlphaMode::Opaque;

    assert(prepareRenderCandidate(model,asset,residency,"static",0,0,
                                  RenderPass::MainOpaque,view,candidate,&error));
    assert(candidate.source==&model);
    assert(candidate.mesh==0 && candidate.primitive==0 && candidate.material==0);
    assert(candidate.selectedClusters==2 && candidate.ranges.size()==2);
    GpuMetadataScene::IndexBinding indices;
    assert(residency.indexBinding("static",model,indices));
    assert(candidate.indexBuffer==indices.buffer &&
           candidate.vertexBuffer==model.vb);
    assert(candidate.ranges[0].firstIndex==indices.firstIndex+3);
    assert(candidate.ranges[1].firstIndex==indices.firstIndex+6);
    assert(candidate.ranges[0].baseVertex==indices.baseVertex);

    // Stage 16.2 conservative Vulkan GPU-authored packet admission.
    // The renderer is still exclusively Legacy until hardware image
    // parity has been explicitly established. The CPU contract checks
    // original-primitive fallback indices, immutability, frame identity
    // and the common pooled VB/IB address space before the GPU dispatch.
    device.meta.backend=gfx::Backend::Vulkan;
    GpuPassConfig gpuView;
    gpuView.viewProjection=mat4::identity();
    gpuView.width=800;gpuView.height=600;
    gpuView.maxOutputClusters=16;
    VulkanIndirectContract contract{};
    GpuPassConfig guarded{};
    assert(prepareVulkanIndirectContract(device,model,asset,indices,
        gpuView,false,contract,guarded,&error));
    assert(contract.source==&model);
    assert(guarded.gpuFallbackEnabled);
    assert(guarded.gpuFallbackFirstIndex==
           model.gpuFirstIndex+original.firstIndex);
    assert(guarded.gpuFallbackIndexCount==original.indexCount);
    assert(guarded.baseVertex==int32_t(model.gpuBaseVertex));
    GpuSelectionSubmission authored{};
    authored.ranges={98};authored.counters={99};authored.indirect={100};
    authored.capacity=16;
    authored.submittedInstances=1;
    authored.frameSerial=device.frameSerial();
    assert(validateVulkanIndirectSubmission(device,model,
        contract,authored,&error));
    // A real Scene forwards this packet by VALUE through the single
    // Renderer3D::drawModel() call. The payload must be independent of
    // temporary culling submissions and reject nonexclusive/stale IDs.
    ModelDraw gpuDraw{};
    gpuDraw.model=&model;
    gpuDraw.flags=kDrawStatic;
    gpuDraw.gpuIndexed.commands=authored.indirect;
    gpuDraw.gpuIndexed.vertex=contract.vertex;
    gpuDraw.gpuIndexed.index=contract.index;
    gpuDraw.gpuIndexed.viewProjection=gpuView.viewProjection;
    gpuDraw.gpuIndexed.viewWidth=gpuView.width;
    gpuDraw.gpuIndexed.viewHeight=gpuView.height;
    gpuDraw.gpuIndexed.frameSerial=contract.expectedFrameSerial;
    gpuDraw.gpuIndexed.geometryRevision=contract.geometryRevision;
    gpuDraw.gpuIndexed.materialRevision=contract.materialRevision;
    gpuDraw.gpuIndexed.commandCapacity=authored.capacity;
    gpuDraw.gpuIndexed.sourceFirstIndex=original.firstIndex;
    gpuDraw.gpuIndexed.sourceIndexCount=original.indexCount;
    gpuDraw.gpuIndexed.sourceBaseVertex=contract.pooledBaseVertex;
    gpuDraw.gpuIndexed.exclusive=true;
    assert(gpuDraw.gpuIndexed.valid());
    ModelDraw copied=gpuDraw;
    assert(copied.gpuIndexed.valid() &&
           copied.gpuIndexed.commands==authored.indirect);
    copied.gpuIndexed.exclusive=false;
    assert(!copied.gpuIndexed.valid());
    copied=gpuDraw;
    copied.gpuIndexed.commands={};
    assert(!copied.gpuIndexed.valid());
    authored.submittedInstances=2;
    assert(!validateVulkanIndirectSubmission(device,model,
        contract,authored,&error));
    authored.submittedInstances=1;
    assert(!prepareVulkanIndirectContract(device,model,asset,indices,
        gpuView,true,contract,guarded,&error)); // active shadow/mirror
    const auto saveAlpha=model.materials[0].alpha;
    model.materials[0].alpha=AlphaMode::Blend;
    assert(!prepareVulkanIndirectContract(device,model,asset,indices,
        gpuView,false,contract,guarded,&error));
    model.materials[0].alpha=saveAlpha;
    device.meta.backend=gfx::Backend::Metal;

    RenderRouteGate gate;
    assert(chooseExclusiveRenderRoute(gate)==RenderRoute::Legacy);
    std::vector<gfx::DrawIndexedIndirectCommand> commands;
    gfx::DrawIndexedIndirectCommand untouched;
    untouched.firstIndex=1234;
    commands.push_back(untouched);
    assert(!encodeRendererCommands(model,residency,candidate,7,gate,commands));
    assert(commands.size()==1 && commands.front().firstIndex==1234);
    gate.requested=true;
    gate.sourceAndMaterialSafe=true;
    gate.sourceResident=true;
    gate.rendererOwnsSubmission=true;
    // Both parity gates must be accepted explicitly before any Strata
    // source route can replace the original renderer path.
    assert(chooseExclusiveRenderRoute(gate)==RenderRoute::Legacy);
    gate.cpuGpuSelectionParityVerified=true;
    assert(chooseExclusiveRenderRoute(gate)==RenderRoute::Legacy);
    gate.rasterParityVerified=true;
    assert(chooseExclusiveRenderRoute(gate)==RenderRoute::Strata);
    assert(!encodeRendererCommands(model,residency,candidate,invalidNode,gate,commands));
    assert(commands.size()==1 && commands.front().firstIndex==1234);

    assert(encodeRendererCommands(model,residency,candidate,7,gate,commands));
    assert(commands.size()==2);
    for(size_t i=0;i<commands.size();++i) {
        assert(commands[i].firstIndex==candidate.ranges[i].firstIndex);
        assert(commands[i].indexCount==3);
        assert(commands[i].instanceCount==1);
        assert(commands[i].baseVertex==indices.baseVertex);
        assert(commands[i].firstInstance==7);
    }
    assert(device.rasterDraws==0 && device.indirectDraws==0);

    // A stale/missing GPU sidecar and a live material edit must be rejected,
    // even if the caller forgot to increment materialRevision.
    GpuMetadataScene absent;
    assert(!encodeRendererCommands(model,absent,candidate,7,gate,commands));
    model.materials[0].alpha=AlphaMode::Blend;
    assert(!encodeRendererCommands(model,residency,candidate,7,gate,commands));
    model.materials[0].alpha=AlphaMode::Opaque;
    model.meshes[0].shadowBatch.indexCount=6;
    assert(!prepareRenderCandidate(model,asset,residency,"static",0,0,
                                   RenderPass::SunShadow,view,candidate,&error));
    model.meshes[0].shadowBatch.indexCount=0;

    // A second pass with a large pixel threshold is a different, valid
    // *exclusive* candidate. It selects the parent, not its two children.
    view.errorThresholdPixels=5000.f;
    assert(prepareRenderCandidate(model,asset,residency,"static",0,0,
                                  RenderPass::SunShadow,view,candidate,&error));
    assert(candidate.ranges.size()==1);
    assert(candidate.ranges[0].firstIndex==indices.firstIndex);
    assert(encodeRendererCommands(model,residency,candidate,8,gate,commands));
    assert(commands.size()==1 && commands.front().firstInstance==8);
    // Completely offscreen is an exclusive empty route, not fallback to
    // drawing the source's full legacy primitive.
    view.objectToClip=mat4::translate({10,0,0});
    assert(prepareRenderCandidate(model,asset,residency,"static",0,0,
                                  RenderPass::MainOpaque,view,candidate,&error));
    assert(candidate.ranges.empty());
    assert(encodeRendererCommands(model,residency,candidate,8,gate,commands));
    assert(commands.empty());
    view.objectToClip=mat4::identity();

    // Updated materials/geometry invalidate previously prepared commands.
    assert(prepareRenderCandidate(model,asset,residency,"static",0,0,
                                  RenderPass::MainOpaque,view,candidate,&error));
    model.markMaterialChanged();
    assert(!encodeRendererCommands(model,residency,candidate,7,gate,commands));
    model.materials[0].alpha=AlphaMode::Blend;
    assert(!prepareRenderCandidate(model,asset,residency,"static",0,0,
                                   RenderPass::MainOpaque,view,candidate,&error));
    model.materials[0].alpha=AlphaMode::Opaque;
    model.materials[0].castShadow=false;
    assert(!prepareRenderCandidate(model,asset,residency,"static",0,0,
                                   RenderPass::SunShadow,view,candidate,&error));
    model.materials[0].castShadow=true;
    assert(prepareRenderCandidate(model,asset,residency,"static",0,0,
                                  RenderPass::MainOpaque,view,candidate,&error));
    model.markGeometryChanged();
    assert(!encodeRendererCommands(model,residency,candidate,7,gate,commands));
    assert(!prepareRenderCandidate(model,asset,residency,"static",0,0,
                                   RenderPass::MainOpaque,view,candidate,&error));

    residency.shutdown(device);
    releaseModelGeometryPool(device,model);
    assert(device.buffers.empty());
    assert(device.rasterDraws==0 && device.indirectDraws==0);
    return 0;
}
