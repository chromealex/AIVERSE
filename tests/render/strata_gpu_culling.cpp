// Strata GPU compute dispatch plumbing: recording-RHI only, NOT GPU parity.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataGpuCulling.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>
#include <string>
#include <vector>
using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

class RecordingComputeDevice final : public gfx::Device {
public:
    gfx::DeviceInfo deviceInfo{gfx::Backend::Metal, true, false, "Strata test"};
    std::map<uint32_t, std::vector<uint8_t>> buffers;
    gfx::BufferHandle bound[8]{};
    GpuSelectionUniforms lastUniforms{};
    std::string shaderText, error;
    std::vector<std::string> shaderSources;
    uint32_t next = 1, computeCalls = 0, dispatchCount = 0;
    uint32_t drawCalls = 0;
    bool supported = true, active = false;
    uint64_t completed = 100, currentFrame = 101;
    struct PendingRead {
        BufferReadFn callback;
        std::vector<uint8_t> data;
    };
    std::vector<PendingRead> reads;
    uint64_t frameSerial() const override { return currentFrame; }
    bool readBufferAsync(gfx::BufferHandle handle, size_t size,
                         BufferReadFn callback) override {
        auto it = buffers.find(handle.id);
        if (it == buffers.end() || !size || size > 65536 ||
            size > it->second.size() || !callback) return false;
        PendingRead pending;
        pending.callback = std::move(callback);
        pending.data.assign(it->second.begin(),
                            it->second.begin() + size);
        reads.push_back(std::move(pending));
        return true;
    }
    void flushReads() {
        auto pending = std::move(reads);
        reads.clear();
        for (auto& read : pending)
            read.callback(std::move(read.data));
    }
    const gfx::DeviceInfo& info() const override { return deviceInfo; }
    const std::string& lastError() const override { return error; }
    bool supportsCompute() const override { return supported; }
    uint64_t completedFrameSerial() const override { return completed; }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& d) override {
        if (!d.size || d.size > 4 * 1024 * 1024) return {};
        const auto id = next++;
        auto& dest = buffers[id];
        dest.assign(d.size, uint8_t(0));
        if (d.data) std::memcpy(dest.data(), d.data, d.size);
        return {id};
    }
    void updateBuffer(gfx::BufferHandle h, const void* data, size_t size,
                      size_t offset = 0) override {
        auto& b = buffers.at(h.id);
        assert(data && offset <= b.size() && size <= b.size() - offset);
        std::memcpy(b.data() + offset, data, size);
    }
    void destroyBuffer(gfx::BufferHandle h) override { buffers.erase(h.id); }
    gfx::ComputeHandle createCompute(const gfx::ComputeDesc& d) override {
        if (deviceInfo.backend==gfx::Backend::Metal) {
            if (!d.msl || !std::strstr(d.msl, "kernel void cs_main")) return {};
            shaderText=d.msl;
        } else if (deviceInfo.backend==gfx::Backend::Vulkan) {
            if (!d.glsl || !std::strstr(d.glsl, "layout(local_size_x=64") ||
                !std::strstr(d.glsl, "void main()"))return {};
            shaderText=d.glsl;
        } else return {};
        shaderSources.push_back(shaderText);
        ++computeCalls;
        return {next++};
    }
    void destroyCompute(gfx::ComputeHandle) override {}
    bool beginCompute(gfx::ComputeHandle h) override {
        if (!supported || !h.valid() || active) return false;
        active = true; return true;
    }
    void setComputeBuffer(int index, gfx::BufferHandle h) override {
        assert(active && index >= 0 && index < 8 && h.valid());
        bound[index] = h;
    }
    void setComputeUniforms(const void* data, size_t size) override {
        assert(active && data && size>0 && size<=128);
        // Clear and safety kernels have their own 4/8/28-byte uniforms.
        // Only selection fills GpuSelectionUniforms (112 bytes).
        if(size==sizeof(GpuSelectionUniforms))
            std::memcpy(&lastUniforms,data,size);
    }
    void dispatch(uint32_t threads, uint32_t = 1) override {
        assert(active && threads > 0);
        dispatchCount += threads;
    }
    void endCompute() override { assert(active); active = false; }
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override { return {}; }
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override {}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override { return {}; }
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override { return {}; }
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
    void draw(uint32_t,uint32_t=0) override { ++drawCalls; }
    void drawIndexed(uint32_t,uint32_t=0,int32_t=0,uint32_t=1) override {
        ++drawCalls;
    }
};

