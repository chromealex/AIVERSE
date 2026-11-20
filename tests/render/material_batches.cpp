// Real-backend A/B: the same model/material copies, camera and shadow map, with batching off/on.
#include "engine/render/Renderer3D.h"
#include "engine/render/ModelMerge.h"
#include "engine/render/GpuMaterialState.h"
#include "engine/platform/Platform.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <vector>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_info.h>
#endif
using namespace aiverse;

// CPU consumed by this calling thread, not whole-process CPU. Wall time
// includes descheduling/waits; their delta is diagnostic, not proof of a GPU
// stall or scheduler fault. Unsupported platforms report unavailable.
static bool cpuClockNs(bool processClock, uint64_t& result) {
#if defined(__APPLE__) || defined(__linux__)
    timespec ts{};
    const clockid_t id = processClock ? CLOCK_PROCESS_CPUTIME_ID
                                       : CLOCK_THREAD_CPUTIME_ID;
    if (clock_gettime(id, &ts) != 0) return false;
    result = uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
    return true;
#else
    (void)processClock;
    (void)result;
    return false;
#endif
}

#if defined(__APPLE__)
static bool machCurrentThreadCpuNs(uint64_t& result) {
    thread_basic_info_data_t info{};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    const thread_t thread = mach_thread_self();
    const kern_return_t status = thread_info(thread, THREAD_BASIC_INFO,
                                             reinterpret_cast<thread_info_t>(&info),
                                             &count);
    mach_port_deallocate(mach_task_self(), thread);
    if (status != KERN_SUCCESS) return false;
    const uint64_t seconds =
        uint64_t(info.user_time.seconds) + uint64_t(info.system_time.seconds);
    const uint64_t micros =
        uint64_t(info.user_time.microseconds) + uint64_t(info.system_time.microseconds);
    result = seconds * 1000000000ull + micros * 1000ull;
    return true;
}
#endif
// GPU-free invariants, including shared vertices and material overrides.
static bool unitChecks() {
    using namespace render;
    int checks=0;
    auto check=[&](bool ok){++checks;if(!ok)std::fprintf(stderr,"material palette check %d failed\n",checks);return ok;};
    Model m; m.materials.resize(3); m.vertices.resize(3); m.indices={0,1,2,0,1,2,0,1,2};
    m.materials[0].baseColor={.137f,.271f,.819f,1};
    m.materials[1].metallic=.731f; m.materials[1].roughness=.213f;
    m.materials[2].baseTex=1;
    MeshDef mesh;
    for(int i=0;i<3;++i){Primitive p;p.firstIndex=uint32_t(i*3);p.indexCount=3;p.material=i;mesh.primitives.push_back(p);}
    m.meshes.push_back(mesh);
    const auto indices=m.indices;
    bool ok=check(prepareMaterialBatches(m)==2);
    ok&=check(m.meshes[0].materialBatches.size()==1);
    const auto& batch=m.meshes[0].materialBatches[0];
    ok&=check(batch.primitive.indexCount==6 && batch.parts==std::vector<size_t>{0,1});
    ok&=check(m.vertices.size()==6 && std::equal(indices.begin(),indices.end(),m.indices.begin()));
    for(uint32_t i=0;i<6;++i)ok&=check(m.vertices[m.indices[batch.primitive.firstIndex+i]].weights[0]==i/3);
    ok&=check(prepareMaterialBatches(m)==0);
    auto mats=m.materials;
    mats[0].baseColor.x=.999123f;mats[1].roughness=.456789f;
    ok&=check(materialBatchCompatible(m.meshes[0],batch,mats));
    mats[1].normalTex=0;ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));mats=m.materials;
    mats[1].alpha=AlphaMode::Blend;ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));mats=m.materials;
    mats[1].receiveShadow=false;ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));mats=m.materials;
    mats.resize(kMaxMaterialPalette+1);ok&=check(!materialBatchCompatible(m.meshes[0],batch,mats));
    ok&=check(m.materials[0].baseColor.x==.137f && m.materials[1].roughness==.213f);
    // A frame-scoped renderer material-group cache must not turn pointer
    // equality into an accidental GPU-state equality shortcut. Identical
    // copies across models merge, while changes to uniforms or sampled
    // texture handles do not.
    Model aModel, bModel;
    Material original, copy;
    original.baseColor={.2f,.4f,.8f,1.f};
    original.emissive={.02f,.04f,.08f};
    copy=original;
    ok&=check(equivalentGpuMaterial(aModel,original,bModel,copy) &&
              gpuMaterialStateHash(aModel,original)==gpuMaterialStateHash(bModel,copy));
    copy.emissive.x+=.1f;
    ok&=check(!equivalentGpuMaterial(aModel,original,bModel,copy));
    copy=original;
    aModel.images.resize(1); bModel.images.resize(1);
    aModel.images[0].tex={123}; bModel.images[0].tex={456};
    original.baseTex=copy.baseTex=0;
    ok&=check(!equivalentGpuMaterial(aModel,original,bModel,copy));
    bModel.images[0].tex=aModel.images[0].tex;
    ok&=check(equivalentGpuMaterial(aModel,original,bModel,copy) &&
              gpuMaterialStateHash(aModel,original)==gpuMaterialStateHash(bModel,copy));
    // Indirect material palettes may vary ONLY baseColour/metal/roughness;
    // texture identity, emissive and render state must still match exactly.
    Material variation=copy;
    variation.baseColor={.91f,.10f,.18f,1};
    variation.metallic=.67f; variation.roughness=.23f;
    ok&=check(equivalentGpuPaletteState(aModel,original,bModel,variation) &&
              gpuPaletteStateHash(aModel,original)==gpuPaletteStateHash(bModel,variation) &&
              !equivalentGpuMaterial(aModel,original,bModel,variation));
    variation.emissive.y += .01f;
    ok&=check(!equivalentGpuPaletteState(aModel,original,bModel,variation));
    variation=copy;variation.alpha=AlphaMode::Blend;
    ok&=check(!equivalentGpuPaletteState(aModel,original,bModel,variation));
    variation=copy;
    bModel.images[0].tex={789};
    ok&=check(!equivalentGpuPaletteState(aModel,original,bModel,variation));
    bModel.images[0].tex=aModel.images[0].tex;
    auto invalid=[&](int mode){
        Model x;x.vertices.resize(3);x.indices=indices;x.materials.resize(3);x.meshes.push_back(mesh);
        if(mode==0)x.meshes[0].primitives[0].morphTargets=1;
        if(mode==1)x.skins.resize(1);
        if(mode==2)x.variants.push_back("variant");
        if(mode==3)x.materials.resize(kMaxMaterialPalette+1);
        return prepareMaterialBatches(x)==0;
    };
    for(int i=0;i<4;++i)ok&=check(invalid(i));
    Model lod;lod.vertices.resize(3);lod.indices=indices;lod.materials.resize(3);lod.meshes={mesh,mesh};
    lod.meshes[0].lods.push_back({1,20});
    ok&=check(prepareMaterialBatches(lod)==6 && lod.meshes[0].materialBatches.size()==1 && lod.meshes[1].materialBatches.size()==1);
    // AUTO CPU scheduler learns independently by pass and size bucket.
    AutoCpuBatchPolicy autoPolicy;
    ok &= check(!autoPolicy.useMdi(0, 32));
    for (int i = 0; i < 10; ++i) {
        autoPolicy.observe(0, 80, false, 0.10);
        autoPolicy.observe(0, 80, true, 0.20);
    }
    ok &= check(!autoPolicy.useMdi(0, 80)); // direct cheaper
    ok &= check(!autoPolicy.useMdi(1, 80)); // independent untrained phase
    AutoCpuBatchPolicy stalled;
    for (int i = 0; i < 3; ++i) stalled.recordNoSubmission(0, 1024);
    ok &= check(!stalled.useMdi(0, 1024)); // no repeated unproductive MDI
    for (int i = 0; i < 30; ++i) stalled.useMdi(0, 1024);
    ok &= check(stalled.useMdi(0, 1024)); // periodic re-probe
    AutoCpuBatchPolicy calibrated;
    // Explicitly feed comparable completed pass measurements; the learner
    // must gather a full MDI window before trusting early expensive frames.
    for (int i = 0; i < 12; ++i)
        calibrated.observe(0, 1024, false, 1.0);
    for (int i = 0; i < 31; ++i)
        calibrated.observe(0, 1024, true, i < 4 ? 2.0 : 0.65);
    ok &= check(calibrated.useMdi(0, 1024));
    calibrated.observe(0, 1024, true, 0.65);
    ok &= check(calibrated.useMdi(0, 1024)); // MDI now wins after warmup
    ok &= check(!calibrated.useMdi(1, 1024)); // independent opaque lane

    std::printf("material palette CPU checks=%d result=%s\n",checks,ok?"passed":"FAILED");
    return ok;
}

