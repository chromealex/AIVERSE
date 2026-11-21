// Real Metal Strata diagnostic culling against the CPU planView oracle.
// The normal Renderer3D remains the sole production draw owner.
// Opt-in native test; requires a macOS Metal layer, NOT a recording RHI.
#include "engine/render/StrataGpuCulling.h"
#include "engine/render/StrataGpuScene.h"
#include "engine/render/GlobalGeometryPool.h"
#include "engine/platform/Platform.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::render::strata;

namespace {
struct Fixture {
    gfx::Device* device = nullptr;
    Model model;
    ModelAsset source;
    GpuMetadataScene gpu;
    GpuClusterCulling culling;
    uint32_t frameCount = 0;
    int requested = 0, received = 0;
    uint32_t pooledIndexBase = 0;
    bool failed = false, submitted = false;
    std::string failure;

    void fail(const std::string& reason) {
        if (failed) return;
        failed = true;
        failure = reason;
        std::fprintf(stderr, "[strata-metal] FAIL: %s\n", reason.c_str());
    }

    bool initialize(gfx::Device& dev) {
        device = &dev;
        if (dev.info().backend != gfx::Backend::Metal || !dev.supportsCompute()) {
            fail("Metal compute unavailable");
            return false;
        }

        // Source positions and indices are ordinary immutable model data.
        // Upload through the original GlobalGeometryPool, not a Strata pool.
        model.vertices.resize(4);
        model.vertices[0].pos = {-0.5f, -0.5f, 0.4f};
        model.vertices[1].pos = { 0.5f, -0.5f, 0.4f};
        model.vertices[2].pos = {-0.5f,  0.5f, 0.4f};
        model.vertices[3].pos = { 0.5f,  0.5f, 0.4f};
        model.indices = {0, 1, 2, 1, 3, 2};
        if (!uploadModelToGeometryPool(dev, model)) {
            fail("shared pool upload failed (set AIVERSE_METAL_CPU_MDI=1)");
            return false;
        }

        ModelPrimitiveAsset primitive;
        primitive.mesh = 0;
        primitive.primitive = 0;
        primitive.material = 0;
        Asset& asset = primitive.geometry;
        // Parent first; its two leaf children represent the same surface.
        // Projected error 0.25 produces two leaves with a small threshold,
        // while a large threshold selects the resident parent.
        asset.indices = {0, 1, 2, 0, 1, 2, 1, 3, 2};
        asset.roots = {0};
        asset.nodes.resize(3);
        asset.bounds.resize(3);
        for (uint32_t i = 0; i != 3; ++i) {
            auto& n = asset.nodes[i];
            n.resident = true;
            n.materialGroup = 0;
            n.indexCount = 3;
            n.firstIndex = 3 * i;
        }
        asset.nodes[0].firstChild = 1;
        asset.nodes[0].childCount = 2;
        asset.nodes[0].geometricError = 0.25f;
        asset.bounds[0] = {{-0.5f, -0.5f, 0.2f}, {0.5f, 0.5f, 0.8f}};
        asset.bounds[1] = {{-0.5f, -0.5f, 0.2f}, {0.0f, 0.5f, 0.8f}};
        asset.bounds[2] = {{0.0f, -0.5f, 0.2f}, {0.5f, 0.5f, 0.8f}};
        source.primitives.push_back(std::move(primitive));
        if (!gpu.stage(dev, "fixture", model, source) ||
            !gpu.find("fixture", model).valid()) {
            fail("version-2 shared-pool metadata staging failed");
            return false;
        }
        GpuMetadataScene::IndexBinding binding;
        if (!gpu.indexBinding("fixture", model, binding) ||
            !binding.valid()) {
            fail("no pooled auxiliary Strata index binding");
            return false;
        }
        pooledIndexBase = binding.firstIndex;
        return true;
    }