static void testGpuTemporalHistory(gfx::Backend backend) {
    RecordingComputeDevice dev;
    dev.deviceInfo.backend=backend;
    dev.currentFrame=301;
    dev.completed=300;
    GpuClusterCulling compute;
    const auto instance=makeGpuSceneInstance(
        mat4::identity(),0,19,true);
    GpuPassConfig camera{};
    camera.viewProjection=mat4::identity();
    camera.width=800;camera.height=600;
    camera.maxOutputClusters=16;
    camera.hysteresisFraction=.2f;
    camera.temporalNodeCount=3;
    camera.temporalPassId=1;
    camera.temporalRevision=41;
    GpuSelectionSubmission first{},second{};
    assert(compute.dispatch(dev,{99},{instance},camera,301,first));
    assert(first.temporalApplied && first.temporalState.valid());
    assert(dev.lastUniforms.temporalNodeCount==3);
    assert(dev.lastUniforms.temporalHasPrevious==0);
    assert(dev.lastUniforms.hysteresisFraction==.2f);
    assert(compute.temporalStats().retainedBytes==3*sizeof(uint32_t));
    assert(compute.temporalStats().histories==1);
    // No two writes to one key in the SAME RHI frame may alias its
    // previous/output resources. This second attempt is transactional.
    GpuSelectionSubmission unchanged=first;
    assert(!compute.dispatch(dev,{99},{instance},camera,301,unchanged));
    assert(unchanged.temporalState==first.temporalState);

    dev.currentFrame=302;dev.completed=301;
    assert(compute.dispatch(dev,{99},{instance},camera,302,second));
    assert(second.temporalApplied && second.temporalState!=first.temporalState);
    assert(dev.lastUniforms.temporalHasPrevious==1);
    assert(compute.temporalStats().retainedBytes==6*sizeof(uint32_t));

    // Separate camera/pass uses its OWN temporal state and cold start;
    // both can be recorded in the same frame with different GPU slots.
    auto sun=camera;sun.temporalPassId=2;
    GpuSelectionSubmission shadow{};
    assert(compute.dispatch(dev,{99},{instance},sun,302,shadow));
    assert(shadow.temporalApplied);
    assert(dev.lastUniforms.temporalHasPrevious==0);
    assert(shadow.temporalState!=second.temporalState);
    assert(compute.temporalStats().histories==2);

    // A changed scene/source/policy revision must cold-start rather than
    // trusting refinement decisions from a different asset generation.
    auto revised=camera;revised.temporalRevision=42;
    GpuSelectionSubmission fresh{};
    assert(compute.dispatch(dev,{99},{instance},revised,302,fresh));
    assert(fresh.temporalApplied);
    assert(dev.lastUniforms.temporalHasPrevious==0);
    assert(compute.temporalStats().histories==3);

    dev.currentFrame=303;dev.completed=302;
    GpuSelectionSubmission third{};
    assert(compute.dispatch(dev,{99},{instance},camera,303,third));
    assert(third.temporalState!=first.temporalState &&
           third.temporalState!=second.temporalState);
    assert(dev.lastUniforms.temporalHasPrevious==1);
    dev.currentFrame=304;dev.completed=302; // frame301 slot completed
    GpuSelectionSubmission fourth{};
    assert(compute.dispatch(dev,{99},{instance},camera,304,fourth));
    assert(fourth.temporalState==first.temporalState);
    assert(dev.lastUniforms.temporalHasPrevious==1);

    // A frame-307 attempt would overwrite frame304's history output.
    // Decline until the actual RHI completion serial confirms safety.
    dev.currentFrame=307;dev.completed=303;
    GpuSelectionSubmission blocked=fourth;
    assert(!compute.dispatch(dev,{99},{instance},camera,307,blocked));
    assert(blocked.temporalState==fourth.temporalState);
    dev.completed=304;
    assert(compute.dispatch(dev,{99},{instance},camera,307,blocked));
    assert(blocked.temporalApplied);
    assert(dev.lastUniforms.temporalHasPrevious==0); // 3 idle frames
    assert(compute.temporalStats().coldStarts>=3);
    assert(compute.temporalStats().submitted>=7);
    compute.shutdown(dev);
    assert(dev.buffers.empty());
}

