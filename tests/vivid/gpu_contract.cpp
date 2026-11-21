// Headless RHI-contract test: records GPU buffer uploads and dispatch commands.
// This does NOT compile Metal shaders or run GPU instructions.
#include "engine/character/vivid/GpuMorph.h"
#include "engine/character/vivid/VividComputeShaders.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::vivid;
namespace {
void check(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
class RecordingDevice final : public gfx::Device {
public:
    gfx::DeviceInfo deviceInfo{};
    bool computeEnabled = true, inFrame = false, computing = false;
    bool failAllocation = false;
    int createCalls = 0, updates = 0, dispatches = 0, completed = 0, begun = 0;
    std::uint32_t logicalThreads = 0;
    std::map<std::uint32_t, std::vector<std::uint8_t>> buffers;
    std::map<int, gfx::BufferHandle> computeSlots;
    GpuMorphUniforms lastParams{};
    std::string error;
    std::uint32_t nextId = 1;
    const gfx::DeviceInfo& info() const override { return deviceInfo; }
    const std::string& lastError() const override { return error; }
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& desc) override {
        ++createCalls;
        if (failAllocation && createCalls == 4) { error = "injected allocation failure"; return {}; }
        if (!desc.size) return {};
        const auto id = nextId++;
        auto& bytes = buffers[id];
        bytes.resize(desc.size);
        if (desc.data) std::memcpy(bytes.data(), desc.data, desc.size);
        return {id};
    }
    void updateBuffer(gfx::BufferHandle handle, const void* data, std::size_t size, std::size_t offset = 0) override {
        auto it = buffers.find(handle.id);
        if (!data || it == buffers.end() || offset > it->second.size() || size > it->second.size()-offset)
            throw std::runtime_error("invalid recording buffer update");
        std::memcpy(it->second.data()+offset, data, size);
        ++updates;
    }
    void destroyBuffer(gfx::BufferHandle handle) override { buffers.erase(handle.id); }
    gfx::TextureHandle createTexture(const gfx::TextureDesc&) override { return {}; }
    void updateTexture(gfx::TextureHandle,int,int,int,int,const void*,int) override {}
    void destroyTexture(gfx::TextureHandle) override {}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc&) override { return {}; }
    void destroyShader(gfx::ShaderHandle) override {}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&) override { return {}; }
    void destroyPipeline(gfx::PipelineHandle) override {}
    bool supportsCompute() const override { return computeEnabled; }
    gfx::ComputeHandle createCompute(const gfx::ComputeDesc& desc) override {
        check(desc.msl && desc.glsl && std::strlen(desc.msl)>100 && std::strlen(desc.glsl)>100, "missing paired shaders");
        return {1};
    }
    void destroyCompute(gfx::ComputeHandle) override {}
    bool beginCompute(gfx::ComputeHandle handle) override {
        if (!inFrame || computing || !handle.valid()) return false;
        computing = true;
        ++begun;
        return true;
    }
    void setComputeBuffer(int slot, gfx::BufferHandle handle) override {
        if (!computing || !handle.valid()) throw std::runtime_error("invalid compute slot");
        computeSlots[slot] = handle;
    }
    void setComputeUniforms(const void* source, std::size_t size) override {
        check(computing && size==sizeof(GpuMorphUniforms), "uniform ABI mismatch");
        std::memcpy(&lastParams,source,size);
    }
    void dispatch(std::uint32_t threads, std::uint32_t=1) override {
        check(computing, "dispatch without compute pass");
        logicalThreads = threads;
        ++dispatches;
    }
    void endCompute() override {
        check(computing, "endCompute without begin");
        computing = false;
        ++completed;
    }
    void beginFrame(int,int) override { inFrame = true; }
    void endFrame() override { check(!computing,"encoder left active");inFrame=false; }
    void beginPass(const gfx::PassDesc&) override {}
    void endPass() override {}
    void setViewport(const gfx::Rect&) override {}
    void setScissor(const gfx::Rect&) override {}
    void setPipeline(gfx::PipelineHandle) override {}
    void setVertexBuffer(gfx::BufferHandle,std::size_t=0) override {}
    void setIndexBuffer(gfx::BufferHandle,std::size_t=0) override {}
    void setTexture(int,gfx::TextureHandle) override {}
    void setUniforms(int,const void*,std::size_t) override {}
    void draw(std::uint32_t,std::uint32_t=0) override {}
    void drawIndexed(std::uint32_t,std::uint32_t=0,std::int32_t=0,std::uint32_t=1) override {}
};