    void submitCase(const char* name, const GpuPassConfig& pass,
                    const std::vector<GpuSceneInstance>& instances,
                    uint32_t expectedProduced, bool expectOverflow = false) {
        if (failed || !device) return;
        GpuSelectionSubmission submission;
        const gfx::BufferHandle metadata = gpu.find("fixture", model);
        if (!culling.dispatch(*device, metadata, instances, pass,
                              device->frameSerial(), submission)) {
            fail(std::string(name) + ": GPU dispatch / MSL compile failed: " +
                 device->lastError());
            return;
        }
        ++requested;
        const bool accepted = culling.requestReadback(*device, submission,
            [this, name = std::string(name), pass, instances,
             expectedProduced, expectOverflow](GpuSelectionReadback readback) {
                ++received;
                if (failed) return;
                if (expectOverflow) {
                    if (readback.status != GpuSelectionReadback::Status::Overflow)
                        fail(name + ": expected GPU overflow flag");
                    else
                        std::printf("[strata-metal] PASS: %s overflow correctly rejected\n",
                                    name.c_str());
                    return;
                }
                if (!readback.valid() ||
                    readback.counters.produced != expectedProduced) {
                    fail(name + ": GPU status invalid or unexpected cluster count " +
                         std::to_string(readback.counters.produced));
                    return;
                }
                std::string reason;
                if (!compareGpuSelection(source, instances, pass,
                        pooledIndexBase, readback, &reason)) {
                    fail(name + ": GPU/CPU mismatch: " + reason);
                    return;
                }
                std::printf("[strata-metal] PASS: %s selected=%u examined=%u\n",
                            name.c_str(), readback.counters.produced,
                            readback.counters.examined);
            });
        if (!accepted) fail(std::string(name) + ": async readback scheduling failed");
    }

    void frame(gfx::Device& dev) {
        ++frameCount;
        if (failed || (submitted && received == requested)) {
            platform::requestQuit();
            return;
        }
        if (frameCount > 120) {
            fail("GPU readback timeout");
            platform::requestQuit();
            return;
        }
        if (submitted) return;
        submitted = true;

        const auto a = makeGpuSceneInstance(mat4::identity(), 0, 1001);
        const auto b = makeGpuSceneInstance(mat4::translate({0.1f, 0, 0}),
                                            0, 1002);
        GpuPassConfig fine{};
        fine.viewProjection = mat4::identity();
        fine.width = 800; fine.height = 600;
        fine.clipDepthZeroToOne = dev.info().clipDepthZeroToOne;
        fine.errorThresholdPixels = 1.0f;
        fine.maxOutputClusters = 16;
        submitCase("fine-LOD-two-instances", fine, {a,b}, 4);
        GpuPassConfig coarse = fine;
        coarse.errorThresholdPixels = 5000.f;
        submitCase("coarse-LOD", coarse, {a,b}, 2);
        GpuPassConfig invisible = fine;
        invisible.viewProjection = mat4::translate({12, 0, 0});
        submitCase("frustum-rejection", invisible, {a,b}, 0);
        auto disabled = makeGpuSceneInstance(mat4::identity(), 0, 1003, false);
        submitCase("disabled-instance", fine, {a,disabled}, 2);
        GpuPassConfig overflow = fine;
        overflow.maxOutputClusters = 1;
        submitCase("bounded-output", overflow, {a,b}, 4, true);
    }

    void shutdown() {
        if (!device) return;
        culling.shutdown(*device);
        gpu.shutdown(*device);
        releaseModelGeometryPool(*device, model);
        std::printf("[strata-metal] cases=%d/%d status=%s\n",
                    received, requested, failed ? "FAILED" : "PASSED");
    }
};
} // namespace

int aiverseMain(int, char**) {
    // This only enables the pre-existing *CPU* shared-pool admission path;
    // it does NOT turn on GPU-encoded ICB or Strata draw submission.
    if (setenv("AIVERSE_METAL_CPU_MDI", "1", 1) != 0) return 2;
    Fixture fixture;
    platform::AppConfig cfg;
    cfg.title = "Strata Metal GPU/CPU selection parity (diagnostic)";
    cfg.width = 800; cfg.height = 600; cfg.highDpi = false;
    platform::AppCallbacks callbacks;
    callbacks.init = [&](gfx::Device& dev) {
        if (!fixture.initialize(dev))
            platform::requestQuit();
    };
    callbacks.frame = [&](gfx::Device& dev, double) { fixture.frame(dev); };
    callbacks.shutdown = [&] { fixture.shutdown(); };
    platform::run(cfg, callbacks);
    if (fixture.failed || fixture.requested != 5 || fixture.received != 5)
        return 1;
    return 0;
}