static void testGpuTemporalMemoryBound() {
    RecordingComputeDevice dev;
    dev.deviceInfo.backend=gfx::Backend::Metal;
    dev.currentFrame=400;dev.completed=399;
    GpuClusterCulling compute;
    GpuPassConfig config{};
    config.width=320;config.height=240;
    config.maxOutputClusters=16;
    config.hysteresisFraction=.2f;
    config.temporalPassId=1;
    config.temporalNodeCount=262144; // 1 MiB per ring slot
    config.temporalRevision=1;
    for(uint32_t i=1;i<=32;++i){
        const auto instance=makeGpuSceneInstance(
            mat4::identity(),0,i,true);
        GpuSelectionSubmission result{};
        assert(compute.dispatch(dev,{99},{instance},config,400,result));
        assert(result.temporalApplied);
    }
    assert(compute.temporalStats().retainedBytes==
           32u*1024u*1024u);
    const auto extra=makeGpuSceneInstance(
        mat4::identity(),0,33,true);
    GpuSelectionSubmission refused{};
    assert(!compute.dispatch(dev,{99},{extra},config,400,refused));
    assert(!refused.valid());
    assert(compute.temporalStats().histories==32);
    assert(compute.temporalStats().refused>=1);
    compute.shutdown(dev);
    assert(dev.buffers.empty());
}