// Deterministic 16-primitive fixture with one shader/material and independent
// node transforms. MDI and the fallback must render byte-identical images.
static std::unique_ptr<render::Model> indirectFixture(bool variedMaterials = false,
                                                       size_t primitives = 16,
                                                       uint32_t meshSubdivisions = 1) {
    using namespace render;
    auto m = std::make_unique<Model>();
    m->path = "indirect-fixture";
    m->materials.resize(variedMaterials ? 3 : 1);
    m->materials[0].doubleSided = true;
    m->materials[0].baseColor = {.72f, .48f, .19f, 1.f};
    if (variedMaterials) {
        m->materials[1] = m->materials[0];
        m->materials[1].baseColor = {.13f, .74f, .28f, 1.f};
        m->materials[1].metallic = .65f;
        m->materials[1].roughness = .35f;
        m->materials[2] = m->materials[0];
        m->materials[2].baseColor = {.15f, .25f, .85f, 1.f};
        m->materials[2].roughness = .18f;
    }
    for (size_t i = 0; i < primitives; ++i) {
        const uint32_t start = uint32_t(m->vertices.size());
        const uint32_t firstIndex = uint32_t(m->indices.size());
        // Test-only regular triangle tessellation: changes vertex/triangle
        // work while keeping model nodes, material and MDI command counts fixed.
        // Direct and MDI consume the exact same geometry for raster parity.
        const float3 a{-.3f, 0, 0}, b{.3f, 0, 0}, c{0, .75f, 0};
        auto point = [&](uint32_t u, uint32_t v) {
            const float fu = float(u) / float(meshSubdivisions);
            const float fv = float(v) / float(meshSubdivisions);
            return float3{
                a.x + (b.x - a.x) * fu + (c.x - a.x) * fv,
                a.y + (b.y - a.y) * fu + (c.y - a.y) * fv,
                a.z + (b.z - a.z) * fu + (c.z - a.z) * fv};
        };
        auto triangle = [&](float3 x, float3 y, float3 z) {
            const uint32_t firstVertex = uint32_t(m->vertices.size());
            for (const float3 pos : {x, y, z}) {
                ModelVertex vertex;
                vertex.pos = pos;
                vertex.nrm = {0, 0, 1};
                m->vertices.push_back(vertex);
            }
            m->indices.insert(m->indices.end(),
                              {firstVertex, firstVertex + 1, firstVertex + 2});
        };
        for (uint32_t u = 0; u < meshSubdivisions; ++u)
            for (uint32_t v = 0; v < meshSubdivisions - u; ++v) {
                triangle(point(u, v), point(u + 1, v), point(u, v + 1));
                if (u + v + 1 < meshSubdivisions)
                    triangle(point(u + 1, v), point(u + 1, v + 1), point(u, v + 1));
            }
        Primitive p;
        p.material = variedMaterials ? int(i % 3) : 0;
        p.firstIndex = firstIndex;
        p.indexCount = uint32_t(m->indices.size()) - firstIndex;
        p.firstVertex = start;
        p.vertexCount = uint32_t(m->vertices.size()) - start;
        MeshDef mesh;
        mesh.name = "primitive_" + std::to_string(i);
        mesh.primitives.push_back(p);
        Node node;
        node.mesh = int(m->meshes.size());
        node.name = mesh.name;
        // Stress: unique primitives in a compact region around the fixed
        // camera target. No identical-primitive instancing can collapse the
        // workload into 16 draws, and no repeated model draws are needed.
        node.t = primitives == 16
            ? float3{float(i % 4) * 1.5f - 2.25f, float(i / 4) * .9f, 0}
            : float3{float(i % 128) * .028f - 1.8f,
                     float((i / 128) % 128) * .028f,
                     float(i / 16384) * .016f};
        m->meshes.push_back(std::move(mesh));
        m->nodes.push_back(std::move(node));
    }
    m->finalize();
    return m;
}

