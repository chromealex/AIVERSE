// VIVID real gfx::Device Metal RHI integration: not a fake device.
// Uses the engine's createMetalDevice / beginFrame / upload / compute /
// endFrame / diagnostic readBuffer path against the CPU CSR gather reference.
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#include "engine/character/vivid/GpuMorph.h"
#include "engine/character/vivid/GpuMorphMeshStream.h"
#include "engine/character/vivid/GpuActorStream.h"
#include "engine/character/vivid/GpuActorRetirement.h"
#include "engine/character/vivid/GpuMorphAssetCache.h"
#include "engine/gfx/Gfx.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace aiverse;
using namespace aiverse::vivid;

// MetalShaderAssets uses the regular platform content reader for optional
// precompiled archives. Standalone graphics tests have no content root.
namespace aiverse::platform {
bool readContentFile(const std::string&, std::string&) { return false; }
}

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    SparseMorphLibrary library;
    std::vector<MorphVertex> base;
    std::vector<float> weights;
};
Fixture makeFixture() {
    constexpr std::uint32_t vertexCount = 79;
    constexpr std::uint32_t shapeCount = 160;
    std::vector<MorphTargetInput> targets;
    targets.reserve(shapeCount);
    for (std::uint32_t shape = 0; shape < shapeCount; ++shape) {
        MorphTargetInput input;
        input.name = "rhi_morph_" + std::to_string(shape);
        for (std::uint32_t influence = 0; influence < 3; ++influence) {
            MorphDelta d{};
            d.vertex = (shape * 13u + influence * 29u) % vertexCount;
            d.position = {
                (float(int(shape % 11u) - 5) * 0.002f) * float(influence + 1),
                float(influence + 1) * 0.00075f,
                float(shape % 7u) * -0.0003f
            };
            d.normal = {0.002f * float(influence + 1), 0.0f, float(shape % 5u) * 0.0004f};
            d.tangent = {0.0f, float(shape % 3u) * 0.0005f, 0.0f};
            input.deltas.push_back(d);
        }
        targets.push_back(std::move(input));
    }
    Fixture f{SparseMorphLibrary::compile(vertexCount, std::move(targets)), {}, {}};
    f.base.resize(vertexCount);
    f.weights.resize(shapeCount);
    for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex) {
        f.base[vertex].position = {float(vertex) * 0.012f, float(vertex % 9u) * -0.015f, float(vertex % 13u) * 0.021f};
        f.base[vertex].normal = {0, 1, 0};
        f.base[vertex].tangent = {1, 0, 0};
    }
    for (std::uint32_t shape = 0; shape < shapeCount; ++shape)
        f.weights[shape] = float((shape * 31u) % 101u) / 100.0f;
    return f;
}
float componentError(const MorphVec3& reference, const MorphVec3& actual) {
    float maximum = 0.0f;
    for (std::size_t c = 0; c < 3; ++c) {
        require(std::isfinite(actual[c]), "VIVID Metal RHI emitted nonfinite component");
        maximum = std::max(maximum, std::fabs(reference[c] - actual[c]));
    }
    return maximum;
}
void compare(const std::vector<MorphVertex>& expected, const std::vector<MorphVertex>& actual,
             std::uint32_t frame) {
    require(expected.size() == actual.size(), "VIVID Metal RHI vertex count mismatch");
    float maximum = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        maximum = std::max(maximum, componentError(expected[i].position, actual[i].position));
        maximum = std::max(maximum, componentError(expected[i].normal, actual[i].normal));
        maximum = std::max(maximum, componentError(expected[i].tangent, actual[i].tangent));
    }
    if (maximum > 0.0005f) {
        throw std::runtime_error("VIVID Metal RHI differs from CPU: frame=" +
            std::to_string(frame) + " max_abs_error=" + std::to_string(maximum));
    }
    std::cout << "VIVID Metal RHI frame=" << frame
              << " vertices=" << actual.size()
              << " max_abs_error=" << maximum << '\n';
}
void run() {
    @autoreleasepool {
        id<MTLDevice> metal = MTLCreateSystemDefaultDevice();
        require(metal != nil, "Metal device unexpectedly missing");
        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.device = metal;
        layer.drawableSize = CGSizeMake(8, 8);
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        // The layer is intentionally headless: no render pass is needed by
        // the compute test, but gfx::Device owns and commits GPU frame queues.
        auto device = gfx::createMetalDevice((__bridge void*)layer);
        require(device != nullptr && device->supportsCompute(), "AIVERSE Metal RHI compute unavailable");

        const Fixture f = makeFixture();
        require(f.library.targets() == 160 && f.library.vertices() == 79, "fixture was truncated");
        GpuMorphDeformer deformer;
        require(deformer.initialize(*device, f.library, f.base),
                "VIVID RHI GPU initialize failed");
        require(deformer.ready() && deformer.outputBuffer().valid(),
                "VIVID RHI deformer output missing");

        // Match the real renderer's complete interleaved 60-byte vertex
        // format, including attributes that facial morphing must preserve.
        std::vector<render::ModelVertex> original(f.base.size());
        for (std::size_t i = 0; i < f.base.size(); ++i) {
            const auto& src = f.base[i];
            auto& v = original[i];
            v.pos = {src.position[0],src.position[1],src.position[2]};
            v.nrm = {src.normal[0],src.normal[1],src.normal[2]};
            v.uv = {float(i % 17) / 17.0f,float(i % 11) / 11.0f};
            v.tangent = {src.tangent[0],src.tangent[1],src.tangent[2],i % 2 ? -1.0f : 1.0f};
            v.color = static_cast<std::uint32_t>(0x8899cc00u | static_cast<std::uint32_t>(i & 0xffu));
            for (int j = 0; j < 4; ++j) {
                v.joints[j] = static_cast<std::uint8_t>((i + std::size_t(j)) % 64u);
                v.weights[j] = static_cast<std::uint8_t>(j==0 ? 255 : 0);
            }
        }
        GpuMorphMeshStream meshStream;
        if (!meshStream.initialize(*device,original)) {
            throw std::runtime_error(
                "VIVID RHI 60-byte mesh stream initialization failed: " +
                meshStream.lastError() +
                " | gfx::Device: " + device->lastError());
        }
        require(meshStream.ready() && meshStream.vertexCount()==f.base.size(),
                "VIVID RHI 60-byte mesh stream unavailable");

        std::vector<float> weights = f.weights;
        for (std::uint32_t frame = 0; frame < 4; ++frame) {
            if (frame == 1)
                std::fill(weights.begin(), weights.end(), 0.0f);
            if (frame == 2)
                for (std::size_t i = 0; i < weights.size(); ++i)
                    weights[i] = float(i % 3u) * 0.5f;
            const bool normalizeDirections = frame != 3;
            device->beginFrame(8, 8);
            const bool submitted = deformer.dispatch(weights, normalizeDirections);
            const bool packed = submitted &&
                meshStream.dispatch(deformer.outputBuffer(), deformer.vertexCount());
            device->endFrame();
            if (!submitted)
                throw std::runtime_error("VIVID RHI dispatch failed on frame " +
                    std::to_string(frame) + ": " + deformer.lastError());
            if (!packed)
                throw std::runtime_error("VIVID RHI mesh packing dispatch failed on frame " +
                    std::to_string(frame) + ": " + meshStream.lastError());
            require(device->gpuHealthy(), "AIVERSE Metal RHI reported GPU failure");
            std::vector<MorphVertex> output(f.base.size());
            require(deformer.download(output), "VIVID Metal RHI synchronous readback failed");
            std::vector<MorphVertex> expected(f.base.size());
            require(f.library.evaluateGather(f.base, weights, expected, normalizeDirections),
                    "VIVID RHI CPU reference failed");
            compare(expected, output, frame);

            std::vector<render::ModelVertex> renderVertices(original.size());
            require(meshStream.download(renderVertices),
                    "VIVID RHI 60-byte mesh stream readback failed");
            float meshError=0.0f;
            for (std::size_t i=0; i<renderVertices.size(); ++i) {
                const auto& result=renderVertices[i];
                const auto& correct=expected[i];
                const MorphVec3 gotPos{result.pos.x,result.pos.y,result.pos.z};
                const MorphVec3 gotNrm{result.nrm.x,result.nrm.y,result.nrm.z};
                const MorphVec3 gotTan{result.tangent.x,result.tangent.y,result.tangent.z};
                meshError=std::max(meshError,componentError(correct.position,gotPos));
                meshError=std::max(meshError,componentError(correct.normal,gotNrm));
                meshError=std::max(meshError,componentError(correct.tangent,gotTan));
                const auto* rawA=reinterpret_cast<const unsigned char*>(&original[i]);
                const auto* rawB=reinterpret_cast<const unsigned char*>(&result);
                // UV at [24,32), tangent handedness/color/joints/weights [44,60):
                // every non-morph byte must survive unchanged across GPU passes.
                require(std::memcmp(rawA+24,rawB+24,8)==0 &&
                        std::memcmp(rawA+44,rawB+44,16)==0,
                        "VIVID GPU mesh stream corrupted UV/colour/tangent sign/skin weights");
            }
            if (meshError>0.0005f)
                throw std::runtime_error("VIVID RHI native ModelVertex mismatch frame " +
                                         std::to_string(frame) +
                                         " max_abs_error=" + std::to_string(meshError));
            std::cout << "VIVID Metal RHI ModelVertex(60B) frame=" << frame
                      << " max_abs_error=" << meshError << '\n';
        }
        // VIVID multi-actor GPU: each character owns a separate writable
        // 48B + 60B compute pipeline, even when sharing ONE immutable Model
        // and sparse library. Repeated submission in the same frame must
        // fail rather than rewrite the stream queued for a model draw.
        {
            render::Model sharedModel;
            sharedModel.vertices=original;
            sharedModel.meshes.resize(1);
            sharedModel.meshes[0].primitives.resize(1);
            auto& p=sharedModel.meshes[0].primitives[0];
            p.firstVertex=0;
            p.vertexCount=static_cast<std::uint32_t>(original.size());
            p.firstIndex=0;
            p.indexCount=3;
            gfx::BufferDesc vertexDesc{};
            vertexDesc.type=gfx::BufferType::Vertex;
            vertexDesc.size=original.size()*sizeof(render::ModelVertex);
            vertexDesc.data=original.data();
            sharedModel.vb=device->createBuffer(vertexDesc);
            const std::uint32_t triangle[3]={0,1,2};
            gfx::BufferDesc indexDesc{};
            indexDesc.type=gfx::BufferType::Index;
            indexDesc.size=sizeof(triangle);
            indexDesc.data=triangle;
            sharedModel.ib=device->createBuffer(indexDesc);
            require(sharedModel.vb.valid() && sharedModel.ib.valid(),
                    "multi-actor fixture model GPU upload failed");
            const std::vector<ShapeRoute> routes{
                {Channel::JawOpen,"rhi_morph_0",1.0f},
                {Channel::EyeBlinkRight,"rhi_morph_1",1.0f}};
            const auto mapping=FaceShapeBinding::compile(f.library,routes);
            // Both actors share the SAME immutable GPU CSR, original mesh
            // and compute programs; their weights and output VBs remain private.
            GpuMorphAssetCache cache(*device);
            auto sharedAsset=cache.acquire(f.library,original);
            auto cachedAgain=cache.acquire(f.library,original);
            require(sharedAsset && sharedAsset->ready() &&
                    sharedAsset.get()==cachedAgain.get() &&
                    cache.cachedKeys()==1,
                    "shared Metal morph library was not cached");
            GpuActorStream actorA, actorB;
            require(actorA.initializeShared(*device,sharedModel,sharedAsset),
                    "multi-actor shared GPU stream A initialization failed");
            require(actorB.initializeShared(*device,sharedModel,sharedAsset),
                    "multi-actor shared GPU stream B initialization failed");
            require(actorA.sharedAsset()==sharedAsset.get() &&
                    actorB.sharedAsset()==sharedAsset.get() &&
                    actorA.vertexBuffer()!=actorB.vertexBuffer(),
                    "multi-actor immutable sharing or writable buffer isolation failed");
            for(std::uint32_t frame=0;frame<3;++frame) {
                FaceFrame faceA{}, faceB{};
                faceA.values[static_cast<std::size_t>(Channel::JawOpen)]=0.25f+frame*0.2f;
                faceB.values[static_cast<std::size_t>(Channel::EyeBlinkRight)]=0.35f+frame*0.2f;

                device->beginFrame(8,8);
                // A previous-frame stream must be rejected only after the
                // new frame serial has advanced at beginFrame().
                render::ModelDraw before;
                before.model=&sharedModel;
                if(frame>0)require(!actorA.attach(before),
                    "VIVID actor accepted stale GPU stream in a new frame");
                require(actorA.dispatch(faceA,mapping), "VIVID actor A dispatch failed");
                require(actorB.dispatch(faceB,mapping), "VIVID actor B dispatch failed");
                require(!actorA.dispatch(faceA,mapping),
                        "VIVID actor accepted two writes in the same frame");
                render::ModelDraw drawA, drawB;
                drawA.model=drawB.model=&sharedModel;
                require(actorA.attach(drawA) && actorB.attach(drawB),
                        "VIVID actor refused fresh ModelDraw binding");
                require(drawA.vertexOverride.valid() && drawB.vertexOverride.valid() &&
                        drawA.vertexOverride!=drawB.vertexOverride &&
                        drawA.vertexOverrideCount==original.size() &&
                        drawB.vertexOverrideCount==original.size(),
                        "VIVID per-actor stream identity/count mismatch");
                render::ModelDraw unrelated;
                require(!actorA.attach(unrelated) && !unrelated.vertexOverride.valid(),
                        "VIVID attached actor GPU data to an unrelated ModelDraw");
                device->endFrame();

                auto checkActor=[&](const GpuActorStream& actor,
                                    const FaceFrame& face) {
                    std::vector<float> weights(f.library.targets(),0.0f);
                    require(mapping.evaluate(face,weights),
                            "multi-actor face weights reference failed");
                    std::vector<MorphVertex> reference(f.base.size());
                    require(f.library.evaluateGather(f.base,weights,reference,true),
                            "multi-actor CPU gather reference failed");
                    std::vector<render::ModelVertex> got(original.size());
                    require(actor.download(got), "multi-actor GPU output readback failed");
                    float largest=0.0f;
                    for(std::size_t i=0;i<got.size();++i) {
                        const MorphVec3 p{got[i].pos.x,got[i].pos.y,got[i].pos.z};
                        const MorphVec3 n{got[i].nrm.x,got[i].nrm.y,got[i].nrm.z};
                        const MorphVec3 t{got[i].tangent.x,got[i].tangent.y,got[i].tangent.z};
                        largest=std::max(largest,componentError(reference[i].position,p));
                        largest=std::max(largest,componentError(reference[i].normal,n));
                        largest=std::max(largest,componentError(reference[i].tangent,t));
                        const auto* src=reinterpret_cast<const unsigned char*>(&original[i]);
                        const auto* out=reinterpret_cast<const unsigned char*>(&got[i]);
                        require(std::memcmp(src+24,out+24,8)==0 &&
                                std::memcmp(src+44,out+44,16)==0,
                                "multi-actor GPU modified immutable vertex attributes");
                    }
                    require(largest<=0.0005f,"multi-actor GPU parity failed");
                };
                checkActor(actorA,faceA);
                checkActor(actorB,faceB);
            }
            require(actorA.shutdownAfterIdle() && actorB.shutdownAfterIdle(),
                    "multi-actor idle teardown failed");
            require(!actorA.ready() && !actorB.ready() &&
                    !actorA.vertexBuffer().valid() && !actorB.vertexBuffer().valid(),
                    "multi-actor GPU stream handles survived shutdown");
            // Real Metal queue completion: the GPU actor remains alive while
            // the completion marker is pending, without a per-actor idle wait.
            auto asyncActor=std::make_unique<GpuActorStream>();
            require(asyncActor->initialize(*device,sharedModel,f.library,original),
                    "async-retired Metal actor GPU initialization failed");
            device->beginFrame(8,8);
            FaceFrame retiringFace{};
            retiringFace.values[static_cast<std::size_t>(Channel::JawOpen)]=0.6f;
            require(asyncActor->dispatch(retiringFace,mapping),
                    "async-retired Metal actor dispatch failed");
            device->endFrame();

            GpuActorRetirementQueue retireQueue(*device);
            require(retireQueue.retire(asyncActor) && !asyncActor,
                    "Metal retirement queue did not take actor ownership");
            require(!retireQueue.collect(),
                    "Metal GPU marker incorrectly reported completed at creation");
            bool retired=false;
            for(int attempt=0;attempt<500 && !retired;++attempt) {
                retired=retireQueue.collect();
                if(!retired)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(!retired) {
                // Explicit shutdown recovery is allowed, but we still fail
                // the nonblocking-token test if the marker never completes.
                require(retireQueue.drain(),
                        "Metal retirement marker failed and idle recovery failed");
                throw std::runtime_error(
                    "Metal actor retirement never completed asynchronously");
            }
            const auto stats=retireQueue.stats();
            require(retireQueue.pending()==0 && stats.completedResources==1 &&
                    stats.completedBatches==1 && stats.tokenFailures==0 &&
                    stats.idleFallbackFailures==0,
                    "Metal retirement queue did not use a completion token");
            std::cout << "VIVID real Metal async actor retirement PASS\n";

            // All earlier actor work was completed by an idle barrier or
            // checked GPU token. Retire last shared asset references only now.
            sharedAsset.reset();
            cachedAgain.reset();
            require(cache.pruneExpired()==1 && cache.cachedKeys()==0,
                    "Metal weak GPU asset cache retained expired geometry");
            device->destroyBuffer(sharedModel.vb);
            device->destroyBuffer(sharedModel.ib);
            std::cout << "VIVID multi-actor GPU 2 independent actors / 3 frames PASS\n";
        }

        meshStream.release();
        require(!meshStream.ready() && !meshStream.vertexBuffer().valid(),
                "VIVID RHI mesh stream output leaked after release");
        deformer.release(); // Must precede device destruction.
        require(!deformer.ready() && !deformer.outputBuffer().valid(),
                "VIVID RHI output handle leaked after release");
    }
}
}

int main() {
    @autoreleasepool {
        if (MTLCreateSystemDefaultDevice() == nil) {
            std::cout << "SKIP: no Metal-capable device\n";
            return 77;
        }
        try {
            run();
            std::cout << "VIVID real Metal gfx::Device parity PASS\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "VIVID real Metal gfx::Device parity FAIL: " << e.what() << '\n';
            return 1;
        }
    }
}