MorphVertex vertex(float x) {
    MorphVertex result;
    result.position = {x,2,3};
    result.normal = {0,1,0};
    result.tangent = {1,0,0};
    return result;
}
SparseMorphLibrary asset() {
    MorphDelta jaw{};
    jaw.vertex=0;
    jaw.position={0.75f,0,0};
    return SparseMorphLibrary::compile(2, {{"jawOpen",{jaw}},{"blink",{}}});
}
void packing() {
    const MorphVertex input = vertex(7);
    auto packed = packMorphVertex(input);
    check(sizeof(packed)==48 && packed.position[0]==7 && packed.position[3]==0 &&
          packed.normal[1]==1 && packed.tangent[0]==1, "packed GPU ABI wrong");
    auto unpacked = unpackMorphVertex(packed);
    check(unpacked.position == input.position && unpacked.normal == input.normal &&
          unpacked.tangent == input.tangent, "GPU packing roundtrip failed");
    check(sizeof(GpuMorphUniforms)==32 && sizeof(GpuMorphLink)==8, "shader metadata ABI mismatch");
}
void dispatchValidation() {
    auto library=asset();
    std::vector<MorphVertex> vertices{vertex(0),vertex(1)};
    RecordingDevice backend;
    GpuMorphDeformer deform;
    check(deform.initialize(backend,library,vertices), "RHI init");
    const std::vector<float> weights{0.5f,0};
    check(!deform.dispatch(weights), "out-of-frame dispatch accepted");
    // The current RHI has no inFrame() query: the failed beginCompute can
    // happen after a safe weights upload, without starting a dispatch.
    const auto uploadsBeforeFrame = backend.updates;
    backend.beginFrame(1,1);
    check(deform.dispatch(weights), "valid GPU dispatch rejected");
    check(backend.logicalThreads==2 && backend.dispatches==1 && backend.completed==1,
          "compute command stream incorrect");
    check(backend.computeSlots.size()==6 && backend.lastParams.vertices==2 &&
          backend.lastParams.targets==2 && backend.lastParams.deltas==1 &&
          backend.lastParams.links==1 && backend.lastParams.normalizeDirections==1,
          "uniform/slot binding mismatch");
    check(backend.updates==uploadsBeforeFrame+1, "valid dispatch missed the shape weights upload");
    const auto count=backend.updates;
    std::vector<float> invalid{std::numeric_limits<float>::quiet_NaN(),0};
    check(!deform.dispatch(invalid), "NaN weight accepted");
    check(backend.updates==count, "bad weight uploaded");
    check(!deform.dispatch(std::span<const float>{}), "missing weights accepted");
    check(backend.updates==count, "wrong count uploaded");
    check(deform.dispatch(weights,false), "unnormalized dispatch failed");
    check(backend.lastParams.normalizeDirections==0, "normalization mode ignored");
    backend.endFrame();
    deform.release();
    check(backend.buffers.empty() && !deform.ready(), "GPU buffers leaked");
}
void allocationFailure() {
    auto library=asset();
    std::vector<MorphVertex> vertices{vertex(0),vertex(1)};
    RecordingDevice backend;
    GpuMorphDeformer deform;
    backend.failAllocation=true;
    check(!deform.initialize(backend,library,vertices), "injected allocation failure ignored");
    check(backend.buffers.empty() && !deform.ready(), "partial allocation not released");
    check(!deform.lastError().empty(), "failure diagnostic missing");
}
} // namespace
int main() {
    try {
        packing();
            dispatchValidation();
        allocationFailure();
        std::cout << "VIVID GPU contract PASS (recording backend, ABI, buffers, dispatch validation)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "VIVID GPU contract FAIL: " << e.what() << '\n';
        return 1;
    }
}
