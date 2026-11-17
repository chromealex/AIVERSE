// Engine regression: static batching preserves runtime-mutable subtrees.
// This is intentionally independent from any particular project/genre.
#include "engine/render/Model.h"
#include "engine/render/ModelMerge.h"
#include "engine/render/GpuMaterialState.h"
#include "engine/render/GlobalGeometryPool.h"
#include "engine/render/PersistentGpuScene.h"
#include "engine/render/PersistentGpuSceneUpload.h"
#include "engine/render/GpuSceneDrawCommands.h"
#include "engine/render/GpuIcbPolicy.h"
#include "engine/render/ModelShaders.h"
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
    void destroyBuffer(aiverse::gfx::BufferHandle h) override {
        CHECK(buffers.erase(h.id) == 1);
        ++destroyed;
    }
    aiverse::gfx::TextureHandle createTexture(const aiverse::gfx::TextureDesc&) override { return {100}; }
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


int main() {
    verifyVehiclePartsRemainIndependent();
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
            out = R"({"mergeRigid":true,"maxExtent":12,"preserveNodes":["shell"],"mutableNodes":["shell"]})";
            return true;
        }
        return false;
    };
    auto imported = loadModel("fixture.obj", read);
    CHECK(imported->batching.mergeRigid);
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
    gpuB.materials[0].roughness = .25f;
    CHECK(!equivalentGpuMaterial(gpuA, gpuA.materials[0], gpuB, gpuB.materials[0]));
    CHECK(gpuMaterialStateHash(gpuA, gpuA.materials[0]) != gpuMaterialStateHash(gpuB, gpuB.materials[0]));
    gpuB.materials[0].roughness = gpuA.materials[0].roughness;
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

    std::puts("engine model batching policy: PASS");
    return 0;
}