int aiverseMain(int argc, char** argv) {
    if (!unitChecks()) return 1;
    if (argc == 2 && std::string(argv[1]) == "--unit") {
        // CPU-only topology sanity: N subdivisions must generate N*N triangles
        // per primitive without changing the number of meshes or model nodes.
        for (uint32_t subdivisions : {1u, 2u, 4u, 8u}) {
            auto fixture = indirectFixture(false, 16, subdivisions);
            const uint32_t verticesPerPrimitive = 3u * subdivisions * subdivisions;
            if (fixture->meshes.size() != 16 || fixture->nodes.size() != 16 ||
                fixture->vertices.size() != size_t(16u * verticesPerPrimitive) ||
                fixture->indices.size() != size_t(16u * verticesPerPrimitive)) {
                std::fprintf(stderr, "mesh subdivision CPU topology FAILED N=%u\n",
                             unsigned(subdivisions));
                return 1;
            }
            for (const auto& mesh : fixture->meshes)
                if (mesh.primitives.size() != 1 ||
                    mesh.primitives[0].indexCount != verticesPerPrimitive ||
                    mesh.primitives[0].vertexCount != verticesPerPrimitive) {
                    std::fprintf(stderr, "mesh subdivision primitive FAILED N=%u\n",
                                 unsigned(subdivisions));
                    return 1;
                }
        }
        std::printf("mesh subdivision CPU checks=passed\n");
        return 0;
    }
    if (argc != 3) { std::fprintf(stderr, "usage: material_batches MODEL OUTPUT_PREFIX\n"); return 2; }
    auto read = [](const std::string& path, std::string& bytes) {
        std::ifstream f(path, std::ios::binary); if (!f) return false;
        bytes.assign(std::istreambuf_iterator<char>(f), {}); return true;
    };
    if (std::string(argv[1]) == "--audit") {
        size_t models=0, before=0, after=0, addedIndices=0, addedVertices=0;
        for (const auto& entry : std::filesystem::directory_iterator(argv[2])) {
            if (entry.path().extension() != ".glb") continue;
            auto m=render::loadModel(entry.path().string(), read);
            const size_t ni=m->indices.size(), nv=m->vertices.size();
            render::prepareMaterialBatches(*m);
            addedIndices+=m->indices.size()-ni; addedVertices+=m->vertices.size()-nv;
            for (const auto& n:m->nodes) if(n.mesh>=0) {
                const auto& mesh=m->meshes[size_t(n.mesh)];
                before+=mesh.primitives.size();after+=mesh.primitives.size();
                for(const auto& b:mesh.materialBatches)after-=b.parts.size()-1;
            }
            ++models;
        }
        std::printf("models=%zu opaque_before=%zu opaque_after=%zu added_indices=%zu added_vertices=%zu\n",models,before,after,addedIndices,addedVertices);
        return models && after<before ? 0 : 1;
    }
    const std::string scenario = std::getenv("AIVERSE_PALETTE_TEST_CASE") ? std::getenv("AIVERSE_PALETTE_TEST_CASE") : "default";
    const bool stress = scenario == "auto_stress";
    // Experimental fixture switch, not a product-level quality option.
    // Compare full/one-pass Direct vs MDI at equal visual workloads.
    const std::string diagnosticPass = stress &&
        std::getenv("AIVERSE_BATCH_DIAGNOSTIC_MODEL_PASS")
        ? std::getenv("AIVERSE_BATCH_DIAGNOSTIC_MODEL_PASS") : "full";
    if (diagnosticPass != "full" && diagnosticPass != "shadow" &&
        diagnosticPass != "opaque") {
        std::fprintf(stderr, "Invalid AIVERSE_BATCH_DIAGNOSTIC_MODEL_PASS: %s\n",
                     diagnosticPass.c_str());
        return 2;
    }
    const uint8_t modelPassMask = diagnosticPass == "shadow" ? 1u
        : diagnosticPass == "opaque" ? 2u : 3u;
    // Explicit per-fixture cap tests ICB submission size without touching the
    // instance uniform capacity or the native shader specialization.
    uint32_t diagnosticMdiGroupCap = 0;
    if (stress && std::getenv("AIVERSE_BATCH_DIAGNOSTIC_MDI_GROUP_CAP")) {
        const char* raw = std::getenv("AIVERSE_BATCH_DIAGNOSTIC_MDI_GROUP_CAP");
        char* end = nullptr;
        const unsigned long n = std::strtoul(raw, &end, 10);
        if (end == raw || *end != '\0' ||
            (n != 0 && (n < 16 || n > 256))) {
            std::fprintf(stderr,
                "AIVERSE_BATCH_DIAGNOSTIC_MDI_GROUP_CAP must be 0 or 16..256\n");
            return 2;
        }
        diagnosticMdiGroupCap = uint32_t(n);
    }
    size_t stressItems = 0;
    if (stress) {
        const char* raw = std::getenv("AIVERSE_BATCH_STRESS_ITEMS");
        char* end = nullptr;
        const unsigned long parsed = raw ? std::strtoul(raw, &end, 10) : 1024ul;
        if (raw && (end == raw || *end != '\0' || parsed < 64ul ||
                    parsed > 100000ul)) {
            std::fprintf(stderr, "AIVERSE_BATCH_STRESS_ITEMS must be 64..100000\n");
            return 2;
        }
        stressItems = size_t(parsed);
    }
    // Diagnostic only: do not change the default model fixture or renderer.
    // Each original primitive remains one draw, with N*N triangles instead
    // of one; cap total triangles to avoid accidental huge stress allocations.
    uint32_t meshSubdivisions = 1;
    if (stress && std::getenv("AIVERSE_BATCH_DIAGNOSTIC_MESH_SUBDIVISIONS")) {
        const char* raw = std::getenv("AIVERSE_BATCH_DIAGNOSTIC_MESH_SUBDIVISIONS");
        char* end = nullptr;
        const unsigned long n = std::strtoul(raw, &end, 10);
        if (end == raw || *end != '\0' ||
            (n != 1 && n != 2 && n != 4 && n != 8)) {
            std::fprintf(stderr,
                "AIVERSE_BATCH_DIAGNOSTIC_MESH_SUBDIVISIONS must be 1, 2, 4 or 8\n");
            return 2;
        }
        meshSubdivisions = uint32_t(n);
    }
    if (stress && uint64_t(stressItems) * meshSubdivisions * meshSubdivisions > 1000000ull) {
        std::fprintf(stderr, "Stress fixture exceeds 1000000 triangles\n");
        return 2;
    }
    // Cold-vs-warm packet screenshot parity uses the same self-contained
    // varied-material 16-rigid-part fixture as the indirect palette test.
    auto model = scenario == "indirect" || scenario == "indirect_palette" ||
                 scenario == "static_packets" || scenario == "auto_stress"
        ? indirectFixture(scenario != "indirect" && !stress,
                          stress ? stressItems : 16,
                          meshSubdivisions) : render::loadModel(argv[1],read);
    const std::string prefix = argv[2];
    const bool reflection=std::getenv("AIVERSE_SHADOW_TEST_REFLECTION")!=nullptr;
    render::Renderer3D renderer; render::Mesh floor; gfx::Device* device = nullptr;
    std::unique_ptr<render::Model> secondModel;
    std::vector<render::Material> materials;
    std::vector<std::vector<render::MaterialBatch>> batches;
    std::vector<uint8_t> before;
    bool mdiCaptureRequested=false, mdiCaptureVerified=false;
    const bool captureMdi = std::getenv("AIVERSE_CAPTURE_SUCCESSFUL_MDI") &&
        std::string(std::getenv("AIVERSE_CAPTURE_SUCCESSFUL_MDI")) == "1";
    uint32_t shadowBefore=0, opaqueBefore=0; int frame=0; bool failed=false, finished=false;
    bool packetBuilt=false; // the warm hit must reuse a previously built packet
    // Opt-in synchronous CPU measurements; never treat these as GPU timing.
    const bool cpuTiming = std::getenv("AIVERSE_BATCH_CPU_TIMING") &&
        std::string(std::getenv("AIVERSE_BATCH_CPU_TIMING")) == "1";
    std::vector<double> cpuRenderBefore, cpuRenderAfter;
    const bool threadCpuTiming = stress &&
        std::getenv("AIVERSE_BATCH_THREAD_CPU_TIMING") &&
        std::string(std::getenv("AIVERSE_BATCH_THREAD_CPU_TIMING")) == "1";
    // Opt-in per-frame CPU timeline: buffer data in memory and write once
    // after the capture window to avoid I/O during measured render frames.
    const bool frameTimeline = stress && std::getenv("AIVERSE_BATCH_FRAME_TIMELINE") &&
        std::string(std::getenv("AIVERSE_BATCH_FRAME_TIMELINE")) == "1";
    struct FrameCpuSample {
        int index = 0;
        double renderMs = 0.0;
        double threadCpuMs = -1.0; // -1 means unsupported / clock read failed
        double processCpuMs = -1.0;
        double machThreadCpuMs = -1.0;
        uint64_t mdiSubmissions = 0;
        uint64_t mdiCommands = 0;
        uint32_t shadowDraws = 0, opaqueDraws = 0;
        double shadowCpuMs = 0.0, opaqueCpuMs = 0.0;
        double shadowBackendMs = 0.0, opaqueBackendMs = 0.0;
    };
    std::vector<FrameCpuSample> frameCpuTimeline;
    if (frameTimeline) frameCpuTimeline.reserve(128);
    // Existing RHI command-buffer GPU timestamps are asynchronous. Tag the
    // command buffer from this fixture, then collect completed tags later.
    // This is whole-frame GPU command-buffer time, NOT model-pass GPU time.
    const bool gpuTimeline = stress && std::getenv("AIVERSE_BATCH_GPU_TIMELINE") &&
        std::string(std::getenv("AIVERSE_BATCH_GPU_TIMELINE")) == "1";
    std::vector<gfx::Device::GpuTiming> gpuTimelineSamples;
    bool gpuTimingUnsupported = false;
    bool modelCountersUnsupported = false;
    if (gpuTimeline) gpuTimelineSamples.reserve(96);
    const bool modelGpuCounters = stress &&
        std::getenv("AIVERSE_BATCH_MODEL_GPU_COUNTERS") &&
        std::string(std::getenv("AIVERSE_BATCH_MODEL_GPU_COUNTERS")) == "1";
    std::vector<gfx::Device::ModelGpuCounter> modelGpuSamples;
    uint64_t firstModelGpuSerial = 0;
    if (modelGpuCounters) modelGpuSamples.reserve(220);
    // Stage 76: do not conflate AUTO exploration frames with its learned
    // Direct frames. Retain CPU samples for *actual* successful MDI frames.
    std::vector<double> stressDirectFallbackMs, stressSuccessfulMdiMs;
    std::vector<double> stressSteadyAutoMs;
    struct SuccessfulMdiCpu {
        uint64_t samples = 0, submissions = 0;
        double totalMs = 0, selectionMs = 0, groupingMs = 0;
        double scanMs = 0, packingMs = 0, pipelineMs = 0;
        double uploadMs = 0, submitMs = 0;
    };
    std::array<SuccessfulMdiCpu, 2> successfulMdiStages{};
    uint64_t stressAutoMdiScans = 0, stressAutoMdiAttempts = 0;
    uint64_t stressAutoMdiSubmitted = 0, stressAutoMdiShort = 0;
    uint64_t stressAutoMdiBelow = 0, stressAutoMdiIneligible = 0;
    uint64_t stressAutoMdiFailed = 0;
    uint64_t stressAutoGeometryBreaks = 0, stressAutoMaterialBreaks = 0;
    uint64_t stressAutoStateBreaks = 0;
    const std::string stressPolicy = stress && std::getenv("AIVERSE_BATCH_STRESS_POLICY")
        ? std::getenv("AIVERSE_BATCH_STRESS_POLICY") : "auto";
    if (stress && stressPolicy != "auto" && stressPolicy != "mdi" &&
        stressPolicy != "direct") {
        std::fprintf(stderr, "Invalid AIVERSE_BATCH_STRESS_POLICY: %s\n",
                     stressPolicy.c_str());
        return 2;
    }
    if (stress && stressPolicy == "mdi" &&
        std::getenv("AIVERSE_BATCH_AUTO") &&
        std::string(std::getenv("AIVERSE_BATCH_AUTO")) == "1") {
        std::fprintf(stderr, "Forced MDI benchmark requires AUTO disabled\n");
        return 2;
    }
    uint64_t stressMeasuredMdiFrames = 0, stressMeasuredDirectFrames = 0;
    uint64_t stressMeasuredMdiSubmissions = 0;
    const int captureBeforeFrame = stress ? 75 : 8;
    const int captureAfterFrame = stress ? 195 : 23;
    const int startBatchFrame = stress ? 90 : 15;
    const int maximumFrame = stress ? 205 : 40;
    if (stress) {
        std::printf("stress requested_items=%zu authored_unique_primitives=%zu "
                    "submitted_model_draws=1\n", stressItems, model->meshes.size());
        std::printf("stress diagnostic mesh subdivisions=%u "
                    "triangles_per_primitive=%u vertices_per_primitive=%u\n",
                    unsigned(meshSubdivisions),
                    unsigned(meshSubdivisions * meshSubdivisions),
                    unsigned(3 * meshSubdivisions * meshSubdivisions));
    }
    // Benchmark only: independent runs for CPU MDI and GPU ICB. The latter
    // may legitimately retain CPU MDI for a different render pass.
    const std::string benchmarkMode = std::getenv("AIVERSE_BATCH_BENCH_MODE")
        ? std::getenv("AIVERSE_BATCH_BENCH_MODE") : "legacy";
    if (benchmarkMode != "legacy" && benchmarkMode != "cpu_mdi" &&
        benchmarkMode != "gpu_icb") {
        std::fprintf(stderr, "Invalid AIVERSE_BATCH_BENCH_MODE: %s\n",
                     benchmarkMode.c_str());
        return 2;
    }
    platform::AppConfig cfg; cfg.title="Material palette A/B"; cfg.width=960; cfg.height=640; cfg.highDpi=false;
    platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& d) {
        device=&d; if (!renderer.init(d) || !render::uploadModel(d,*model)) { failed=true; return; }
        if (modelGpuCounters && !d.enableModelGpuCounters(true)) {
            std::fprintf(stderr,
                "GPU model-pass draw-boundary timestamp counters UNSUPPORTED "
                "(Metal capability; no samples recorded)\n");
            modelCountersUnsupported = true;
            failed = true;
            return;
        }
        if (scenario == "indirect") {
            secondModel = indirectFixture();
            if (!render::uploadModel(d, *secondModel)) { failed=true; return; }
            if (!model->geometryPoolPage || !secondModel->geometryPoolPage ||
                !(model->vb == secondModel->vb)) { failed=true; return; }
        }
        // AUTO is a complete CPU-path opt-in on Metal. Failing this capability
        // gate used to silently prevent all MDI probes and poison learning.
        if (stress && std::getenv("AIVERSE_BATCH_AUTO") &&
            std::string(std::getenv("AIVERSE_BATCH_AUTO")) == "1" &&
            d.info().backend == gfx::Backend::Metal &&
            !d.supportsMultiDrawIndirect()) {
            std::fprintf(stderr,
                "AUTO stress: Metal CPU MDI capability unavailable despite explicit AUTO opt-in\n");
            failed = true;
            return;
        }
        renderer.setDiagnosticModelPassMask(modelPassMask);
        renderer.setDiagnosticCpuMdiGroupCap(diagnosticMdiGroupCap);
        if (stress) std::printf("stress diagnostic CPU MDI group cap=%u\n",
                                unsigned(diagnosticMdiGroupCap));
        if (stress) std::printf("stress diagnostic model pass=%s mask=%u\n",
                                diagnosticPass.c_str(), unsigned(modelPassMask));
        renderer.setProfiling(true); renderer.setShadowQuality(render::ShadowQuality::High);
        renderer.lighting.fogDensity=0;
        if (const char* q=std::getenv("AIVERSE_PALETTE_TEST_QUALITY")) renderer.setMaterialQuality(render::MaterialQuality(std::atoi(q)));
        renderer.lights.setQuality(render::LightQuality::High);
        floor=render::MeshBuilder().box({0,-.1f,0},{60,.2f,60}).upload(d);
        materials=model->materials; // Match RtsView's per-type material-copy path.
        if (scenario=="overrides") for(size_t i=0;i<materials.size();++i) {
            materials[i].baseColor.x*=.731234f; materials[i].metallic=float(i%3)*.27913f;
            materials[i].roughness=.1213f+float(i%4)*.174321f;
        }
        if (scenario=="fallback") for(size_t i=0;i<materials.size();++i) materials[i].uvOffset.x=float(i)*.137f;
        if (scenario=="lod1" || scenario=="lod2") for(auto& n:model->nodes) if(n.mesh>=0) {
            const auto& lods=model->meshes[size_t(n.mesh)].lods;
            if(!lods.empty()) n.mesh=lods[scenario=="lod2"?lods.size()-1:0].mesh;
        }
        for (const auto& mesh:model->meshes) batches.push_back(mesh.materialBatches);
    };
    cb.frame=[&](gfx::Device& d,double) {
        if (modelGpuCounters) {
            if (!firstModelGpuSerial) firstModelGpuSerial = d.frameSerial();
            auto completed = d.takeModelGpuCounters();
            modelGpuSamples.insert(modelGpuSamples.end(),
                                   completed.begin(), completed.end());
        }
        if (gpuTimeline) {
            auto completed = d.takeGpuTimings();
            gpuTimelineSamples.insert(gpuTimelineSamples.end(),
                                      completed.begin(), completed.end());
        }
        if(failed||finished||frame>maximumFrame){platform::requestQuit();return;}
        // Stop tagging early enough for GPU completion results to be polled
        // over the remaining fixture frames without a blocking GPU wait.
        if (gpuTimeline && frame >= 115 && frame <= 180 &&
            !d.timeGpuFrame(uint64_t(frame)))
            gpuTimingUnsupported = true;
        const bool batch=frame>=startBatchFrame;
        // In cpu_mdi mode explicitly exclude GPU-authored draws, regardless
        // of process flags. In gpu_icb mode the original real GPU ICB path is
        // allowed, with separate CPU MDI counters for any remaining passes.
        if (benchmarkMode != "legacy")
            renderer.setGpuAuthoredDrawEnabled(batch && benchmarkMode == "gpu_icb");
        renderer.setModelMultiDrawEnabled(scenario == "static_packets"
            ? false : ((scenario != "indirect" &&
                        scenario != "indirect_palette" &&
                        scenario != "auto_stress") || batch) &&
                (!stress || !batch || stressPolicy != "direct"));
        // Keep CPU packet primitive source pointers stable for the lifetime of
        // the fixture. Other scenarios deliberately toggle material batches.
        if (scenario != "static_packets" && !stress)
            for(size_t i=0;i<model->meshes.size();++i)
                model->meshes[i].materialBatches=(batch && scenario != "indirect" &&
                                                   scenario != "indirect_palette")
                    ? batches[i] : std::vector<render::MaterialBatch>{};
        render::RTSCamera cam; cam.target={0,2,0};cam.distance=std::getenv("AIVERSE_SHADOW_TEST_DISTANCE")?float(std::atof(std::getenv("AIVERSE_SHADOW_TEST_DISTANCE"))):14;cam.pitch=.85f;cam.yaw=.45f;
        renderer.beginScene(cam,960,640);
        renderer.drawMesh(floor,{}, {.6f,.6f,.6f,1});
        if(reflection) renderer.drawMesh(floor,render::mat4::translate({0,.2f,0}),{.1f,.2f,.3f,.6f},render::kDrawNoCastShadow,.9f);
        render::ModelDraw draw;draw.model=model.get();draw.materials=&materials;
        // This is also a GPU-authored draw fixture. The real renderer only
        // puts static (or explicitly allowed rigid) instances into the
        // compute-generated ICB path. Without this flag a green test would
        // exercise CPU MDI only, regardless of AIVERSE_GPU_ENCODED_ICB.
        if (scenario == "indirect" || scenario == "static_packets" || stress)
            draw.flags |= render::kDrawStatic;
        if (scenario == "static_packets") {
            draw.persistentId = 0x12345;
            // Frames 0..14 are genuinely uncached; the second image is
            // captured after 8 warm cache replays with identical geometry.
            draw.staticPacketRevision = batch ? 1 : 0;
        }
        const auto center=(model->boundsMin+model->boundsMax)*.5f;
        draw.world=render::mat4::scale({.25f,.25f,.25f})*render::mat4::translate({-center.x,-model->boundsMin.y,-center.z});
        if (scenario=="effects") {draw.dissolve=.18f;draw.outline={1,.2f,0,.025f};}
        if (scenario=="transition") for(size_t i=0;i<model->nodes.size();++i) {
            const auto& n=model->nodes[i];if(n.mesh<0)continue;
            auto& mesh=model->meshes[size_t(n.mesh)];if(mesh.lods.empty())continue;
            const auto xf=draw.world*model->bindWorld[i];const float error=render::length(mesh.boundsMax-mesh.boundsMin);
            float pixels=render::geometryErrorPixels(renderer.viewProj()*xf,mesh.boundsMin,mesh.boundsMax,error,960,640,d.info().clipDepthZeroToOne);
            pixels=std::max(pixels,render::geometryErrorPixels(renderer.lightViewProj()*xf,mesh.boundsMin,mesh.boundsMax,error,renderer.shadowMapSize(),renderer.shadowMapSize(),d.info().clipDepthZeroToOne));
            mesh.lods.resize(1);mesh.lods[0].maxPixels=pixels;renderer.setModelLodFadeWidth(.15f);
        }
        renderer.drawModel(draw);
        if (scenario == "indirect") {
            render::ModelDraw other = draw;
            other.model = secondModel.get();
            other.materials = &secondModel->materials; // distinct Material* with identical shader/parameters
            other.world = render::mat4::translate({4.f, 0, 0}) * draw.world;
            renderer.drawModel(other);
        }
        if(scenario=="instanced"){draw.world=render::mat4::translate({3,0,0})*draw.world;renderer.drawModel(draw);}
        gfx::PassDesc pass;
        const auto startCpu = std::chrono::steady_clock::now();
        uint64_t threadCpuStartNs = 0, threadCpuEndNs = 0;
        uint64_t processCpuStartNs = 0, processCpuEndNs = 0;
        uint64_t machCpuStartNs = 0, machCpuEndNs = 0;
        const bool threadStartOk = threadCpuTiming &&
            cpuClockNs(false, threadCpuStartNs);
        const bool processStartOk = threadCpuTiming &&
            cpuClockNs(true, processCpuStartNs);
#if defined(__APPLE__)
        const bool machStartOk = threadCpuTiming &&
            machCurrentThreadCpuNs(machCpuStartNs);
#else
        const bool machStartOk = false;
#endif
        renderer.render(pass);
        const bool threadEndOk = threadStartOk &&
            cpuClockNs(false, threadCpuEndNs);
        const bool processEndOk = processStartOk &&
            cpuClockNs(true, processCpuEndNs);
#if defined(__APPLE__)
        const bool machEndOk = machStartOk &&
            machCurrentThreadCpuNs(machCpuEndNs);
#else
        const bool machEndOk = false;
#endif
        const auto endCpu = std::chrono::steady_clock::now();
        const double threadCpuMs = threadEndOk && threadCpuEndNs >= threadCpuStartNs
            ? double(threadCpuEndNs - threadCpuStartNs) * 1e-6 : -1.0;
        const double processCpuMs = processEndOk && processCpuEndNs >= processCpuStartNs
            ? double(processCpuEndNs - processCpuStartNs) * 1e-6 : -1.0;
        const double machThreadCpuMs = machEndOk && machCpuEndNs >= machCpuStartNs
            ? double(machCpuEndNs - machCpuStartNs) * 1e-6 : -1.0;
        if (frameTimeline && batch && frame <= captureAfterFrame) {
            const auto& stats = renderer.modelStats();
            const auto& profile = renderer.profile();
            frameCpuTimeline.push_back({
                frame,
                std::chrono::duration<double, std::milli>(endCpu-startCpu).count(),
                threadCpuMs, processCpuMs, machThreadCpuMs,
                stats.multiDrawSubmissions, stats.indirectCommands,
                profile.draws[0], profile.draws[1],
                stats.mdiCpuStages[0].totalMs, stats.mdiCpuStages[1].totalMs,
                stats.mdiCpuStages[0].backendSubmitMs,
                stats.mdiCpuStages[1].backendSubmitMs
            });
        }
        if (stress && batch && captureMdi && frame >= 100 &&
            !mdiCaptureRequested && renderer.modelStats().multiDrawSubmissions > 0) {
            // Capture a frame with a *real* CPU MDI submission rather than
            // comparing baseline against the final AUTO Direct fallback.
            mdiCaptureRequested = true;
            d.captureNextFrame([&, capturedFrame=frame](int w, int h,
                                                         std::vector<uint8_t> px) {
                if (w != 960 || h != 640 || before.size() != px.size()) {
                    std::fprintf(stderr, "MDI pixel capture invalid size at frame=%d\n",
                                 capturedFrame);
                    failed = true;
                    return;
                }
                size_t changed = 0;
                for (size_t i = 0; i < px.size(); ++i)
                    changed += px[i] != before[i];
                std::printf("actual MDI pixel parity frame=%d changed_channels=%zu\n",
                            capturedFrame, changed);
                if (changed) failed = true;
                std::ofstream output(prefix + "-mdi-after.ppm", std::ios::binary);
                output << "P6\n" << w << " " << h << "\n255\n";
                for (size_t i = 0; i < px.size(); i += 4)
                    output.write(reinterpret_cast<const char*>(px.data()+i), 3);
                if (!output) failed = true;
                mdiCaptureVerified = true;
            });
        }
        if (stress && batch && cpuTiming && frame >= 100 &&
            frame <= captureAfterFrame) {
            const auto& st = renderer.modelStats();
            const double ms = std::chrono::duration<double, std::milli>(
                endCpu - startCpu).count();
            if (st.multiDrawSubmissions > 0) {
                stressSuccessfulMdiMs.push_back(ms);
                for (size_t phase = 0; phase < successfulMdiStages.size(); ++phase) {
                    const auto& sample = st.mdiCpuStages[phase];
                    if (!sample.submitted) continue;
                    auto& total = successfulMdiStages[phase];
                    ++total.samples;
                    total.submissions += sample.submitted;
                    total.totalMs += sample.totalMs;
                    total.selectionMs += sample.selectionMs;
                    total.groupingMs += sample.groupingSortMs;
                    total.scanMs += sample.candidateScanMs;
                    total.packingMs += sample.commandPackingMs;
                    total.pipelineMs += sample.pipelineSetupMs;
                    total.uploadMs += sample.bufferUploadMs;
                    total.submitMs += sample.backendSubmitMs;
                }
            } else {
                stressDirectFallbackMs.push_back(ms);
            }
            // Late AUTO frames, clearly separate from exploratory warmup.
            if (frame >= 160) stressSteadyAutoMs.push_back(ms);
        }
        if (stress && batch && frame >= 115 && frame <= captureAfterFrame) {
            const auto& stats = renderer.modelStats();
            if (stats.multiDrawSubmissions) {
                ++stressMeasuredMdiFrames;
                stressMeasuredMdiSubmissions += stats.multiDrawSubmissions;
            } else ++stressMeasuredDirectFrames;
        }
        if (stress && batch) {
            const auto& st = renderer.modelStats();
            stressAutoMdiScans += st.mdiScans;
            stressAutoMdiAttempts += st.mdiCpuStages[0].indirectAttempts +
                                     st.mdiCpuStages[1].indirectAttempts;
            stressAutoMdiSubmitted += st.multiDrawSubmissions;
            stressAutoMdiShort += st.mdiShort;
            stressAutoMdiBelow += st.mdiBelowThreshold;
            stressAutoMdiIneligible += st.mdiIneligible;
            stressAutoMdiFailed += st.mdiFailed;
            stressAutoGeometryBreaks += st.mdiGeometryBreaks;
            stressAutoMaterialBreaks += st.mdiMaterialBreaks;
            stressAutoStateBreaks += st.mdiStateBreaks;
        }
        // Discard pipeline warmup and the transition frame; no readback in sample.
        if (cpuTiming && (stress
                ? ((frame >= 10 && frame <= captureBeforeFrame) ||
                   (frame >= 115 && frame <= captureAfterFrame))
                : ((frame >= 3 && frame <= 8) ||
                   (frame >= 18 && frame <= 23)))) {
            const double ms = std::chrono::duration<double, std::milli>(
                endCpu - startCpu).count();
            (batch ? cpuRenderAfter : cpuRenderBefore).push_back(ms);
        }
        if (scenario == "static_packets" && frame == 15) {
            const auto stats = renderer.staticRenderPacketStats();
            packetBuilt = stats.rebuilds == 1 && stats.hits == 0 &&
                          stats.live == 1 && stats.retainedBytes > 0;
            if (!packetBuilt) {
                std::fprintf(stderr,
                    "static packet fixture failed to build a cold packet "
                    "(rebuilt=%llu hits=%llu live=%zu bytes=%zu)\n",
                    (unsigned long long)stats.rebuilds,
                    (unsigned long long)stats.hits,
                    stats.live, stats.retainedBytes);
                failed = true;
            }
        }
        if (cpuTiming && frame == captureAfterFrame) {
            const auto report = [](const char* phase, std::vector<double> samples) {
                std::sort(samples.begin(), samples.end());
                if (samples.empty()) return;
                const size_t n = samples.size();
                const double median = (n % 2) ? samples[n / 2]
                    : (samples[n / 2 - 1] + samples[n / 2]) * 0.5;
                const size_t p95Index = (95 * n + 99) / 100 - 1;
                std::printf("CPU render benchmark %s: samples=%zu median_ms=%.4f p95_ms=%.4f min_ms=%.4f max_ms=%.4f\n",
                    phase, n, median, samples[p95Index], samples.front(), samples.back());
            };
            report("direct", cpuRenderBefore);
            report(benchmarkMode == "cpu_mdi" ? "cpu_mdi" :
                   benchmarkMode == "gpu_icb" ? "gpu_icb_mixed" :
                   "batched", cpuRenderAfter);
            std::printf("CPU render benchmark: synchronous renderer.render() only; not GPU time or FPS\n");
            if (stress) {
                std::printf("stress benchmark policy=%s measured_mdi_frames=%llu "
                            "measured_direct_frames=%llu measured_mdi_submissions=%llu\n",
                    stressPolicy.c_str(),
                    (unsigned long long)stressMeasuredMdiFrames,
                    (unsigned long long)stressMeasuredDirectFrames,
                    (unsigned long long)stressMeasuredMdiSubmissions);
                if (stressPolicy == "mdi" && !stressMeasuredMdiFrames) {
                    std::fprintf(stderr, "forced MDI failed: zero submitted frames\n");
                    failed = true;
                }
                if (stressPolicy == "direct" && stressMeasuredMdiFrames) {
                    std::fprintf(stderr, "forced Direct failed: MDI submitted\n");
                    failed = true;
                }
                report("auto_direct_frames", stressDirectFallbackMs);
                report("auto_mdi_submitted_frames", stressSuccessfulMdiMs);
                report("auto_late_frames", stressSteadyAutoMs);
                for (size_t phase = 0; phase < successfulMdiStages.size(); ++phase) {
                    const auto& x = successfulMdiStages[phase];
                    if (!x.samples) continue;
                    const double scale = 1.0 / double(x.samples);
                    std::printf("successful MDI CPU stage pass=%zu samples=%llu "
                                "submissions=%llu mean_total_ms=%.5f "
                                "selection_ms=%.5f grouping_ms=%.5f scan_ms=%.5f "
                                "packing_ms=%.5f pipeline_ms=%.5f "
                                "upload_ms=%.5f submit_ms=%.5f\n",
                        phase, (unsigned long long)x.samples,
                        (unsigned long long)x.submissions, x.totalMs * scale,
                        x.selectionMs * scale, x.groupingMs * scale,
                        x.scanMs * scale, x.packingMs * scale,
                        x.pipelineMs * scale, x.uploadMs * scale,
                        x.submitMs * scale);
                }
                std::printf("AUTO stress timing note: exploratory frame modes "
                            "are not randomized A/B/A, GPU time remains unknown\n");
            }

            if (std::getenv("AIVERSE_BATCH_AUTO") &&
                std::string(std::getenv("AIVERSE_BATCH_AUTO")) == "1") {
                const auto& learned = renderer.autoCpuBatchStats();
                for (size_t phase = 0; phase < 2; ++phase) {
                    for (size_t bucket = 0; bucket < 3; ++bucket) {
                        const auto& lane = learned.lanes[phase][bucket];
                        if (!lane.opportunities) continue;
                        std::printf("AUTO CPU learning phase=%zu bucket=%zu direct=%u %.5fms "
                                    "mdi=%u %.5fms opportunities=%u\n",
                            phase, bucket, lane.directSamples, lane.directMs,
                            lane.mdiSamples, lane.mdiMs, lane.opportunities);
                    }
                }
            }

        }
        if (frameTimeline && frame == captureAfterFrame) {
            const std::string path = prefix + "-frames.csv";
            std::ofstream csv(path, std::ios::binary);
            csv << "frame,render_cpu_ms,mdi_submissions,mdi_commands,"
                   "shadow_draws,opaque_draws,shadow_cpu_ms,opaque_cpu_ms,"
                   "shadow_backend_ms,opaque_backend_ms";
            if (threadCpuTiming)
                csv << ",render_thread_cpu_ms,render_offcpu_ms,"
                       "render_process_cpu_ms,render_mach_thread_cpu_ms";
            csv << '\n';
            for (const auto& sample : frameCpuTimeline) {
                csv << sample.index << ',' << sample.renderMs << ','
                    << sample.mdiSubmissions << ',' << sample.mdiCommands << ','
                    << sample.shadowDraws << ',' << sample.opaqueDraws << ','
                    << sample.shadowCpuMs << ',' << sample.opaqueCpuMs << ','
                    << sample.shadowBackendMs << ',' << sample.opaqueBackendMs;
                if (threadCpuTiming) {
                    // On macOS the native Mach thread accounting is the
                    // cross-checked source. Keep the POSIX clock separately
                    // for diagnosing its observed ~1/42 disagreement.
#if defined(__APPLE__)
                    const double trustedThreadMs = sample.machThreadCpuMs;
#else
                    const double trustedThreadMs = sample.threadCpuMs;
#endif
                    csv << ',' << sample.threadCpuMs << ','
                        << (trustedThreadMs >= 0.0
                            ? std::max(0.0, sample.renderMs - trustedThreadMs)
                            : -1.0)
                        << ',' << sample.processCpuMs
                        << ',' << sample.machThreadCpuMs;
                }
                csv << '\n';
            }
            csv.close();
            if (!csv) {
                std::fprintf(stderr, "stress frame timeline CSV write failed: %s\n",
                             path.c_str());
                failed = true;
            } else {
                std::printf("stress frame timeline samples=%zu path=%s\n",
                            frameCpuTimeline.size(), path.c_str());
            }
        }
        if (stress && frame == captureAfterFrame && captureMdi &&
            (!mdiCaptureRequested || !mdiCaptureVerified)) {
            std::fprintf(stderr, "actual MDI parity capture missing at AUTO end\n");
            failed = true;
        }
        if(frame==captureBeforeFrame||frame==captureAfterFrame){
            const auto& p=renderer.profile();
            std::printf("batch=%d shadow=%u opaque=%u total=%u error=%s\n",batch,p.draws[0],p.draws[1],renderer.drawCalls(),renderer.modelError().c_str());
            if(!batch){shadowBefore=p.draws[0];opaqueBefore=p.draws[1];}
            else if (scenario == "static_packets") {
                const auto stats = renderer.staticRenderPacketStats();
                std::printf("static packet A/B: hits=%llu rebuilds=%llu declines=%llu bytes=%zu\n",
                    (unsigned long long)stats.hits,
                    (unsigned long long)stats.rebuilds,
                    (unsigned long long)stats.declined,
                    stats.retainedBytes);
                if (!packetBuilt || stats.hits != 1 || stats.rebuilds != 0 ||
                    stats.live != 1 || stats.retainedBytes == 0 ||
                    p.draws[0] != shadowBefore || p.draws[1] != opaqueBefore)
                    failed = true;
            }
            else if (scenario == "indirect_palette") {
                const bool supported = d.supportsMultiDrawIndirect();
                const bool requested = std::getenv("AIVERSE_MDI_INSTANCE_PALETTE") &&
                    std::string(std::getenv("AIVERSE_MDI_INSTANCE_PALETTE")) == "1";
                const auto& stats = renderer.modelStats();
                std::printf("indirect palette: supported=%d requested=%d calls %u -> %u, palette submissions=%llu commands=%llu\n",
                            int(supported), int(requested), opaqueBefore, p.draws[1],
                            (unsigned long long)stats.mdiPaletteSubmissions,
                            (unsigned long long)stats.mdiPaletteCommands);
                if (supported && requested) {
                    if (p.draws[1] >= opaqueBefore || !stats.mdiPaletteSubmissions ||
                        stats.mdiPaletteCommands < 16) failed = true;
                } else if (p.draws[1] != opaqueBefore) failed = true;
                if (p.draws[0] != shadowBefore) failed = true;
            }
            else if (stress) {
                const auto gpu = renderer.gpuModelDrawStats();
                const auto cpu = renderer.modelStats();
                std::printf("stress phase=%s model_draw_calls=%u "
                            "cpu_mdi_submissions=%llu cpu_mdi_commands=%llu "
                            "gpu_icb_submissions=%u gpu_icb_commands=%u\n",
                    batch ? "auto" : "direct", p.draws[0] + p.draws[1],
                    (unsigned long long)cpu.multiDrawSubmissions,
                    (unsigned long long)cpu.indirectCommands,
                    gpu.gpuEncodedSubmissions, gpu.gpuEncodedCommands);
                std::printf("stress AUTO cumulative: scans=%llu attempts=%llu "
                            "submitted=%llu short=%llu below=%llu ineligible=%llu "
                            "failed=%llu geometryBreaks=%llu materialBreaks=%llu "
                            "stateBreaks=%llu\n",
                    (unsigned long long)stressAutoMdiScans,
                    (unsigned long long)stressAutoMdiAttempts,
                    (unsigned long long)stressAutoMdiSubmitted,
                    (unsigned long long)stressAutoMdiShort,
                    (unsigned long long)stressAutoMdiBelow,
                    (unsigned long long)stressAutoMdiIneligible,
                    (unsigned long long)stressAutoMdiFailed,
                    (unsigned long long)stressAutoGeometryBreaks,
                    (unsigned long long)stressAutoMaterialBreaks,
                    (unsigned long long)stressAutoStateBreaks);
                std::printf("stress MDI decisions: scans=%llu short=%llu below=%llu "
                            "ineligible=%llu failed=%llu geometryBreaks=%llu "
                            "materialBreaks=%llu stateBreaks=%llu\n",
                    (unsigned long long)cpu.mdiScans,
                    (unsigned long long)cpu.mdiShort,
                    (unsigned long long)cpu.mdiBelowThreshold,
                    (unsigned long long)cpu.mdiIneligible,
                    (unsigned long long)cpu.mdiFailed,
                    (unsigned long long)cpu.mdiGeometryBreaks,
                    (unsigned long long)cpu.mdiMaterialBreaks,
                    (unsigned long long)cpu.mdiStateBreaks);
                if (cpuTiming)
                    for (size_t phase = 0; phase < 2; ++phase) {
                        const auto& st = cpu.mdiCpuStages[phase];
                        std::printf("stress MDI pass=%zu scans=%llu attempts=%llu "
                                    "submitted=%llu warmups=%llu threshold=%llu "
                                    "fallbacks=%llu total=%.5fms "
                                    "grouping=%.5fms packing=%.5fms "
                                    "pipeline=%.5fms submit=%.5fms\n",
                            phase, (unsigned long long)st.candidateScans,
                            (unsigned long long)st.indirectAttempts,
                            (unsigned long long)st.submitted,
                            (unsigned long long)st.pipelineWarmups,
                            (unsigned long long)st.underThreshold,
                            (unsigned long long)st.backendFallbacks,
                            st.totalMs, st.groupingSortMs, st.commandPackingMs,
                            st.pipelineSetupMs, st.backendSubmitMs);
                    }
                // AUTO can correctly choose any valid renderer path.
                // Pixel parity and exit status remain mandatory below.
            }
            else if (scenario == "indirect") {
                const bool supported = d.supportsMultiDrawIndirect();
                // Traditional Metal ICB accepts depth-only/vertex-buffer shaders,
                // but PBR colour fragment shaders with direct textures cannot
                // become ICB pipelines without converting textures to argument
                // buffers. Validate the actual shadow + colour submission total.
                const bool expectGpu = std::getenv("AIVERSE_EXPECT_GPU_ICB") &&
                                        std::string(std::getenv("AIVERSE_EXPECT_GPU_ICB")) == "1";
                const auto gpuDraw = renderer.gpuModelDrawStats();
                const uint64_t indirectCommands = renderer.modelStats().indirectCommands;
                if (benchmarkMode == "cpu_mdi" &&
                    (gpuDraw.gpuEncodedSubmissions || gpuDraw.gpuEncodedCommands ||
                     !renderer.modelStats().multiDrawSubmissions)) {
                    std::fprintf(stderr,
                        "CPU MDI benchmark isolation failed: gpu=%u/%u cpu=%llu\n",
                        gpuDraw.gpuEncodedSubmissions, gpuDraw.gpuEncodedCommands,
                        (unsigned long long)renderer.modelStats().multiDrawSubmissions);
                    failed = true;
                }
                if (benchmarkMode == "gpu_icb" &&
                    (!gpuDraw.gpuEncodedSubmissions || !gpuDraw.gpuEncodedCommands)) {
                    std::fprintf(stderr, "GPU ICB benchmark did not submit GPU commands\n");
                    failed = true;
                }
                // Separate GPU-compute-authored ICB from CPU-encoded MDI.
                // The two paths can both contribute to a single model frame
                // (e.g. different shadow/PBR passes), so the aggregate CPU
                // indirect counters alone cannot establish GPU ICB execution.
                std::printf("GPU-encoded ICB: supported=%d required=%d submissions=%u commands=%u\n",
                            int(d.supportsGpuEncodedIcb()), int(expectGpu),
                            gpuDraw.gpuEncodedSubmissions, gpuDraw.gpuEncodedCommands);
                // Report eligibility/preflight separately from actual GPU execution.
                // Nonzero candidates alone are never evidence of GPU-authored draws.
                std::printf("GPU ICB diagnostics: candidates=%u prepared=%u rejected=%u "
                            "executeFailed=%u preflightGroups=%u\n",
                            gpuDraw.gpuIcbCandidates, gpuDraw.gpuIcbPrepared,
                            gpuDraw.gpuIcbRejected, gpuDraw.gpuIcbExecuteFailed,
                            gpuDraw.preflightGroups);
                // CPU-encoded MDI and GPU-encoded ICB account for separate
                // command counters. Do not reject a successful GPU ICB draw
                // merely because the CPU indirect-command counter is zero.
                if (expectGpu && !d.supportsGpuEncodedIcb()) {
                    std::fprintf(stderr,
                        "[metal-gpu-icb] GPU-encoded ICB is unavailable on the CI device; cannot claim GPU validation\n");
                    failed = true;
                }
                if (expectGpu && d.supportsGpuEncodedIcb()) {
                    if (!gpuDraw.gpuEncodedSubmissions ||
                        !gpuDraw.gpuEncodedCommands ||
                        uint64_t(p.draws[0]) + p.draws[1] >=
                            uint64_t(shadowBefore) + opaqueBefore) {
                        std::fprintf(stderr,
                            "[metal-gpu-icb] expected actual GPU submissions, got submissions=%u commands=%u shadow=%u opaque=%u\n",
                            gpuDraw.gpuEncodedSubmissions, gpuDraw.gpuEncodedCommands,
                            p.draws[0], p.draws[1]);
                        failed = true;
                    }
                } else if (std::getenv("AIVERSE_BATCH_AUTO") &&
                           std::string(std::getenv("AIVERSE_BATCH_AUTO")) == "1" &&
                           benchmarkMode == "legacy") {
                    // AUTO intentionally keeps this tiny scene on direct draws.
                    // Reject accidental indirect submissions, not missing savings.
                    const auto stats = renderer.modelStats();
                    if (stats.multiDrawSubmissions || gpuDraw.gpuEncodedSubmissions ||
                        p.draws[0] != shadowBefore ||
                        p.draws[1] != opaqueBefore) {
                        std::fprintf(stderr,
                            "AUTO small-direct isolation failed: cpu=%llu gpu=%u shadow=%u opaque=%u\n",
                            (unsigned long long)stats.multiDrawSubmissions,
                            gpuDraw.gpuEncodedSubmissions, p.draws[0], p.draws[1]);
                        failed = true;
                    }
                    std::printf("AUTO path: direct/instancing (small eligible scene)\n");
                } else if (supported) {
                    if (uint64_t(p.draws[0]) + p.draws[1] >=
                            uint64_t(shadowBefore) + opaqueBefore ||
                        indirectCommands < 8) failed = true;
                } else if (p.draws[1] != opaqueBefore ||
                           p.draws[0] != shadowBefore) {
                    failed = true;
                }
                const bool demandPbr = std::getenv("AIVERSE_EXPECT_PBR_ICB") &&
                                       std::string(std::getenv("AIVERSE_EXPECT_PBR_ICB")) == "1";
                if (demandPbr && supported && p.draws[1] >= opaqueBefore) {
                    std::fprintf(stderr, "[metal-icb] PBR colour pass did not enter indirect rendering\n");
                    failed = true;
                }
                std::printf("PBR colour indirect active=%d (before %u, after %u)\n",
                            int(p.draws[1] < opaqueBefore), opaqueBefore, p.draws[1]);
                std::printf("CPU MDI: supported=%d submissions=%llu commands=%llu\n",
                            int(supported),
                            (unsigned long long)renderer.modelStats().multiDrawSubmissions,
                            (unsigned long long)renderer.modelStats().indirectCommands);
                if (cpuTiming) {
                    for (size_t pass = 0; pass < 2; ++pass) {
                        const auto& st = renderer.modelStats().mdiCpuStages[pass];
                        std::printf("CPU MDI stages pass=%zu samples=%llu scans=%llu attempts=%llu submitted=%llu "
                                    "total_ms=%.5f selection_ms=%.5f grouping_ms=%.5f scan_ms=%.5f "
                                    "packing_ms=%.5f pipeline_ms=%.5f upload_ms=%.5f submit_ms=%.5f\n",
                            pass, (unsigned long long)st.passes,
                            (unsigned long long)st.candidateScans,
                            (unsigned long long)st.indirectAttempts,
                            (unsigned long long)st.submitted, st.totalMs,
                            st.selectionMs, st.groupingSortMs, st.candidateScanMs,
                            st.commandPackingMs, st.pipelineSetupMs,
                            st.bufferUploadMs, st.backendSubmitMs);
                    }
                }
            }
            else if(p.draws[0]!=shadowBefore || (scenario=="fallback"?p.draws[1]!=opaqueBefore:p.draws[1]>=opaqueBefore)) failed=true;
            if(scenario=="transition") {std::printf("lod_transition_nodes=%u\n",p.lodTransitionNodes);failed|=p.lodTransitionNodes==0;}
            d.captureNextFrame([&,batch](int w,int h,std::vector<uint8_t> px){
                if(w!=960||h!=640||px.size()!=size_t(w*h*4)){failed=true;return;}
                std::ofstream f(prefix+(batch?"-after.ppm":"-before.ppm"),std::ios::binary);f<<"P6\n"<<w<<" "<<h<<"\n255\n";
                for(size_t i=0;i<px.size();i+=4) f.write(reinterpret_cast<const char*>(px.data()+i),3);
                if(!f)failed=true;
                if(!batch)before=std::move(px);
                else {size_t changed=0;for(size_t i=0;i<px.size();++i)changed+=i>=before.size()||px[i]!=before[i];
                    std::printf("changed_channels=%zu\n",changed);
                    if (scenario == "indirect" && changed) {
                        size_t changedPixels=0, beforeBright=0, afterBright=0;
                        uint8_t beforeMin=255, beforeMax=0, afterMin=255, afterMax=0;
                        for (size_t j=0;j+3<px.size();j+=4) {
                            bool differs=false;
                            for (int k=0;k<3;++k) {
                                const uint8_t a=before[j+size_t(k)],b=px[j+size_t(k)];
                                differs |= a!=b;
                                beforeMin=std::min(beforeMin,a);beforeMax=std::max(beforeMax,a);
                                afterMin=std::min(afterMin,b);afterMax=std::max(afterMax,b);
                                beforeBright+=a;afterBright+=b;
                            }
                            changedPixels+=differs;
                        }
                        std::printf("indirect mismatch: pixels=%zu beforeRGB=%u..%u afterRGB=%u..%u beforeSum=%zu afterSum=%zu\n",
                                    changedPixels,unsigned(beforeMin),unsigned(beforeMax),
                                    unsigned(afterMin),unsigned(afterMax),beforeBright,afterBright);
                        for (size_t i=0;i<5;++i) {
                            const size_t pos=(i==0?0:i==1?px.size()/8:i==2?px.size()/2:i==3?px.size()*3/4:px.size()-4)&~size_t(3);
                            std::printf("sample[%zu] (%u,%u,%u) -> (%u,%u,%u)\n",i,
                                unsigned(before[pos]),unsigned(before[pos+1]),unsigned(before[pos+2]),
                                unsigned(px[pos]),unsigned(px[pos+1]),unsigned(px[pos+2]));
                        }
                    }
                    failed|=changed!=0;finished=true;}
            });
        }
        failed|=!renderer.modelError().empty();d.endPass();++frame;
    };
    cb.shutdown=[&]{
        // The fixture exits cleanly with a distinct unsupported code instead
        // of emitting misleading "0/66" secondary GPU timeline failures.
        if (modelGpuCounters && !modelCountersUnsupported) {
            auto completed = device->takeModelGpuCounters();
            modelGpuSamples.insert(modelGpuSamples.end(),
                                   completed.begin(), completed.end());
            std::sort(modelGpuSamples.begin(), modelGpuSamples.end(),
                      [](const auto& a, const auto& b) { return a.frame < b.frame; });
            std::ofstream out(prefix + "-model-gpu-counters.csv");
            out << "frame,shadow_gpu_ticks,opaque_gpu_ticks,valid\n";
            size_t validCount = 0;
            uint64_t lastFrame = 0;
            for (const auto& entry : modelGpuSamples) {
                if (!firstModelGpuSerial || entry.frame < firstModelGpuSerial)
                    continue;
                const uint64_t logicalFrame = entry.frame - firstModelGpuSerial;
                if (logicalFrame < 115 || logicalFrame > 180) continue;
                if (lastFrame && logicalFrame <= lastFrame) {
                    failed = true;
                    continue;
                }
                lastFrame = logicalFrame;
                if (entry.valid && entry.shadowTicks > 0 && entry.opaqueTicks > 0)
                    ++validCount;
                else failed = true;
                out << logicalFrame << ',' << entry.shadowTicks << ','
                    << entry.opaqueTicks << ',' << (entry.valid ? 1 : 0) << '\n';
            }
            out.close();
            if (!out || validCount != 66) {
                std::fprintf(stderr,
                    "GPU model counter timeline INVALID: valid=%zu expected=66\n",
                    validCount);
                failed = true;
            } else {
                std::printf("GPU model counter timeline samples=%zu path=%s\n",
                            validCount, (prefix + "-model-gpu-counters.csv").c_str());
            }
        }
        if (gpuTimeline && !modelCountersUnsupported) {
            auto completed = device->takeGpuTimings();
            gpuTimelineSamples.insert(gpuTimelineSamples.end(),
                                      completed.begin(), completed.end());
            std::sort(gpuTimelineSamples.begin(), gpuTimelineSamples.end(),
                      [](const auto& x, const auto& y) { return x.tag < y.tag; });
            const std::string path = prefix + "-gpu-frames.csv";
            std::ofstream out(path);
            out << "frame,gpu_command_buffer_ms,valid\n";
            size_t validCount = 0;
            uint64_t previousTag = 0;
            for (const auto& sample : gpuTimelineSamples) {
                if (sample.tag < 115 || sample.tag > 180 ||
                    sample.tag <= previousTag) { failed = true; continue; }
                previousTag = sample.tag;
                if (sample.valid && std::isfinite(sample.milliseconds) &&
                    sample.milliseconds > 0.0) ++validCount;
                else failed = true;
                out << sample.tag << ',' << sample.milliseconds << ','
                    << (sample.valid ? 1 : 0) << '\n';
            }
            out.close();
            if (!out || gpuTimingUnsupported || validCount != 66) {
                std::fprintf(stderr,
                    "GPU command-buffer timeline INVALID: valid=%zu expected=66 unsupported=%d\n",
                    validCount, int(gpuTimingUnsupported));
                failed = true;
            } else {
                std::printf("GPU command-buffer timeline samples=%zu path=%s\n",
                            validCount, path.c_str());
            }
        }
        device->destroyBuffer(floor.vb);device->destroyBuffer(floor.ib);
        if(secondModel)render::releaseModel(*device,*secondModel);
        render::releaseModel(*device,*model);renderer.shutdown();
    };
    platform::run(cfg,cb);
    if (modelCountersUnsupported) return 77; // explicit hardware capability skip
    return failed||!finished?1:0;
}
