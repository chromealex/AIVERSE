// Compile-only SpectraFX API smoke: every header can be included from a
// normal native game module without relying on editor- or RTS-only includes.
// Built as an OBJECT target; it deliberately does not require an RHI device.
#include "engine/fx/SpectraFX.h"
#include "engine/fx/SpectraFxCueBridge.h"
#include "engine/fx/SpectraFxCueInbox.h"
#include "engine/fx/SpectraFxCommandQueue.h"
#include "engine/fx/SpectraFxSimulationBridge.h"
#include "engine/fx/SpectraFxEmitterEvents.h"
#include "engine/fx/SpectraFxEmitterController.h"
#include "engine/fx/SpectraFxEmitterRuntime.h"
#include "engine/fx/SpectraFxEmitterAsset.h"
#include "engine/fx/SpectraFxGraph.h"
#include "engine/fx/SpectraFxGraphAsset.h"
#include "engine/fx/SpectraFxGraphRegistry.h"
#include "engine/fx/SpectraFxGraphPlayback.h"
#include "engine/fx/SpectraFxGpuSpawnScheduler.h"
#include "engine/fx/SpectraFxGpuLod.h"
#include "engine/fx/SpectraFxGpuBudgetGovernor.h"
#include "engine/fx/SpectraFxGpuParticleSlots.h"
#include "engine/fx/SpectraFxGpuCompaction.h"
#include "engine/fx/SpectraFxGpuAlphaSort.h"
#include "engine/fx/SpectraFxGpuBillboardRenderer.h"
#include "engine/fx/SpectraFxGpuImpactFeedback.h"
#include "engine/fx/SpectraFxHybridEmitter.h"
#include "engine/fx/SpectraFxGpuEmitterAsset.h"
#include "engine/fx/SpectraFxGpuEmitterLibrary.h"
#include "engine/fx/SpectraFxHybridPlayback.h"
#include "engine/fx/SpectraFxGpuRoutes.h"
#include "engine/fx/SpectraFxSceneHost.h"
#include "engine/fx/SpectraFxView.h"

// The names below must be valid public API types in a standalone TU.
namespace aiverse::fx {
void spectraFxCompileSmoke() {
    SpectraFxCueInbox inbox;
    SpectraFxGpuBudgetGovernor governor;
    SpectraFxGpuLodSettings lod;
    SpectraFxHybridEmitter::Config emitter;
    SpectraFxGpuRouteAsset::Routes routes;
    // Verify the render-only callback invalidation API is exposed to native
    // hosts without an Editor or genre module (no RHI call is made here).
    auto cancelImpacts=&SpectraFxHybridEmitter::invalidateGpuImpacts;
    (void)cancelImpacts;
    (void)inbox;
    (void)governor;
    (void)lod;
    (void)emitter;
    (void)routes;
}
} // namespace aiverse::fx

// Prove the public SpectraFX host can compile with a camera that belongs
// to no built-in genre or editor module, and with raw camera data.
namespace {
struct CinemaCamera {
    aiverse::render::float3 eye() const {return {1,2,3};}
    aiverse::render::mat4 view() const {
        return aiverse::render::mat4::lookAt({1,2,3},{0,0,0},{0,1,0});
    }
    aiverse::render::mat4 proj(float a,bool z) const {
        return aiverse::render::mat4::perspective(.75f,a,.1f,1000.0f,z);
    }
};
[[maybe_unused]] void compileGenreNeutralSceneHost(aiverse::fx::SpectraFxSceneHost& host,
                                  aiverse::fx::ParticleSystem& particles,
                                  const aiverse::render::Lighting& lighting) {
    const CinemaCamera camera{};
    const auto raw=aiverse::fx::SpectraFxView::fromCamera(
        camera,aiverse::render::mat4::identity(),1920,1080);
    host.prepareGpu(.016f,raw);
    host.prepareCpu(.016f,raw);
    (void)host.prepareRenderView(raw);
    host.render(lighting,raw);
    particles.setView(raw);
    // Previous camera-shaped APIs remain source compatible.
    host.prepareGpu(.016f,camera);
    host.prepareCpu(.016f,camera,raw.viewProj,1920,1080);
    (void)host.prepareRenderView(camera,raw.viewProj,1920,1080);
    host.render(lighting,raw.viewProj,camera);
    particles.setView(camera,raw.viewProj,1920,1080);
}
} // namespace

namespace {
[[maybe_unused]] void compilePooledGraphView(
    aiverse::spectra::BatchWorld& cpu,aiverse::spectra::GpuPoolWorld& gpu,
    aiverse::spectra::ComputeParticles& particles,aiverse::spectra::GraphRuntime& graph) {
    const CinemaCamera camera{};
    const auto view=aiverse::fx::SpectraFxView::fromCamera(
        camera,aiverse::render::mat4::identity(),1920,1080);
    cpu.setView(view); particles.setView(view); graph.setView(view);
    (void)gpu.beginPresentationFrame(view);
    cpu.setView(camera,view.viewProj,1920,1080);
    particles.setView(camera,view.viewProj,1920,1080);
    graph.setView(camera,view.viewProj,1920,1080);
    (void)gpu.beginPresentationFrame(camera,view.viewProj,1920,1080);
}
}
