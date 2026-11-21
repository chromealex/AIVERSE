// macOS real Metal GPU execution and numerical parity for VIVID sparse morphs.
// This test executes kVividMorphMSL, unlike vivid_gpu_contract (mock RHI).
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "engine/character/vivid/GpuMorph.h"
#include "engine/character/vivid/VividComputeShaders.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

using namespace aiverse::vivid;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
std::string errorText(NSError* error) {
    return error ? std::string([[error localizedDescription] UTF8String]) : "unknown Metal error";
}

struct Fixture {
    SparseMorphLibrary morphs;
    std::vector<MorphVertex> base;
    std::vector<float> weights;
};

Fixture fixture() {
    constexpr std::uint32_t kVertices = 137;
    constexpr std::uint32_t kShapes = 160;
    std::vector<MorphTargetInput> targets;
    targets.reserve(kShapes);
    for (std::uint32_t target = 0; target < kShapes; ++target) {
        MorphTargetInput shape;
        shape.name = "shape_" + std::to_string(target);
        for (std::uint32_t j = 0; j < 3; ++j) {
            MorphDelta d{};
            d.vertex = (target * 17 + j * 13) % kVertices;
            const float a = (float(target % 9) - 4.0f) * 0.003f;
            d.position = {a * (float(j) + 1.0f), -0.002f * float(j + 1), 0.001f * float(target % 4)};
            d.normal = {0.003f * float(j + 1), 0.0f, -0.002f * float(target % 3)};
            d.tangent = {0.0f, 0.002f * float((target + j) % 5), 0.0f};
            shape.deltas.push_back(d);
        }
        targets.push_back(std::move(shape));
    }
    Fixture f{SparseMorphLibrary::compile(kVertices, std::move(targets)), {}, {}};
    f.base.resize(kVertices);
    for (std::uint32_t v = 0; v < kVertices; ++v) {
        f.base[v].position = {float(v) * 0.05f, float(v % 11) * 0.03f, float(v % 7) * -0.02f};
        f.base[v].normal = {0.0f, 1.0f, 0.0f};
        f.base[v].tangent = {1.0f, 0.0f, 0.0f};
    }
    f.weights.resize(kShapes);
    for (std::uint32_t i = 0; i < kShapes; ++i)
        f.weights[i] = float((i * 37u) % 101u) / 100.0f;
    return f;
}
id<MTLBuffer> makeBuffer(id<MTLDevice> device, const void* data, std::size_t bytes) {
    check(bytes > 0, "zero-sized Metal buffer");
    id<MTLBuffer> buffer = data
        ? [device newBufferWithBytes:data length:bytes options:MTLResourceStorageModeShared]
        : [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    check(buffer != nil, "Metal buffer allocation failed");
    return buffer;
}
float compareAxis(const MorphVec3& expected, const float* got) {
    float maxError = 0.0f;
    for (int i = 0; i < 3; ++i) {
        check(std::isfinite(got[i]), "Metal emitted non-finite vertex channel");
        maxError = std::max(maxError, std::fabs(expected[std::size_t(i)] - got[i]));
    }
    return maxError;
}
void run(id<MTLDevice> device) {
    const Fixture f = fixture();
    check(f.morphs.targets() == 160 && f.morphs.vertices() == 137, "fixture was truncated");
    id<MTLCommandQueue> queue = [device newCommandQueue];
    check(queue != nil, "Metal command queue unavailable");

    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:kVividMorphMSL];
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) throw std::runtime_error("VIVID Metal shader compilation failed: " + errorText(error));
    id<MTLFunction> function = [library newFunctionWithName:@"cs_main"];
    check(function != nil, "VIVID Metal cs_main missing");
    error = nil;
    id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
    if (!pipeline) throw std::runtime_error("VIVID Metal pipeline creation failed: " + errorText(error));
    check(pipeline.maxTotalThreadsPerThreadgroup >= 64, "Metal device lacks 64-thread compute groups");

    std::vector<GpuMorphVertex> packed(f.base.size());
    for (std::size_t i = 0; i < f.base.size(); ++i) packed[i] = packMorphVertex(f.base[i]);
    const auto deltas = f.morphs.gpuDeltas();
    const auto offsets = f.morphs.gpuVertexOffsets();
    const auto links = f.morphs.gpuVertexLinks();
    id<MTLBuffer> base = makeBuffer(device, packed.data(), packed.size() * sizeof(GpuMorphVertex));
    id<MTLBuffer> shapeDeltas = makeBuffer(device, deltas.data(), deltas.size_bytes());
    id<MTLBuffer> vertexOffsets = makeBuffer(device, offsets.data(), offsets.size_bytes());
    id<MTLBuffer> vertexLinks = makeBuffer(device, links.data(), links.size_bytes());
    id<MTLBuffer> weights = makeBuffer(device, f.weights.data(), f.weights.size() * sizeof(float));
    id<MTLBuffer> output = makeBuffer(device, nullptr, packed.size() * sizeof(GpuMorphVertex));

    GpuMorphUniforms params{};
    params.vertices = static_cast<std::uint32_t>(f.base.size());
    params.targets = static_cast<std::uint32_t>(f.morphs.targets());
    params.deltas = static_cast<std::uint32_t>(deltas.size());
    params.links = static_cast<std::uint32_t>(links.size());

    for (int pass = 0; pass < 3; ++pass) {
        std::vector<float> currentWeights = f.weights;
        if (pass == 2) std::fill(currentWeights.begin(), currentWeights.end(), 0.0f);
        std::memcpy(weights.contents, currentWeights.data(), currentWeights.size() * sizeof(float));
        const bool normalized = pass != 1;
        params.normalizeDirections = normalized ? 1u : 0u;
        id<MTLCommandBuffer> command = [queue commandBuffer];
        check(command != nil, "Metal command buffer creation failed");
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        check(encoder != nil, "Metal compute encoder unavailable");
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:base offset:0 atIndex:0];
        [encoder setBuffer:shapeDeltas offset:0 atIndex:1];
        [encoder setBuffer:vertexOffsets offset:0 atIndex:2];
        [encoder setBuffer:vertexLinks offset:0 atIndex:3];
        [encoder setBuffer:weights offset:0 atIndex:4];
        [encoder setBuffer:output offset:0 atIndex:5];
        [encoder setBytes:&params length:sizeof(params) atIndex:8];
        const NSUInteger groups = (f.base.size() + 63u) / 64u;
        [encoder dispatchThreadgroups:MTLSizeMake(groups,1,1)
               threadsPerThreadgroup:MTLSizeMake(64,1,1)];
        [encoder endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted)
            throw std::runtime_error("Metal compute command failed: " + errorText(command.error));

        std::vector<MorphVertex> reference(f.base.size());
        check(f.morphs.evaluateGather(f.base,currentWeights,reference,normalized),
              "CPU reference failed");
        const auto* actual = static_cast<const GpuMorphVertex*>(output.contents);
        check(actual != nullptr, "Metal output buffer is not CPU-mappable");
        float largestError = 0.0f;
        for (std::size_t i = 0; i < reference.size(); ++i) {
            largestError = std::max(largestError, compareAxis(reference[i].position, actual[i].position));
            largestError = std::max(largestError, compareAxis(reference[i].normal, actual[i].normal));
            largestError = std::max(largestError, compareAxis(reference[i].tangent, actual[i].tangent));
        }
        // Roundoff from 160 independent fp32 shapes + normalize() is expected,
        // but coordinates and directions must agree well within visual precision.
        if (largestError > 0.0005f)
            throw std::runtime_error("Metal/CPU VIVID morph mismatch: max error " +
                                     std::to_string(largestError) + ", pass " + std::to_string(pass));
        std::cout << "VIVID real Metal GPU pass=" << pass
                  << " vertices=" << reference.size()
                  << " shapes=" << f.morphs.targets()
                  << " max_abs_error=" << largestError << '\n';
    }
}
} // namespace

int main() {
    @autoreleasepool {
        try {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device) {
                std::cout << "SKIP: no Metal-capable device\n";
                return 77;
            }
            run(device);
            std::cout << "VIVID real Metal GPU parity PASS\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "VIVID real Metal GPU parity FAIL: " << e.what() << '\n';
            return 1;
        }
    }
}