int main() {
    testGpuTemporalHistory(gfx::Backend::Metal);
    testGpuTemporalHistory(gfx::Backend::Vulkan);
    testGpuTemporalMemoryBound();
    RecordingComputeDevice dev;
    GpuClusterCulling compute;
    auto one = makeGpuSceneInstance(mat4::translate({2, 3, 4}), 3, 101);
    assert(one.primitiveIndex == 3 && one.instanceToken == 101);
    assert(one.enabled == 1 && one.objectToWorld[12] == 2 &&
           one.objectToWorld[13] == 3 && one.objectToWorld[14] == 4);
    auto two = makeGpuSceneInstance(mat4::identity(), 0, 102, false);
    assert(two.enabled == 0);
    GpuPassConfig pass;
    pass.viewProjection = mat4::scale({.5f, .25f, 1.f});
    pass.width = 1920; pass.height = 1080;
    pass.errorThresholdPixels = 1.5f;
    pass.maxOutputClusters = 64;
    const ViewConfig oracle = cpuReferenceView(pass, one);
    const mat4 expected = pass.viewProjection *
                          mat4::translate({2, 3, 4});
    for (int i=0;i<16;++i)
        assert(std::fabs(oracle.objectToClip.m[i] - expected.m[i]) < 1e-5f);
    assert(oracle.maxSelectedNodes == 64 && oracle.width == 1920);

    GpuSelectionSubmission result;
    // Unsupported backend/capability must decline without creating resources.
    dev.deviceInfo.backend = gfx::Backend::GLES3;
    assert(!compute.dispatch(dev, {99}, {one}, pass, 101, result));
    assert(!result.valid() && dev.buffers.empty());
    dev.deviceInfo.backend = gfx::Backend::Metal;
    dev.supported = false;
    assert(!compute.dispatch(dev, {99}, {one}, pass, 101, result));
    dev.supported = true;

    assert(compute.dispatch(dev, {99}, {one,two}, pass, 101, result));
    assert(result.valid() && result.capacity == 64);
    assert(result.frameSerial == 101 && result.submission == 0);
    assert(result.submittedInstances == 2);
    assert(dev.computeCalls == 1 && dev.dispatchCount == 2);
    assert(dev.shaderText.find("projectError") != std::string::npos);
    assert(dev.lastUniforms.width == 1920 &&
           dev.lastUniforms.height == 1080);
    assert(dev.lastUniforms.instanceCount == 2);
    assert(dev.lastUniforms.outputCapacity == 64);
    assert(dev.lastUniforms.thresholdPixels == 1.5f);
    assert(dev.lastUniforms.viewProjection[0] == .5f);
    assert(dev.buffers.count(result.ranges.id));
    assert(dev.buffers.at(result.ranges.id).size() >= 64*sizeof(GpuSelectedCluster));
    assert(dev.buffers.count(result.counters.id));
    auto nextResult = result;
    assert(compute.dispatch(dev, {99}, {one}, pass, 101, nextResult));
    assert(nextResult.valid() && nextResult.submission == 1);
    assert(nextResult.ranges != result.ranges); // two views same GPU frame
    assert(nextResult.counters != result.counters);
    assert(dev.drawCalls == 0); // no raster or indirect commands

    GpuPassConfig invalid = pass;
    invalid.width = 0;
    assert(!compute.dispatch(dev, {99}, {one}, invalid, 101, nextResult));
    assert(nextResult.submission == 1); // transactional failure
    assert(dev.computeCalls == 1);
    // Frame 104 would reuse the same triple-buffer slot while frame 101
    // remains GPU-in-flight. Reject until completion is reported.
    assert(!compute.dispatch(dev, {99}, {one}, pass, 104, nextResult));
    dev.completed = 101;
    dev.currentFrame = 104;
    assert(compute.dispatch(dev, {99}, {one}, pass, 104, nextResult));
    assert(nextResult.frameSerial == 104 && nextResult.submission == 0);
    assert(dev.drawCalls == 0);

    // Diagnostic async readback and CPU oracle comparison. The recording
    // device does NOT execute Metal shader: synthesize two result records,
    // deliberately unordered to verify key-based, not atomic-order parity.
    ModelAsset reference;
    ModelPrimitiveAsset part;
    part.mesh = 0;
    part.primitive = 0;
    part.material = 7;
    part.geometry.indices = {0, 1, 2};
    part.geometry.roots = {0};
    ClusterNode root;
    root.firstIndex = 0; root.indexCount = 3; root.materialGroup = 7;
    root.resident = true;
    part.geometry.nodes.push_back(root);
    part.geometry.bounds.push_back({{-0.2f,-0.2f,0.2f},
                                     {0.2f,0.2f,0.8f}});
    reference.primitives.push_back(part);
    auto ia = makeGpuSceneInstance(mat4::identity(), 0, 333);
    auto ib = makeGpuSceneInstance(mat4::identity(), 0, 444);
    GpuPassConfig referencePass;
    referencePass.width = 800;
    referencePass.height = 600;
    referencePass.maxOutputClusters = 16;
    GpuSelectionSubmission selection;
    assert(compute.dispatch(dev, {99}, {ia,ib},
                            referencePass, 104, selection));
    GpuSelectionCounters counts;
    counts.produced = 2;
    std::memcpy(dev.buffers.at(selection.counters.id).data(),
                &counts, sizeof(counts));
    GpuSelectedCluster recordA{};
    recordA.instanceToken = 333;
    recordA.gpuFirstIndex = 400;
    recordA.indexCount = 3;
    recordA.materialGroup = 7;
    recordA.nodeIndex = 0;
    recordA.primitiveIndex = 0;
    GpuSelectedCluster recordB = recordA;
    recordB.instanceToken = 444;
    std::memcpy(dev.buffers.at(selection.ranges.id).data(),
                &recordB, sizeof(recordB));
    std::memcpy(dev.buffers.at(selection.ranges.id).data()+sizeof(recordB),
                &recordA, sizeof(recordA));
    GpuSelectionReadback decoded;
    int callbacks = 0;
    assert(compute.requestReadback(dev, selection, [&](GpuSelectionReadback r) {
        ++callbacks;
        decoded = std::move(r);
    }));
    assert(callbacks == 0 && dev.reads.size() == 2);
    dev.flushReads();
    assert(callbacks == 1 && decoded.valid());
    assert(decoded.counters.produced == 2 && decoded.ranges.size() == 2);
    std::string error;
    assert(compareGpuSelection(reference, {ia,ib}, referencePass, 400,
                               decoded, &error));
    auto wrong = decoded;
    wrong.ranges[0].materialGroup = 123;
    assert(!compareGpuSelection(reference, {ia,ib}, referencePass, 400,
                                wrong, &error));
    assert(!error.empty());
    // A GPU-reported overflow must never pass parity or return drawable
    // ranges, even if the output prefix appears plausible.
    counts.overflow = 1;
    std::memcpy(dev.buffers.at(selection.counters.id).data(),
                &counts, sizeof(counts));
    assert(compute.requestReadback(dev, selection, [&](GpuSelectionReadback r) {
        ++callbacks;
        decoded = std::move(r);
    }));
    dev.flushReads();
    assert(callbacks == 2);
    assert(decoded.status == GpuSelectionReadback::Status::Overflow);
    assert(decoded.ranges.empty());
    assert(!compareGpuSelection(reference, {ia,ib}, referencePass, 400,
                                decoded, &error));

    // Any malformed traversal is also non-drawable, even with a seemingly
    // complete prefix and without an overflow.
    counts.overflow = 0;
    counts.invalid = 1;
    std::memcpy(dev.buffers.at(selection.counters.id).data(),
                &counts, sizeof(counts));
    assert(compute.requestReadback(dev, selection, [&](GpuSelectionReadback r) {
        ++callbacks;
        decoded = std::move(r);
    }));
    dev.flushReads();
    assert(callbacks == 3);
    assert(decoded.status == GpuSelectionReadback::Status::Invalid);
    assert(decoded.ranges.empty());
    assert(!compareGpuSelection(reference, {ia,ib}, referencePass, 400,
                                decoded, &error));

    // The 64 KiB RHI read limit means oversized requests are not a
    // partially accepted oracle (2048 selected records maximum).
    GpuPassConfig oversized = referencePass;
    oversized.maxOutputClusters = 2049;
    GpuSelectionSubmission big;
    assert(compute.dispatch(dev, {99}, {ia}, oversized, 104, big));
    assert(!compute.requestReadback(dev, big, [&](GpuSelectionReadback r) {
        ++callbacks;
        decoded = std::move(r);
    }));
    assert(callbacks == 4);
    assert(decoded.status == GpuSelectionReadback::Status::ReadbackFailed);
    assert(dev.drawCalls == 0);
    // Stage18: cumulative CPU/RHI instrumentation counts only accepted
    // dispatches; it does not report GPU execution time or physical VRAM.
    const auto computeTelemetry = compute.temporalStats();
    assert(computeTelemetry.successfulDispatches >= 2);
    assert(computeTelemetry.peakSubmissionsPerFrameSlot >= 2);
    assert(computeTelemetry.peakRetainedBytes >= computeTelemetry.retainedBytes);
    assert(computeTelemetry.peakHistories >= computeTelemetry.histories);
    assert(computeTelemetry.computeBeginAttempts >=
           computeTelemetry.computeEncodersOpened);
    assert(computeTelemetry.computeBeginAttempts ==
           computeTelemetry.computeEncodersOpened+
           computeTelemetry.computeBeginFailures);
    assert(computeTelemetry.computeDispatchAttempts ==
           computeTelemetry.computeDispatchAccepted+
           computeTelemetry.computeDispatchFailures);
    assert(computeTelemetry.computeDispatchAttempts <=
           computeTelemetry.computeEncodersOpened);
    assert(computeTelemetry.selectionDispatches >=
           computeTelemetry.successfulDispatches);
    compute.shutdown(dev);
    assert(dev.buffers.empty());

    // Stage 16.1 Vulkan uses the SAME metadata/instance/range/counter
    // contract and asynchronous parity callbacks as Metal, but supplies
    // a native GLSL450 compute shader instead of an MSL kernel.
    RecordingComputeDevice vk;
    vk.deviceInfo.backend=gfx::Backend::Vulkan;
    vk.currentFrame=201;
    vk.completed=200;
    GpuClusterCulling gpuVk;
    GpuSelectionSubmission vkResult;
    assert(gpuVk.dispatch(vk,{99},{ia,ib},referencePass,201,vkResult));
    assert(vkResult.valid() && vkResult.gpuIndirectPrepared());
    assert(vk.dispatchCount==2+16+16); // clear/select/safety GPU jobs
    assert(vk.computeCalls==3);
    assert(vk.buffers.at(vkResult.indirect.id).size()==
           16*sizeof(gfx::DrawIndexedIndirectCommand));
    assert(vk.shaderSources[0].find("gpuFirstIndex+node.firstIndex") !=
           std::string::npos);
    assert(vk.shaderSources[1].find("commands[id].indexCount=0u") !=
           std::string::npos);
    assert(vk.shaderSources[2].find("counters.overflow") !=
           std::string::npos);
    GpuSelectionCounters vkCounters{};
    vkCounters.produced=2;
    std::memcpy(vk.buffers.at(vkResult.counters.id).data(),
                &vkCounters,sizeof(vkCounters));
    std::memcpy(vk.buffers.at(vkResult.ranges.id).data(),
                &recordB,sizeof(recordB));
    std::memcpy(vk.buffers.at(vkResult.ranges.id).data()+sizeof(recordB),
                &recordA,sizeof(recordA));
    GpuSelectionReadback vulkanRead;
    bool delivered=false;
    assert(gpuVk.requestReadback(vk,vkResult,[&](GpuSelectionReadback r){
        vulkanRead=std::move(r);
        delivered=true;
    }));
    assert(!delivered);
    vk.flushReads();
    assert(delivered && vulkanRead.valid());
    assert(compareGpuSelection(reference,{ia,ib},referencePass,400,
                               vulkanRead,&error));
    // Stage16.4c: dispatch different pass cameras before their respective
    // graphics encoders. GPU pools must not reuse another camera's slot
    // in the SAME RHI frame; each dispatch retains its own projection.
    GpuPassConfig sun=referencePass;
    sun.viewProjection=mat4::scale({.5f,.6f,1.f});
    sun.width=sun.height=1024;
    GpuPassConfig reflected=referencePass;
    reflected.viewProjection=mat4::scale({-1.f,1.f,1.f});
    GpuSelectionSubmission sunOutput{},reflectionOutput{};
    assert(gpuVk.dispatch(vk,{99},{ia},sun,201,sunOutput));
    assert(sunOutput.gpuIndirectPrepared());
    assert(vk.lastUniforms.viewProjection[0]==.5f);
    assert(gpuVk.dispatch(vk,{99},{ia},reflected,201,reflectionOutput));
    assert(reflectionOutput.gpuIndirectPrepared());
    assert(vk.lastUniforms.viewProjection[0]==-1.f);
    assert(sunOutput.indirect!=reflectionOutput.indirect);
    assert(sunOutput.indirect!=vkResult.indirect);
    assert(reflectionOutput.indirect!=vkResult.indirect);
    assert(sunOutput.frameSerial==reflectionOutput.frameSerial);
    // Fail-closed if the checked RHI rejects compute encoding.
    vk.supported=false;
    GpuSelectionSubmission unchanged=vkResult;
    assert(!gpuVk.dispatch(vk,{99},{ia},referencePass,201,vkResult));
    assert(vkResult.ranges==unchanged.ranges);
    const auto vkTelemetry=gpuVk.temporalStats();
    assert(vkTelemetry.successfulDispatches==3); // rejected 4th is excluded
    assert(vkTelemetry.peakSubmissionsPerFrameSlot==3);
    // Three accepted Vulkan views, each clear/select/safety (3 encoders).
    // vk.computeCalls counts shader CREATION, not beginCompute calls.
    assert(vkTelemetry.computeBeginAttempts==9);
    assert(vkTelemetry.computeEncodersOpened==9);
    assert(vkTelemetry.computeDispatchAttempts==
           vkTelemetry.computeDispatchAccepted);
    assert(vkTelemetry.computeDispatchAttempts==9);
    assert(vkTelemetry.indirectClearDispatches==3);
    assert(vkTelemetry.selectionDispatches==3);
    assert(vkTelemetry.safetyDispatches==3);
    assert(vkTelemetry.computeGroupsRequested==
           uint64_t(vk.dispatchCount));
    gpuVk.shutdown(vk);
    assert(vk.buffers.empty());
    return 0;
}
