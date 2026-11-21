// SpectraFX — AIVERSE's universal, native-project ECS presentation API.
// Include this in any native game module (FPS, RTS, racing, RPG, etc.).
#pragma once
#include "engine/fx/SpectraFX.h"
#include "EcsVfxPresentation.h"
#include "SpectraCueEcsAdapter.h"
#include "SpectraGpuCueEcsAdapter.h"
#include "SpectraGpuAttachmentEcsAdapter.h"
#include "SpectraEcsRenderPose.h"
#include "SpectraGpuRecoveryHost.h"
#include "SpectraGpuCaptureHost.h"
#include "SpectraGpuCaptureMailbox.h"
#include "engine/fx/SpectraDiagnosticReport.h"

namespace aiverse::spectra {

// Scene attachments and one-shot events are presentation-only. These aliases
// refer to exactly the same ECS types as the legacy Vfx* compatibility API.
using SourceComponent = lib::genre::SpectraSourceComponent;
using EventBinding = lib::genre::SpectraEventBinding;
using PresentationSystem = lib::genre::SpectraPresentationSystem;

} // namespace aiverse::spectra
