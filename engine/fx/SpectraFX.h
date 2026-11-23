// SpectraFX — AIVERSE's genre-independent visual effects system.
// Public entry for graph compilation, asset variants and CPU effect rendering.
// This is the supported public name; prior Vfx* symbols are compatibility APIs.
// A basic v1 node canvas exists in the AIVERSE Editor; full graph dataflow and
// A limited opt-in Metal compute backend is available; full Niagara dataflow
// and production GPU parity remain future work.
#pragma once
#include "VfxGraphAssets.h"
#include "SpectraModuleSchema.h"
#include "SpectraAuthoring.h"
#include "SpectraFunctionAssets.h"
#include "SpectraGpuParticles.h"
#include "SpectraGpuFramePlan.h"
#include "SpectraGpuProgram.h"
#include "SpectraGpuVmDispatch.h"
#include "SpectraGpuSourceParameters.h"
#include "SpectraGpuParticleSlots.h"
#include "SpectraGraphRuntime.h"
#include "SpectraBatchWorld.h"
#include "SpectraCueScheduler.h"
#include "SpectraGpuPoolWorld.h"
#include "SpectraGpuCueBridge.h"
#include "SpectraFxSignificance.h"
#include "SpectraFxCuePriority.h"
#include "SpectraFxAccelerationFields.h"

namespace aiverse::spectra {

// The engine-level rendering data and backend remain in aiverse::fx.
// Reuse existing CPU emitter implementation; no duplicate simulation path.
using Effect = fx::EffectDef;
using Runtime = fx::ParticleSystem;
using Handle = fx::FxHandle;
using Quality = fx::FxQuality;
using GraphAssets = fx::SpectraGraphAssets;
using GraphLoadResult = fx::SpectraGraphLoadResult;

inline constexpr const char* kName = "AIVERSE SpectraFX";
inline constexpr int kGraphVersion = 2; // latest authoring format
inline constexpr int kLegacyGraphVersion = 1; // still supported

inline bool compileGraph(const Json& graph, const Json& overrides, Effect& out,
                         std::vector<std::string>& errors) {
    return fx::compileSpectraGraph(graph, overrides, out, errors);
}
inline bool compileGraph(const Json& graph, Effect& out,
                         std::vector<std::string>& errors) {
    return fx::compileSpectraGraph(graph, out, errors);
}

} // namespace aiverse::spectra
