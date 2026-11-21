// Engine regression: static batching preserves runtime-mutable subtrees.
// This is intentionally independent from any particular project/genre.
#include "engine/render/Model.h"
#include "engine/render/ModelTextureCache.h"
#include "engine/render/Animation.h" // full NodePose definition for active-pose regression
#include "engine/render/ModelMerge.h"
#include "engine/render/GpuMaterialState.h"
#include "engine/render/GlobalGeometryPool.h"
#include "engine/render/PersistentGpuScene.h"
#include "engine/render/PersistentGpuSceneUpload.h"
#include "engine/render/GpuSceneDrawCommands.h"
#include "engine/render/GpuIcbPolicy.h"
#include "engine/render/StaticModelCachePolicy.h"
#include "engine/render/StaticCrossModelBatchPolicy.h"
#include "engine/render/ModelLodTransition.h"
#include "engine/render/EcsStaticModelRender.h"
#include "engine/render/ModelShaders.h"
#include "engine/render/ModelSamplingPolicy.h"
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "model batching check failed: %s at %d\n", #x, __LINE__); std::exit(1); } } while (0)
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <map>
#include <vector>
#include <string>

using namespace aiverse::render;
// GPU-free device verifies byte-accurate shared geometry offsets and release
// behaviour on every CI platform, independently of Metal/Vulkan availability.
class MockDevice final : public aiverse::gfx::Device {
public:
    aiverse::gfx::DeviceInfo deviceInfo{};
    std::string error;
    std::map<uint32_t,std::vector<uint8_t>> buffers;
    uint32_t nextId = 1;
    int created = 0, destroyed = 0;
    int createAttempts = 0, failCreateOn = 0;
    int sharedWrites = 0, failSharedOn = 0;
    uint64_t serial = 0, completed = 0;
    uint64_t frameSerial() const override { return serial; }
    uint64_t completedFrameSerial() const override { return completed; }
    const aiverse::gfx::DeviceInfo& info() const override { return deviceInfo; }
    const std::string& lastError() const override { return error; }
    bool supportsMultiDrawIndirect() const override { return indirectSupported; }
    bool indirectSupported = true;
    bool fakeMetalCompute = false;
    bool supportsCompute() const override { return fakeMetalCompute; }
    aiverse::gfx::ComputeHandle createCompute(const aiverse::gfx::ComputeDesc&) override {
        return fakeMetalCompute ? aiverse::gfx::ComputeHandle{1} : aiverse::gfx::ComputeHandle{};
    }
    bool beginCompute(aiverse::gfx::ComputeHandle) override { return fakeMetalCompute; }
    void setComputeBuffer(int, aiverse::gfx::BufferHandle) override {}
    void setComputeUniforms(const void*, size_t) override {}
    void dispatch(uint32_t, uint32_t) override {}
    void endCompute() override {}
    aiverse::gfx::BufferHandle createBuffer(const aiverse::gfx::BufferDesc& d) override {
        if (++createAttempts == failCreateOn) return {};
        auto id = nextId++;
        buffers[id].resize(d.size);
        if (d.data && d.size) std::memcpy(buffers[id].data(), d.data, d.size);
        ++created; return {id};
    }
    void updateBuffer(aiverse::gfx::BufferHandle h, const void* data, size_t size, size_t offset) override {
        auto& v = buffers.at(h.id);
        CHECK(offset <= v.size() && size <= v.size() - offset);
        if (size) std::memcpy(v.data() + offset, data, size);
    }
    bool updateSharedBufferChecked(aiverse::gfx::BufferHandle h,
                                   const void* data, size_t size, size_t offset) override {
        if (++sharedWrites == failSharedOn) return false;
        updateBuffer(h, data, size, offset);
        return true;
    }
    void destroyBuffer(aiverse::gfx::BufferHandle h) override {
        CHECK(buffers.erase(h.id) == 1);
        ++destroyed;
    }
    struct TextureRequest {int width,height,mips;bool generate;};
    std::vector<TextureRequest> textureRequests;
    bool acceptTextureRegions=false;int textureRegionWrites=0;
    aiverse::gfx::TextureHandle createTexture(const aiverse::gfx::TextureDesc& d) override {
        textureRequests.push_back({d.width,d.height,d.mipLevels,d.mipmaps});
        return {100u+uint32_t(textureRequests.size())};
    }
    bool updateTextureRegion(aiverse::gfx::TextureHandle,const aiverse::gfx::TextureUploadDesc& d) override {
        CHECK(d.mipLevel>0&&d.width>0&&d.height>0&&d.rowPitchBytes==d.width*4&&d.data);
        if(acceptTextureRegions)++textureRegionWrites;
        return acceptTextureRegions;
    }
    void updateTexture(aiverse::gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(aiverse::gfx::TextureHandle) override {}
    aiverse::gfx::ShaderHandle createShader(const aiverse::gfx::ShaderDesc&) override { return {1}; }
    void destroyShader(aiverse::gfx::ShaderHandle) override {}
    aiverse::gfx::PipelineHandle createPipeline(const aiverse::gfx::PipelineDesc&) override { return {1}; }
    void destroyPipeline(aiverse::gfx::PipelineHandle) override {}
    void beginFrame(int,int) override {}
    void endFrame() override {}
    void beginPass(const aiverse::gfx::PassDesc&) override {}
    void endPass() override {}
    void setViewport(const aiverse::gfx::Rect&) override {}
    void setScissor(const aiverse::gfx::Rect&) override {}
    void setPipeline(aiverse::gfx::PipelineHandle) override {}
    void setVertexBuffer(aiverse::gfx::BufferHandle,size_t) override {}
    void setIndexBuffer(aiverse::gfx::BufferHandle,size_t) override {}
    void setTexture(int,aiverse::gfx::TextureHandle) override {}
    void setUniforms(int,const void*,size_t) override {}
    void draw(uint32_t,uint32_t) override {}
    void drawIndexed(uint32_t,uint32_t,int32_t,uint32_t) override {}
};


static void verifyVehiclePartsRemainIndependent() {
    // No project assets are required. Each part has a different mesh and its
    // own transform, while several parts intentionally share the SAME material.
    // A draw-submission optimizer must not alter wheel/door node identity.
    Model car;
    car.path = "test-vehicle-separated-meshes";
    car.materials.resize(2);
    car.materials[1].alpha = AlphaMode::Blend;
    car.materials[1].transparent = true;
    const char* names[] = {"body", "wheel_fl", "wheel_fr", "wheel_rl",
                           "wheel_rr", "door_left", "glass"};
    for (int n = 0; n < 7; ++n) {
        const uint32_t firstVertex = uint32_t(car.vertices.size());
        const uint32_t firstIndex = uint32_t(car.indices.size());
        for (int k = 0; k < 3; ++k) {
            ModelVertex v;
            v.pos = {float(k == 1), float(k == 2), 0};
            v.nrm = {0, 0, 1};
            car.vertices.push_back(v);
            car.indices.push_back(firstVertex + uint32_t(k));
        }
        Primitive p;
        p.firstIndex = firstIndex; p.indexCount = 3;
        p.firstVertex = firstVertex; p.vertexCount = 3;
        p.material = n == 6 ? 1 : 0;
        MeshDef mesh;
        mesh.name = names[n];
        mesh.primitives.push_back(p);
        Node node;
        node.name = names[n];
        node.mesh = n;
        node.t = {float(n * 2), 0, 0};
        node.runtimeMutable = n > 0 && n < 6;
        car.meshes.push_back(std::move(mesh));
        car.nodes.push_back(std::move(node));
    }
    car.finalize();
    const auto worldBefore = car.bindWorld;
    car.batching.mergeRigid = true;
    prepareModelUpload(car);
    CHECK(car.uploadPrepared);
    // Wheels and doors still have their own mesh, node ID and bind transform.
    // Glass uses separate transparent state and must not join the opaque run.
    for (int n = 1; n <= 5; ++n) {
        CHECK(car.nodes[size_t(n)].name == names[n]);
        CHECK(car.nodes[size_t(n)].mesh >= 0);
        CHECK(car.bindWorld[size_t(n)].transformPoint({0, 0, 0}).x ==
              worldBefore[size_t(n)].transformPoint({0, 0, 0}).x);
    }
    CHECK(car.nodes[6].mesh >= 0);
    CHECK(car.materials[size_t(car.meshes[size_t(car.nodes[6].mesh)]
                               .primitives[0].material)].transparent);
    // A GPU batch of different geometry must preserve source index offsets
    // and the independent object matrices. The source meshes are not merged.
    for (int n = 1; n <= 6; ++n) {
        const auto& primitive = car.meshes[size_t(car.nodes[size_t(n)].mesh)]
                                    .primitives[0];
        CHECK(primitive.firstIndex + primitive.indexCount <= car.indices.size());
        CHECK(primitive.indexCount == 3);
    }
    // Independent Material allocations with the same resolved GPU parameters
    // are equivalent, even when they live in different Model objects.
    Model copy;
    copy.materials = car.materials;
    CHECK(equivalentGpuMaterial(car, car.materials[0], copy, copy.materials[0]));
    CHECK(gpuMaterialStateHash(car, car.materials[0]) ==
          gpuMaterialStateHash(copy, copy.materials[0]));
    copy.materials[0].roughness *= .75f;
    CHECK(!equivalentGpuMaterial(car, car.materials[0], copy, copy.materials[0]));
    CHECK(!equivalentGpuMaterial(car, car.materials[0], car, car.materials[1]));
}

static Model pooledFixture(float dx) {
    Model m;
    m.path = "geometry-pool-fixture";
    m.materials.resize(1);
    m.nodes.resize(1);
    m.nodes[0].mesh = 0;
    m.meshes.resize(1);
    m.meshes[0].primitives.resize(1);
    m.meshes[0].primitives[0].indexCount = 3;
    m.meshes[0].primitives[0].vertexCount = 3;
    m.vertices.resize(3);
    m.vertices[0].pos = {dx, 0, 0};
    m.vertices[1].pos = {dx + 1, 0, 0};
    m.vertices[2].pos = {dx, 1, 0};
    for (auto& v : m.vertices) v.nrm = {0, 0, 1};
    m.indices = {0, 1, 2};
    m.finalize();
    return m;
}



// Source-only tests for retained whole-pair recycling and upload failure
// recovery. The MockDevice does not execute GPU commands: these prove only
// allocator and CPU/RHI contract logic, not Vulkan/Metal pixel parity.
static void verifyGeometryPoolRetirementAndUploadRecovery() {
    {
        MockDevice dev;
        dev.serial = 10;
        auto large = pooledFixture(1);
        large.vertices.resize(12);
        large.indices.resize(12, 1);
        auto keeper = pooledFixture(2);
        CHECK(uploadModel(dev, large, true));
        CHECK(uploadModel(dev, keeper, true));
        const uint32_t oldFirst = large.gpuFirstIndex;
        const uint32_t oldBase = large.gpuBaseVertex;
        const uint32_t oldVBBytes = large.gpuPoolVertexBytes;
        const uint32_t oldIBBytes = large.gpuPoolIndexBytes;
        CHECK(oldVBBytes > 3 * sizeof(ModelVertex));
        CHECK(oldIBBytes > 3 * sizeof(uint32_t));
        releaseModel(dev, large);
        CHECK(geometryPoolStats(dev).retiredPairs == 1);
        CHECK(geometryPoolStats(dev).reusablePairs == 0);

        // Still in-flight. A new model must use a different physical span.
        dev.serial = 11;
        auto early = pooledFixture(3);
        CHECK(uploadModel(dev, early, true));
        CHECK(early.gpuBaseVertex != oldBase);

        dev.completed = 10;
        auto smaller = pooledFixture(4);
        CHECK(uploadModel(dev, smaller, true));
        CHECK(smaller.gpuBaseVertex == oldBase);
        CHECK(smaller.gpuFirstIndex == oldFirst);
        CHECK(smaller.gpuPoolVertexBytes == oldVBBytes);
        CHECK(smaller.gpuPoolIndexBytes == oldIBBytes);
        CHECK(geometryPoolStats(dev).recycledUploads == 1);

        // Releasing the smaller successor MUST retain the full old pair.
        releaseModel(dev, smaller);
        dev.completed = 10; // its current retirement serial 11 isn't ready
        auto waiting = pooledFixture(5);
        waiting.vertices.resize(12);
        waiting.indices.resize(12, 1);
        CHECK(uploadModel(dev, waiting, true));
        CHECK(waiting.gpuBaseVertex != oldBase);

        dev.completed = 11;
        auto restored = pooledFixture(6);
        restored.vertices.resize(12);
        restored.indices.resize(12, 1);
        CHECK(uploadModel(dev, restored, true));
        CHECK(restored.gpuBaseVertex == oldBase);
        CHECK(restored.gpuFirstIndex == oldFirst);
        CHECK(geometryPoolStats(dev).recycledUploads == 2);
        releaseModel(dev, restored);
        releaseModel(dev, waiting);
        releaseModel(dev, early);
        releaseModel(dev, keeper);
        CHECK(dev.buffers.empty() && geometryPoolStats(dev).pages == 0);
    }
    {
        // Review 2026-11-21: a fence-completed topmost release returns its
        // span to the page bump pointers (LIFO), even when it is too small
        // for whole-pair reuse; an in-flight release never does.
        MockDevice dev;
        dev.serial = 30;
        auto bottom = pooledFixture(30);
        auto top = pooledFixture(31);
        CHECK(uploadModel(dev, bottom, true));
        CHECK(uploadModel(dev, top, true));
        const uint32_t topBase = top.gpuBaseVertex;
        const uint32_t topFirst = top.gpuFirstIndex;
        const size_t occupiedTwo = geometryPoolStats(dev).occupiedBytes;
        releaseModel(dev, top); // retire frame 30
        dev.completed = 29;     // still in flight
        auto big = pooledFixture(32);
        big.vertices.resize(12);
        big.indices.resize(12, 1);
        CHECK(uploadModel(dev, big, true));
        CHECK(big.gpuBaseVertex > topBase);
        releaseModel(dev, big); // retire frame 30 as well
        dev.serial = 31;
        dev.completed = 30;
        const uint64_t recycledBefore = geometryPoolStats(dev).recycledUploads;
        auto big2 = pooledFixture(33);
        big2.vertices.resize(12);
        big2.indices.resize(12, 1);
        CHECK(uploadModel(dev, big2, true));
        // Both released tail pairs collapsed; big2 starts where top did.
        CHECK(big2.gpuBaseVertex == topBase);
        CHECK(big2.gpuFirstIndex + 12 == topFirst + 3);
        CHECK(geometryPoolStats(dev).recycledUploads == recycledBefore);
        CHECK(geometryPoolStats(dev).retiredPairs == 0);
        CHECK(geometryPoolStats(dev).occupiedBytes ==
              occupiedTwo - 3 * sizeof(ModelVertex) - 3 * sizeof(uint32_t) +
              12 * sizeof(ModelVertex) + 12 * sizeof(uint32_t));
        releaseModel(dev, big2);
        releaseModel(dev, bottom);
        CHECK(dev.buffers.empty() && geometryPoolStats(dev).pages == 0);
    }
    {
        MockDevice dev;
        dev.failSharedOn = 2; // vertex copy succeeded, index copy failed
        auto model = pooledFixture(10);
        CHECK(uploadModel(dev, model, true)); // standalone fallback
        CHECK(!model.geometryPoolPage);
        CHECK(model.vb.valid() && model.ib.valid() && model.vb != model.ib);
        CHECK(dev.destroyed == 1); // rejected new shared page was retired
        CHECK(geometryPoolStats(dev).pages == 0);
        releaseModel(dev, model);
        CHECK(dev.buffers.empty());
    }
    {
        MockDevice dev;
        dev.indirectSupported = false; // force separate VB / IB
        dev.failCreateOn = 2; // second standalone buffer allocation fails
        auto model = pooledFixture(20);
        CHECK(!uploadModel(dev, model, false));
        CHECK(!model.vb.valid() && !model.ib.valid());
        CHECK(model.vertices.size() == 3 && model.indices.size() == 3);
        CHECK(dev.buffers.empty());
        dev.failCreateOn = 0;
        CHECK(uploadModel(dev, model, false));
        CHECK(model.vb.valid() && model.ib.valid());
        CHECK(model.vertices.empty() && model.indices.empty());
        releaseModel(dev, model);
        CHECK(dev.buffers.empty());
    }
    {
        MockDevice dev;auto model=pooledFixture(31);
        model.images.resize(2);
        for(auto& image:model.images){image.width=image.height=2;image.rgba.assign(16,255);}
        model.materials[0].alpha=AlphaMode::Mask;model.materials[0].baseTex=0;model.materials[0].mrTex=1;
        CHECK(uploadModel(dev,model,false,true,true));
        CHECK(!model.vertices.empty()&&!model.indices.empty());
        CHECK(!model.images[0].rgba.empty()&&model.images[1].rgba.empty());
        releaseModel(dev,model);CHECK(dev.buffers.empty());
    }

}

static void verifyCutoutMipUploadPolicy() {
    for(int policy=0;policy<6;++policy) {
        MockDevice dev;dev.acceptTextureRegions=policy!=1;
        auto model=pooledFixture(71);model.images.resize(1);
        auto& image=model.images[0];image.width=image.height=4;image.rgba.assign(64,255);
        for(size_t i=3;i<image.rgba.size();i+=8)image.rgba[i]=0;
        const auto original=image.rgba;
        model.materials[0].alpha=AlphaMode::Mask;model.materials[0].baseTex=0;
        if(policy==2)image.nearest=true;
        if(policy==5)model.materials[0].baseColor.w=.5f;
        if(policy==3){model.materials.push_back(Material{});model.materials.back().baseTex=0;}
        if(policy==4){model.materials.push_back(model.materials[0]);model.materials.back().alphaCutoff=.7f;}
        CHECK(uploadModel(dev,model,true));
        CHECK(image.rgba==original);
        CHECK(image.alphaCoverageThreshold==(policy<2?128:policy==5?255:0));
        CHECK(image.alphaCoverageMipmaps==(policy==0||policy==5));
        if(policy==0||policy==5){CHECK(dev.textureRegionWrites==2);CHECK(dev.textureRequests[0].mips==3&&!dev.textureRequests[0].generate);}
        if(policy==1){CHECK(dev.textureRequests.size()==2&&dev.textureRequests.back().generate);}
        releaseModel(dev,model);
    }
    MockDevice dev;dev.acceptTextureRegions=true;
    auto cache=std::make_shared<ModelTextureCache>();
    Image first;first.textureKey="shared-cutout";first.width=first.height=4;first.rgba.assign(64,255);first.alphaCoverageThreshold=128;
    Image second=first;second.alphaCoverageThreshold=191;
    cache->upload(dev,first);cache->upload(dev,second);
    CHECK(first.tex.valid()&&second.tex.valid()&&!(first.tex==second.tex));
    Image replay;replay.textureKey="shared-cutout";replay.alphaCoverageThreshold=128;
    CHECK(cache->borrowResident(replay)&&replay.tex==first.tex&&replay.alphaCoverageMipmaps);
    cache->shutdown(dev);
}

static void verifyIncrementalBatchingWorkspaceAndRhiCounters() {
    // Stage 21-24: current-pass exact page/material grouping, epoch-safe
    // membership, and deterministic bucket sorting of the admitted prefix.
    StaticCrossModelPreflightWorkspace workspace;
    workspace.beginPass(10);
    const StaticCrossModelPreflightKey high{nullptr, 20u, 3u, 17u, 19u};
    const StaticCrossModelPreflightKey low{nullptr, 10u, 3u, 17u, 19u};
    const uint16_t highGroup = workspace.findOrCreate(high);
    const uint16_t lowGroup = workspace.findOrCreate(low);
    CHECK(highGroup != StaticCrossModelPreflightWorkspace::kInvalidGroup);
    CHECK(lowGroup != StaticCrossModelPreflightWorkspace::kInvalidGroup);
    CHECK(highGroup != lowGroup);
    CHECK(workspace.findOrCreate(high) == highGroup);
    Model first, second;
    Primitive primitiveA, primitiveB;
    auto& admitted = workspace.groups()[highGroup].group;
    admitted.observe(&first, &primitiveA, 2);
    admitted.observe(&second, &primitiveB, 2);
    CHECK(admitted.opportunity(2, 16));
    admitted.ready = true;
    admitted.permitted = true;
    auto& earlier = workspace.groups()[lowGroup].group;
    earlier.ready = earlier.permitted = true;
    for (uint32_t index = 1; index <= 4; ++index) {
        workspace.rememberItem(index, index <= 2 ? highGroup : lowGroup);
    }
    workspace.activateModes(10);
    for (uint32_t index = 1; index <= 4; ++index)
        workspace.rememberMode(index, 1);
    CHECK(workspace.modeOf(1) == 1);
    CHECK(workspace.modeOf(5) == 0);
    CHECK(workspace.prepareSortRanks() == 2);
    CHECK(workspace.sortRankForPermittedItem(3) <
          workspace.sortRankForPermittedItem(1));
    std::vector<uint32_t> order{4, 1, 3, 2, 8};
    CHECK(workspace.bucketizePermittedPrefix(
        order, 4, [](uint32_t a, uint32_t b) { return a < b; }));
    const std::vector<uint32_t> expected{3, 4, 1, 2, 8};
    CHECK(order == expected);
    // Invalid prefix does not corrupt the caller's ordering.
    std::vector<uint32_t> invalid{5, 1, 2};
    const auto unchanged = invalid;
    CHECK(!workspace.bucketizePermittedPrefix(
        invalid, 3, [](uint32_t a, uint32_t b) { return a < b; }));
    CHECK(invalid == unchanged);
    workspace.beginPass(10);
    CHECK(workspace.groupForItem(1) ==
          StaticCrossModelPreflightWorkspace::kInvalidGroup);
    CHECK(workspace.modeOf(1) == 0);
    workspace.activateModes(10);
    CHECK(workspace.modeOf(1) == 0);
    workspace.clearModes();
    CHECK(workspace.modeOf(1) == 0);
    workspace.release();
    CHECK(workspace.retainedBytes() == 0);

    // Stage 28-29: counter differences are monotonic and retained-capacity
    // gauges are deliberately not added to per-pass transfer deltas.
    aiverse::gfx::BufferDesc desc{};
    CHECK(!desc.discardOnUpdate);
    aiverse::gfx::BufferUpdateStats before{};
    before.available = true;
    before.updateCalls = 2;
    before.requestedBytes = 160;
    before.mappedWriteBytes = 192;
    before.cpuShadowWriteBytes = 40;
    before.comparedBytes = 80;
    before.skippedWriteBytes = 20;
    before.preservedBytes = 32;
    before.allocations = 3;
    before.reused = 4;
    before.retainedBufferBytes = 4096;
    auto after = before;
    ++after.updateCalls;
    after.requestedBytes += 80;
    after.mappedWriteBytes += 64;
    after.cpuShadowWriteBytes += 80;
    after.comparedBytes += 80;
    after.skippedWriteBytes += 16;
    after.preservedBytes += 8;
    after.allocations += 1;
    after.reused += 2;
    after.retainedBufferBytes = 8192;
    const auto delta = aiverse::gfx::bufferUpdateCounterDelta(before, after);
    CHECK(delta.available && delta.updateCalls == 1);
    CHECK(delta.requestedBytes == 80);
    CHECK(delta.mappedWriteBytes == 64);
    CHECK(delta.cpuShadowWriteBytes == 80);
    CHECK(delta.comparedBytes == 80);
    CHECK(delta.skippedWriteBytes == 16);
    CHECK(delta.preservedBytes == 8);
    CHECK(delta.allocations == 1 && delta.reused == 2);
    CHECK(delta.retainedBufferBytes == 0 && delta.retainedCpuShadowBytes == 0);
    aiverse::gfx::BufferUpdateStats sum{};
    aiverse::gfx::accumulateBufferUpdateCounters(sum, delta);
    aiverse::gfx::accumulateBufferUpdateCounters(sum, delta);
    CHECK(sum.available && sum.updateCalls == 2);
    CHECK(sum.mappedWriteBytes == 128 && sum.allocations == 2);
    CHECK(sum.retainedBufferBytes == 0);
    auto reset = after;
    reset.updateCalls = 0;
    CHECK(!aiverse::gfx::bufferUpdateCounterDelta(after, reset).available);
    CHECK(!aiverse::gfx::bufferUpdateCounterDelta(before, {}).available);
    const auto initiallyCreated = aiverse::gfx::bufferUpdateCounterDelta({}, after);
    CHECK(initiallyCreated.available && initiallyCreated.allocations == after.allocations);
}

int main() {
    // Resource admission must agree between pipeline compilation and binding.
    // In particular, constrained RHIs retain base PBR instead of losing meshes.
    {
        auto plan = modelSamplingPlan(16,4,true,false,false,false,true,true);
        CHECK(!plan.graph && plan.graphBudgetFallback && plan.alphaR8);
        plan = modelSamplingPlan(16,3,true,false,false,false,false,true);
        CHECK(plan.graph && plan.graphSlot==13);
        plan = modelSamplingPlan(16,3,true,false,false,true,false,true);
        CHECK(!plan.graph && plan.graphBudgetFallback && plan.dynamicGiSlot==13);
        plan = modelSamplingPlan(32,4,true,false,false,true,false,true);
        CHECK(plan.graph && plan.graphSlot==14 && plan.dynamicGiSlot==13);
        plan = modelSamplingPlan(16,2,true,true,true,false,false,true);
        CHECK(plan.graph && !plan.virtualTexture && plan.graphSlot==13);
        plan = modelSamplingPlan(16,1,true,true,true,true,false,true);
        CHECK(plan.graph && !plan.virtualTexture && plan.graphSlot==14);
        plan = modelSamplingPlan(16,0,false,false,true,true,true,true);
        CHECK(!plan.graph && !plan.virtualTexture && plan.alphaR8 && plan.dynamicGiSlot==13);
        plan = modelSamplingPlan(32,4,true,true,true,true,false,true);
        CHECK(plan.graph && plan.virtualTexture && plan.graphSlot==13);
        CHECK(plan.atlasSlot==17 && plan.tableSlot==18 && plan.dynamicGiSlot==19);
        plan = modelSamplingPlan(32,1,true,true,true,true,false,false);
        CHECK(plan.graph && !plan.virtualTexture && plan.graphSlot==14);
        plan = modelSamplingPlan(16,size_t(-1),true,true,true,true,false,true);
        CHECK(!plan.graph && plan.graphBudgetFallback && plan.virtualTexture);
        // Every combination keeps admitted bindings inside its advertised budget.
        for (int slots : {16,32}) for(size_t sources=0;sources<=5;++sources)
        for(int flags=0;flags<64;++flags) {
            const bool graph=flags&1, usesVt=flags&2, vt=flags&4;
            const bool gi=flags&8, alpha=flags&16, allow=flags&32;
            const auto p=modelSamplingPlan(slots,sources,graph,usesVt,vt,gi,alpha,allow);
            CHECK(!p.graph || p.graphSlot+int(sources)<=slots);
            CHECK(!p.virtualTexture || (p.atlasSlot<slots && p.tableSlot<slots));
            CHECK(!gi || p.dynamicGiSlot<slots);
            CHECK(!p.graph || !p.alphaR8);
            CHECK(!p.virtualTexture || (allow && vt && (!p.graph || usesVt)));
            CHECK(!gi || !p.virtualTexture || p.dynamicGiSlot>p.tableSlot);
        }
    }

    // This process tests both serial-fenced retirement and a dedicated
    // fallback. Per-process production default remains OFF.
#ifdef _WIN32
    CHECK(_putenv_s("AIVERSE_GEOMETRY_POOL_RECYCLE", "1") == 0);
#else
    CHECK(setenv("AIVERSE_GEOMETRY_POOL_RECYCLE", "1", 1) == 0);
#endif
    verifyVehiclePartsRemainIndependent();
    verifyCutoutMipUploadPolicy();
    verifyIncrementalBatchingWorkspaceAndRhiCounters();
    verifyGeometryPoolRetirementAndUploadRecovery();
    Model m;
    m.materials.resize(1);
    constexpr int kNodes = 5;
    m.vertices.resize(kNodes * 3);
    m.indices.resize(kNodes * 3);
    const char* names[kNodes] = {"building_wall", "building_roof", "hinged_panel", "panel_surface", "static_sign"};
    for (int n = 0; n < kNodes; ++n) {
        for (int j = 0; j < 3; ++j) {
            const int v = n * 3 + j;
            m.indices[size_t(v)] = uint32_t(v);
            m.vertices[size_t(v)].pos = {float(n * 2 + (j == 1)), float(j == 2), 0};
            m.vertices[size_t(v)].nrm = {0, 0, 1};
        }
        Primitive p;
        p.firstIndex = uint32_t(n * 3);
        p.indexCount = 3;
        p.material = 0;
        MeshDef mesh;
        mesh.name = names[n];
        mesh.primitives.push_back(p);
        m.meshes.push_back(mesh);
        Node node;
        node.name = names[n];
        node.mesh = n;
        node.parent = n == 3 ? 2 : -1;
        node.runtimeMutable = n == 2;
        m.nodes.push_back(node);
    }
    m.finalize();
    auto oldParent = m.nodes[3].parent;
    const auto oldMatrices = m.bindWorld;
    m.batching.mergeRigid = true;
    prepareModelUpload(m);
    CHECK(m.uploadPrepared);
    CHECK(m.nodes.size() == 6);
    CHECK(m.nodes.back().name == "merged");
    CHECK(m.nodes[2].name == "hinged_panel");
    CHECK(m.nodes[3].name == "panel_surface");
    CHECK(m.nodes[3].parent == oldParent);
    for (int n : {2, 3}) {
        CHECK(m.nodes[size_t(n)].mesh >= 0);
        CHECK(m.meshes[size_t(m.nodes[size_t(n)].mesh)].primitives.size() == 1);
        for (int row = 0; row < 16; ++row)
            CHECK(m.bindWorld[size_t(n)].m[row] == oldMatrices[size_t(n)].m[row]);
    }

    Model skip;
    skip.materials.resize(1);
    skip.batching.mergeRigid = false;
    skip.nodes.resize(1);
    skip.nodes[0].name = "untouched";
    skip.vertices.resize(3);
    skip.indices = {0, 1, 2};
    skip.meshes.resize(1);
    skip.meshes[0].primitives.resize(1);
    skip.meshes[0].primitives[0].indexCount = 3;
    skip.nodes[0].mesh = 0;
    skip.finalize();
    prepareModelUpload(skip);
    CHECK(skip.nodes.size() == 1);
    // The same policy must load from a model-sidecar in any game or editor.
    const std::string obj =
        "o shell\n"
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "f 1 2 3\n";
    const auto read = [&](const std::string& path, std::string& out) {
        if (path == "fixture.obj") { out = obj; return true; }
        if (path == "fixture.obj.batching.json") {
            out = R"({"mergeRigid":true,"independentShadowLod":true,"maxExtent":12,"preserveNodes":["shell"],"mutableNodes":["shell"]})";
            return true;
        }
        return false;
    };
    auto imported = loadModel("fixture.obj", read);
    CHECK(imported->batching.mergeRigid);
    CHECK(imported->batching.independentShadowLod);
    CHECK(imported->batching.maxExtent == 12.0f);
    CHECK(imported->batching.preserveNodes.size() == 1);
    CHECK(imported->findNode("shell") >= 0);
    CHECK(imported->nodes[size_t(imported->findNode("shell"))].runtimeMutable);
    CHECK(!imported->uploadPrepared);
    // Distinct material objects with identical GPU state must batch together.
    // Changing an effective uniform or sampled texture must split the batch.
    Model gpuA, gpuB;
    gpuA.materials.resize(1);
    gpuB.materials.resize(1);
    CHECK(&gpuA.materials[0] != &gpuB.materials[0]);
    CHECK(equivalentGpuMaterial(gpuA, gpuA.materials[0], gpuB, gpuB.materials[0]));
    CHECK(gpuMaterialStateHash(gpuA, gpuA.materials[0]) == gpuMaterialStateHash(gpuB, gpuB.materials[0]));
    gpuB.materials[0].eyeOptics={.5f,.5f,.14f,.01f};
    CHECK(!equivalentGpuMaterial(gpuA,gpuA.materials[0],gpuB,gpuB.materials[0]));
    CHECK(gpuMaterialStateHash(gpuA,gpuA.materials[0])!=gpuMaterialStateHash(gpuB,gpuB.materials[0]));
    gpuB.materials[0].eyeOptics={};
    gpuB.materials[0].roughness = .25f;
    CHECK(!equivalentGpuMaterial(gpuA, gpuA.materials[0], gpuB, gpuB.materials[0]));
    CHECK(gpuMaterialStateHash(gpuA, gpuA.materials[0]) != gpuMaterialStateHash(gpuB, gpuB.materials[0]));
    gpuB.materials[0].roughness = gpuA.materials[0].roughness;
    gpuB.materials[0].thinTransmission=.4f;
    CHECK(!equivalentGpuMaterial(gpuA,gpuA.materials[0],gpuB,gpuB.materials[0]));
    CHECK(gpuMaterialStateHash(gpuA,gpuA.materials[0])!=gpuMaterialStateHash(gpuB,gpuB.materials[0]));
    gpuB.materials[0].thinTransmission=0;
    gpuB.materials[0].shadowDiffusion = .1f;
    CHECK(!equivalentGpuMaterial(gpuA, gpuA.materials[0], gpuB, gpuB.materials[0]));
    CHECK(gpuMaterialStateHash(gpuA, gpuA.materials[0]) != gpuMaterialStateHash(gpuB, gpuB.materials[0]));
    CHECK(!equivalentGpuMaterial(gpuA, withoutGpuPaletteFactors(gpuA.materials[0]),
                                gpuB, withoutGpuPaletteFactors(gpuB.materials[0])));
    gpuB.materials[0].shadowDiffusion = 0;
    gpuB.materials[0].sssStrength = .5f;
    CHECK(!equivalentGpuMaterial(gpuA, gpuA.materials[0], gpuB, gpuB.materials[0]));
    CHECK(gpuMaterialStateHash(gpuA, gpuA.materials[0]) != gpuMaterialStateHash(gpuB, gpuB.materials[0]));
    gpuB.materials[0].sssStrength = 0;
    gpuB.materials[0].sssColor.y = .3f;
    CHECK(!equivalentGpuMaterial(gpuA, gpuA.materials[0], gpuB, gpuB.materials[0]));
    gpuB.materials[0].sssColor.y = 1;
    gpuB.materials[0].sssThickness = .06f;
    CHECK(!equivalentGpuMaterial(gpuA, gpuA.materials[0], gpuB, gpuB.materials[0]));
    gpuB.materials[0].sssThickness = 0;
    gpuA.images.resize(1); gpuB.images.resize(1);
    gpuA.images[0].tex.id = 101;
    gpuB.images[0].tex.id = 101;
    gpuA.materials[0].baseTex = 0; gpuB.materials[0].baseTex = 0;
    CHECK(equivalentGpuMaterial(gpuA, gpuA.materials[0], gpuB, gpuB.materials[0]));
    gpuB.images[0].tex.id = 202;
    CHECK(!equivalentGpuMaterial(gpuA, gpuA.materials[0], gpuB, gpuB.materials[0]));
    gpuB.images[0].tex.id = 101;
    gpuA.materials[0].shaderGraph = std::make_shared<graph::Program>();
    gpuB.materials[0].shaderGraph = std::make_shared<graph::Program>();
    gpuA.materials[0].shaderGraph = std::make_shared<const graph::Program>(*gpuA.materials[0].shaderGraph);
    CHECK(equivalentGpuMaterial(gpuA, gpuA.materials[0], gpuB, gpuB.materials[0]));
    // Different rigid models automatically share one GPU resource, preserving
    // independent node transforms, local index data and model selection IDs.
    MockDevice dev;
    auto modelA = pooledFixture(0);
    auto modelB = pooledFixture(12);
    CHECK(uploadModel(dev, modelA, true));
    CHECK(uploadModel(dev, modelB, true));
    CHECK(modelA.geometryPoolPage && modelB.geometryPoolPage);
    CHECK(modelA.vb == modelB.vb && modelA.ib == modelB.ib);
    CHECK(modelA.vb == modelA.ib);
    CHECK(modelA.gpuFirstIndex != modelB.gpuFirstIndex);
    CHECK(modelA.gpuBaseVertex != modelB.gpuBaseVertex);
    CHECK(dev.created == 1);
    {
        const auto stats = geometryPoolStats(dev);
        CHECK(stats.pages == 1 && stats.liveModels == 2);
        // Small shared pages avoid reserving 8 MiB for just a few rigid meshes.
        CHECK(stats.allocatedBytes == 2u * 1024u * 1024u);
        CHECK(stats.occupiedBytes > 0 && stats.occupiedBytes < stats.allocatedBytes);
    }
    const auto& bytes = dev.buffers.at(modelA.vb.id);
    for (auto* model : {&modelA, &modelB}) {
        const uint32_t first = model->gpuFirstIndex;
        for (size_t i = 0; i < model->indices.size(); ++i) {
            uint32_t stored = 0;
            std::memcpy(&stored, bytes.data() + (size_t(first) + i) * sizeof(uint32_t), sizeof(stored));
            CHECK(stored == model->indices[i]);
        }
        for (size_t i = 0; i < model->vertices.size(); ++i) {
            ModelVertex stored{};
            std::memcpy(&stored, bytes.data() + (size_t(model->gpuBaseVertex) + i) * sizeof(ModelVertex), sizeof(stored));
            CHECK(stored.pos.x == model->vertices[i].pos.x);
        }
    }
    releaseModel(dev, modelA);
    CHECK(dev.destroyed == 0 && geometryPoolStats(dev).liveModels == 1);
    releaseModel(dev, modelB);
    CHECK(dev.destroyed == 1 && dev.buffers.empty());
    CHECK(geometryPoolStats(dev).pages == 0);
    // A backend without MDI (GLES3/WebGL2) must retain the legacy dedicated
    // buffers automatically and must not allocate a shared geometry page.
    MockDevice legacy;
    legacy.indirectSupported = false;
    auto legacyModel = pooledFixture(0);
    CHECK(uploadModel(legacy, legacyModel, true));
    CHECK(!legacyModel.geometryPoolPage);
    CHECK(legacyModel.vb.valid() && legacyModel.ib.valid() &&
          !(legacyModel.vb == legacyModel.ib));
    CHECK(geometryPoolStats(legacy).pages == 0);
    releaseModel(legacy, legacyModel);
    CHECK(legacy.destroyed == 2);


    // Persistent scene staging: stable handles, generation invalidation,
    // unchanged-record elision, coalesced uploads, and full GPU-slot refresh.
    {
        PersistentGpuScene scene;
        GpuSceneRecord record{};
        record.geometry = 7;
        auto a = scene.insert(record);
        auto b = scene.insert(record);
        CHECK(scene.liveCount() == 2 && scene.contains(a) && scene.contains(b));
        std::vector<PersistentGpuScene::DirtyRange> ranges;
        auto flush = [&](PersistentGpuScene::DirtyRange range, const GpuSceneRecord* values) {
            ranges.push_back(range);
            CHECK(values[0].geometry == 7 || values[0].geometry == 0);
        };
        scene.flush(flush);
        CHECK(ranges.size() == 1 && ranges[0].first == 0 && ranges[0].count == 2);
        ranges.clear();
        CHECK(scene.update(a, record));
        scene.flush(flush);
        CHECK(ranges.empty());
        record.geometry = 9;
        CHECK(scene.update(a, record));
        scene.flush([&](PersistentGpuScene::DirtyRange range, const GpuSceneRecord* values) {
            ranges.push_back(range);
            CHECK(values[0].geometry == 9);
        });
        CHECK(ranges.size() == 1 && ranges[0].first == 0 && ranges[0].count == 1);
        ranges.clear();
        CHECK(scene.erase(a) && !scene.contains(a) && !scene.update(a, record));
        auto replacement = scene.insert(record);
        CHECK(replacement.slot == a.slot && replacement.generation != a.generation);
        scene.flush([&](PersistentGpuScene::DirtyRange range, const GpuSceneRecord*) {
            ranges.push_back(range);
        });
        CHECK(ranges.size() == 1 && ranges[0].count == 1);
        ranges.clear();
        scene.invalidateAll();
        scene.flush([&](PersistentGpuScene::DirtyRange range, const GpuSceneRecord*) {
            ranges.push_back(range);
        });
        CHECK(ranges.size() == 1 && ranges[0].count == 2);
    }

    // Triple-slot GPU staging: first use uploads all slots; unchanged frame
    // revisits upload nothing; a changed object propagates to each slot.
    {
        MockDevice device;
        PersistentGpuScene scene;
        GpuSceneRecord record{};
        record.geometry = 11;
        const auto handle = scene.insert(record);
        PersistentGpuSceneUpload upload(device);
        for (uint64_t frame = 0; frame < 3; ++frame) {
            const auto stats = upload.sync(scene, frame);
            CHECK(stats.records == 1 && stats.ranges == 1);
        }
        CHECK(device.created == 3);
        CHECK(upload.sync(scene, 3).records == 0);
        record.geometry = 12;
        CHECK(scene.update(handle, record));
        for (uint64_t frame = 3; frame < 6; ++frame) {
            const auto stats = upload.sync(scene, frame);
            CHECK(stats.records == 1);
            uint32_t stored = 0;
            const auto& bytes = device.buffers.at(upload.buffer(frame).id);
            std::memcpy(&stored, bytes.data() + offsetof(GpuSceneRecord, geometry), sizeof(stored));
            CHECK(stored == 12);
        }
        upload.release();
        CHECK(device.destroyed == 3);
    }

    // GPU-authored commands have the Metal indirect ABI and are strictly
    // optional on non-Metal devices. No compute dispatch on the mock device.
    {
        static_assert(sizeof(GpuPrimitiveDrawRecord) == 176);
        static_assert(offsetof(GpuPrimitiveDrawRecord, command) == 96);
        static_assert(offsetof(GpuPrimitiveDrawRecord, lodAlternate) == 128);
        static_assert(offsetof(GpuPrimitiveDrawRecord, lodMaxPixels) == 148);
        static_assert(offsetof(GpuPrimitiveDrawRecord, lodEnabled) == 156);
        static_assert(offsetof(GpuPrimitiveDrawRecord, lodPadding) == 160);
        static_assert(sizeof(GpuCompactedDrawRecord) == 24);
        static_assert(sizeof(aiverse::gfx::DrawIndexedIndirectCommand) == 20);
        MockDevice mock;
        GpuSceneDrawCommands generator;
        std::vector<GpuPrimitiveDrawRecord> records(1);
        records[0].command.indexCount = 24;
        records[0].command.instanceCount = 1;
        records[0].enabled = 1;
        CHECK(!generator.prepare(mock, records, mat4::identity(), 1));
        CHECK(!generator.output(1).valid());
        generator.release(mock);
    }

    // Fake Metal compute does not execute shaders but exercises CPU-side GPU
    // buffer allocation and command slot lifetime without requiring a Mac.
    {
        MockDevice mock;
        mock.deviceInfo.backend = aiverse::gfx::Backend::Metal;
        mock.fakeMetalCompute = true;
        GpuSceneDrawCommands generator;
        std::vector<GpuPrimitiveDrawRecord> records(2);
        records[0].enabled = records[1].enabled = 1;
        records[0].command.indexCount = 12;
        records[1].command.indexCount = 36;
        size_t first = SIZE_MAX, second = SIZE_MAX, reused = SIZE_MAX;
        CHECK(generator.prepare(mock, records, mat4::identity(), 1, &first));
        CHECK(first == 0);
        const auto firstBuffer = generator.output(1, first);
        CHECK(firstBuffer.valid());
        CHECK(generator.uploadStats(1, first).records == 2);
        records[1].command.indexCount = 48;
        CHECK(generator.prepare(mock, records, mat4::identity(), 1, &second));
        CHECK(second == 1);
        const auto secondBuffer = generator.output(1, second);
        CHECK(secondBuffer.valid() && !(secondBuffer == firstBuffer));
        CHECK(generator.output(1, first) == firstBuffer);
        CHECK(generator.uploadStats(1, second).records == 2);
        // Recycling a frame slot after three frames reuses the first
        // submission's buffers and uploads only the changed records.
        records[1].command.indexCount = 36;
        CHECK(generator.prepare(mock, records, mat4::identity(), 4, &reused));
        CHECK(reused == 0 && generator.output(4, reused) == firstBuffer);
        CHECK(generator.uploadStats(4, reused).records == 0);
        CHECK(!generator.output(1, first).valid()); // old frame epoch retired
        generator.release(mock);
        CHECK(mock.buffers.empty());
    }

    // GPU-indexed instance records retain the same 144-byte layout as the
    // original U0 Inst uniform; Metal switches address spaces, not layout.
    // The GPU shader macro must select device memory and instance_id, while
    // the legacy permutation must still select the original MU0 block.
    {
        CHECK(36u * sizeof(float) == 144u);
        CHECK(std::strstr(shaders::kModelMSL, "GPU_INSTANCE_BUFFER") != nullptr);
        CHECK(std::strstr(shaders::kModelMSL,
            "#define INSTANCE gpuInstances[iid]") != nullptr);
        CHECK(std::strstr(shaders::kModelMSL,
            "#define INSTANCE u.inst[iid]") != nullptr);
    }

    // Adaptive ICB is a three-way policy, not a disguised CPU MDI fallback:
    // tiny groups select direct instancing before running GPU compute.
    {
        CHECK(gpuIcbMinimumCommands(67, 8, true) == 16);
        CHECK(gpuIcbMinimumCommands(255, 2, true) == 16);
        CHECK(gpuIcbMinimumCommands(256, 2, true) == 8);
        CHECK(gpuIcbMinimumCommands(512, 20, true) == 20);
        CHECK(gpuIcbMinimumCommands(67, 8, false) == 8);
        CHECK(gpuIcbMinimumCommands(67, 1, false) == 2);
        CHECK(gpuIcbShouldFallbackToInstancing(true, 0));
        CHECK(!gpuIcbShouldFallbackToInstancing(true, 1));
        CHECK(!gpuIcbShouldFallbackToInstancing(false, 0));
        // Per-group decisions are based on compatible primitive runs; material
        // object identity is deliberately absent from these inputs.
        CHECK(chooseDrawGroupMode(0, 0, true, true, true, 2, 2) ==
              DrawGroupMode::Direct);
        CHECK(chooseDrawGroupMode(1, 1, true, true, true, 2, 2) ==
              DrawGroupMode::Direct);
        CHECK(chooseDrawGroupMode(8, 1, true, true, true, 2, 2) ==
              DrawGroupMode::Instancing);
        CHECK(chooseDrawGroupMode(8, 4, true, true, false, 4, 8) ==
              DrawGroupMode::CpuMultiDraw);
        CHECK(chooseDrawGroupMode(8, 4, true, true, true, 4, 8) ==
              DrawGroupMode::CpuMultiDraw);
        CHECK(chooseDrawGroupMode(16, 8, true, true, true, 4, 8) ==
              DrawGroupMode::GpuIcb);
        CHECK(chooseDrawGroupMode(8, 4, true, false, false, 4, 8) ==
              DrawGroupMode::Instancing);
        CHECK(shouldUseCpuMultiDraw(12, 2, 2));
        CHECK(!shouldUseCpuMultiDraw(12, 1, 2));
        CHECK(shouldUseGpuIcb(16, 8, 8));
        CHECK(!shouldUseGpuIcb(16, 1, 8));
        // A conservative 0.5% noise deadband prefers baseline instancing.
        CHECK(chooseSceneBatchMode({11.801, 11.953, 11.977}, 3) == 0);
        CHECK(chooseSceneBatchMode({12.100, 11.800, 11.900}, 3) == 1);
        CHECK(chooseSceneBatchMode({12.100, 12.020, 11.700}, 3) == 2);
        CHECK(chooseSceneBatchMode({12.100, 12.070, 10.000}, 2) == 0);
    }

    // Model content revisions must distinguish in-place edits even if an
    // allocator reuses the same address or the GPU reuses the same handle ID.
    {
        Model model;
        const uint64_t initial = model.geometryRevision;
        model.finalize();
        CHECK(model.geometryRevision != initial);
        const uint64_t afterFinalize = model.geometryRevision;
        model.markGeometryChanged();
        CHECK(model.geometryRevision != afterFinalize);
        const uint64_t beforeRelease = model.geometryRevision;
        MockDevice dev;
        releaseModel(dev, model);
        CHECK(model.geometryRevision != beforeRelease);
    }

    // Stage103 post-main-merge regression: production GPU admission must
    // preserve the mutable CPU-vertex opt-out from the PHYSIS/Terra branch
    // alongside main's per-asset rigid-LOD admission policy.
    {
        MockDevice dev;
        Model model;
        model.vertices.resize(3);
        model.indices = {0, 1, 2};
        model.batching.allowGeometryPool = false;
        const auto mutableAdmission = geometryPoolAdmission(dev, model);
        CHECK(!mutableAdmission.eligible());
        if (globalGeometryPoolingEnabled())
            CHECK(mutableAdmission.reason == GeometryPoolAdmissionReason::DedicatedAsset);
        // Dedicated assets must never fall through to shared geometry pages.
        model.batching.allowGeometryPool = true;
        model.batching.geometryPoolPolicy = Model::Batching::GeometryPoolPolicy::Dedicated;
        const auto dedicatedAdmission = geometryPoolAdmission(dev, model);
        CHECK(!dedicatedAdmission.eligible());
        if (globalGeometryPoolingEnabled())
            CHECK(dedicatedAdmission.reason == GeometryPoolAdmissionReason::DedicatedAsset);
    }

    // CPU-side cross-frame rigid cache is deliberately narrower than an
    // immediate ModelDraw: visibility and material overrides stay live.
    {
        Model model;
        model.nodes.resize(1);
        model.meshes.resize(1);
        ModelDraw draw;
        draw.model = &model;
        draw.persistentId = 41;
        draw.flags = kDrawStatic;
        // The CPU cache requires bindWorld matrices from finalized assets.
        // Imported models always call finalize(); this synthetic fixture must
        // do so too. Missing bind matrices are correctly ineligible.
        CHECK(!staticModelCacheCandidate(draw, false));
        model.finalize();
        CHECK(model.bindWorld.size() == model.nodes.size());
        CHECK(staticModelCacheCandidate(draw, false));
        draw.vertexOverride = aiverse::gfx::BufferHandle{17};
        draw.vertexOverrideCount = 3;
        CHECK(!staticModelCacheCandidate(draw, false));
        draw.staticPacketRevision = 1;
        CHECK(!staticRenderPacketCandidate(draw, false, false, false));
        draw.vertexOverride = {};
        draw.vertexOverrideCount = 0;
        draw.staticPacketRevision = 0;
        CHECK(staticModelCacheCandidate(draw, false));
        draw.materials = &model.materials;
        std::vector<int> nodeOverrides(1, -1);
        draw.nodeMaterial = &nodeOverrides;
        CHECK(staticModelCacheCandidate(draw, false));
        CHECK(!staticModelCacheCandidate(draw, true));

        StaticModelCache cache;
        cache.model = &model;
        cache.world = draw.world;
        cache.transforms.resize(model.nodes.size());
        cache.primitiveOffsets.resize(model.meshes.size());
        CHECK(staticModelCacheGeometryCompatible(cache, draw, false));
        CHECK(!staticModelCacheGeometryCompatible(cache, draw, true));
        // Runtime vertex parameters and filtered-material baker draws are
        // not safe to reuse via static transforms or packet blueprints.
        draw.useVertexParameters = true;
        CHECK(!staticModelCacheCandidate(draw, false));
        CHECK(!staticModelCacheGeometryCompatible(cache, draw, false));
        draw.useVertexParameters = false;
        draw.primitiveMaterialFilter = 0;
        CHECK(!staticModelCacheCandidate(draw, false));
        CHECK(!staticModelCacheGeometryCompatible(cache, draw, false));
        draw.primitiveMaterialFilter = -1;
        CHECK(staticModelCacheCandidate(draw, false));
        CHECK(staticModelCacheGeometryCompatible(cache, draw, false));
        draw.world.m[12] += 2.0f;
        CHECK(!staticModelCacheGeometryCompatible(cache, draw, false));
        draw.world = cache.world;
        draw.persistentId = 0;
        CHECK(!staticModelCacheCandidate(draw, false));
        draw.persistentId = 41;
        draw.flags = 0;
        CHECK(!staticModelCacheCandidate(draw, false));
        draw.flags = kDrawStatic;
        std::vector<mat4> animatedNodes(1);
        draw.nodeWorld = &animatedNodes;
        CHECK(!staticModelCacheCandidate(draw, false));
        CHECK(!staticModelCacheGeometryCompatible(cache, draw, false));
        draw.nodeWorld = nullptr;
        model.nodes[0].runtimeMutable = true;
        CHECK(!staticModelCacheCandidate(draw, false));
        model.nodes[0].runtimeMutable = false;
        // Authored clips on disk do not mean this *instance* is animated.
        // Bind-pose static instances remain eligible; an active pose does not.
        model.clips.emplace_back();
        CHECK(staticModelCacheCandidate(draw, false));
        CHECK(staticModelCacheGeometryCompatible(cache, draw, false));
        std::vector<NodePose> activePose(1);
        draw.pose = &activePose;
        CHECK(!staticModelCacheCandidate(draw, false));
        CHECK(!staticModelCacheGeometryCompatible(cache, draw, false));
        draw.pose = nullptr;
        model.clips.clear();
        model.geometry = std::make_shared<GeometryData>();
        CHECK(!staticModelCacheCandidate(draw, false));
    }

    // Persistent packet blueprints must never reuse camera/LOD/GI/pose
    // assumptions or silently ignore caller-visible appearance revisions.
    {
        Model model;
        model.nodes.resize(1);
        model.nodes[0].mesh = 0;
        model.meshes.resize(1);
        model.materials.resize(1);
        ModelDraw draw;
        draw.model = &model;
        draw.persistentId = 42;
        draw.flags = kDrawStatic;
        CHECK(!staticRenderPacketCandidate(draw, false, false, false));
        draw.staticPacketRevision = 1;
        CHECK(!staticRenderPacketCandidate(draw, false, false, false)); // missing bindWorld
        model.finalize();
        CHECK(staticRenderPacketCandidate(draw, false, false, false));
        CHECK(!staticRenderPacketCandidate(draw, true, false, false));
        CHECK(!staticRenderPacketCandidate(draw, false, true, false));
        CHECK(!staticRenderPacketCandidate(draw, false, false, true));
        // Review 2026-11-21: GPU selection packets are per-frame; a draw
        // carrying any of them must never replay a recorded CPU packet.
        draw.gpuIndexedPrimitives.emplace_back();
        CHECK(!staticRenderPacketCandidate(draw, false, false, false));
        draw.gpuIndexedPrimitives.clear();
        draw.gpuShadowPrimitives.emplace_back();
        CHECK(!staticRenderPacketCandidate(draw, false, false, false));
        draw.gpuShadowPrimitives.clear();
        CHECK(staticRenderPacketCandidate(draw, false, false, false));

        ModelDraw snapshot = draw;
        CHECK(equivalentStaticRenderPacketDraw(snapshot, draw));
        draw.staticPacketRevision++;
        CHECK(!equivalentStaticRenderPacketDraw(snapshot, draw));
        draw.staticPacketRevision = snapshot.staticPacketRevision;
        draw.outline.w = 0.03f;
        CHECK(!equivalentStaticRenderPacketDraw(snapshot, draw));
        draw.outline = snapshot.outline;
        draw.tint.w = 0.5f;
        CHECK(!equivalentStaticRenderPacketDraw(snapshot, draw));
        draw.tint = snapshot.tint;
        draw.world.m[12] = 15.f;
        CHECK(!equivalentStaticRenderPacketDraw(snapshot, draw));
        draw.world = snapshot.world;
        CHECK(equivalentStaticRenderPacketDraw(snapshot, draw));

        draw.materials = &model.materials;
        CHECK(staticRenderPacketCandidate(draw, false, false, false));
        const auto savedMaterials = model.materials;
        const auto savedHashes = staticPacketMaterialHashes(model, savedMaterials);
        CHECK(staticPacketMaterialStateMatches(model, model.materials,
                                                savedMaterials, savedHashes));
        model.materials[0].metallic = .85f; // pointer-stable native mutation
        CHECK(!staticPacketMaterialStateMatches(model, model.materials,
                                                 savedMaterials, savedHashes));
        model.materials[0] = savedMaterials[0];
        CHECK(staticPacketMaterialStateMatches(model, model.materials,
                                                savedMaterials, savedHashes));
        model.materials.emplace_back(); // reallocated material vector
        CHECK(!staticPacketMaterialStateMatches(model, model.materials,
                                                 savedMaterials, savedHashes));
        model.materials.pop_back();
        draw.materials = nullptr;
        std::vector<int> nodeMaterials(1, 0);
        draw.nodeMaterial = &nodeMaterials;
        // Per-node material overrides are live packet inputs, not a blanket
        // cache exclusion. Renderer3D validates hasNodeMaterialOverride and
        // the exact nodeMaterialSnapshot on every packet replay.
        CHECK(staticRenderPacketCandidate(draw, false, false, false));
        nodeMaterials[0] = -1; // switching overrides remains a runtime input
        CHECK(staticRenderPacketCandidate(draw, false, false, false));
        draw.nodeMaterial = nullptr;
        model.meshes[0].lods.push_back({0, 10.f});
        CHECK(staticRenderPacketCandidate(draw, false, false, false));
        model.meshes[0].lods.clear();
        model.meshes[0].deformable = true;
        CHECK(!staticRenderPacketCandidate(draw, false, false, false));
        model.meshes[0].deformable = false;
        model.nodes[0].runtimeMutable = true;
        CHECK(!staticRenderPacketCandidate(draw, false, false, false));
        model.nodes[0].runtimeMutable = false;
        model.nodes[0].skin = 0;
        CHECK(!staticRenderPacketCandidate(draw, false, false, false));
    }

    // LOD-aware packet replay must invalidate on a changed selected mesh,
    // and bypass continuously varying crossfade frames.
    {
        const std::vector<ModelLod> levels{{1, 32.f}, {2, 8.f}};
        const auto near = selectModelLod(0, levels, 150.f, .15f);
        const auto mid = selectModelLod(0, levels, 20.f, .15f);
        const auto far = selectModelLod(0, levels, 2.f, .15f);
        CHECK(near.mesh == 0 && near.incoming < 0);
        CHECK(mid.mesh == 1 && mid.incoming < 0);
        CHECK(far.mesh == 2 && far.incoming < 0);
        const auto transition = selectModelLod(0, levels, 32.f, .15f);
        CHECK(transition.incoming >= 0 && transition.progress > 0.f);
    }

    // Generic ECS presentation adapter: only integer asset identity and
    // revision live in ECS; the renderer receives a current fixed->float world
    // matrix, a stable generation-aware host key, and a live model reference.
    {
        aiverse::World world;
        const auto first = world.create();
        const auto missingTransform = world.create();
        aiverse::render::EcsStaticModelRender component;
        component.assetKey = 7;
        component.appearanceRevision = 9;
        component.flags = kDrawNoCastShadow;
        world.add<aiverse::render::EcsStaticModelRender>(first, component);
        world.add<aiverse::render::EcsStaticModelRender>(
            missingTransform, component);
        aiverse::WorldTransform xf;
        xf.affine.m[3] = aiverse::Fixed::fromInt(7);
        world.add<aiverse::WorldTransform>(first, xf);
        Model model;
        size_t draws = 0;
        uint64_t lastId = 0;
        const auto resolve = [&model](uint32_t asset) -> const Model* {
            return asset == 7 ? &model : nullptr;
        };
        const auto stableId = [](aiverse::Entity entity) {
            return entity.packed() + uint64_t{1};
        };
        auto submit = [&](aiverse::Entity e, const ModelDraw& d) {
            CHECK(e == first);
            CHECK(d.model == &model);
            CHECK(d.staticPacketRevision == 9);
            CHECK(d.persistentId == stableId(first));
            CHECK((d.flags & kDrawStatic) != 0);
            CHECK((d.flags & kDrawNoCastShadow) != 0);
            CHECK(d.world.at(0,3) == 7.f);
            lastId = d.persistentId;
            ++draws;
        };
        CHECK(aiverse::render::visitEcsStaticModels(
            world, resolve, stableId, submit) == 1);
        CHECK(draws == 1 && lastId);
        world.get<aiverse::render::EcsStaticModelRender>(first).visible = false;
        CHECK(aiverse::render::visitEcsStaticModels(
            world, resolve, stableId, submit) == 0);
        world.get<aiverse::render::EcsStaticModelRender>(first).visible = true;
        world.get<aiverse::render::EcsStaticModelRender>(first).appearanceRevision = 0;
        CHECK(aiverse::render::visitEcsStaticModels(
            world, resolve, stableId, submit) == 0);
        // A recycled ECS index gains a generation, so the host's stable
        // namespaced mapping can distinguish it from the old render packet.
        world.destroy(first);
        const auto recycled = world.create();
        CHECK(recycled.index == first.index && recycled.gen != first.gen);
        CHECK(stableId(recycled) != lastId);
    }

    std::puts("engine model batching policy: PASS");
    return 0;
}
