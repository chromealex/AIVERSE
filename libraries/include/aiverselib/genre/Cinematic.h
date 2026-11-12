#pragma once
// cinematic-gameplay: authoritative, deterministic scene-flow policies. Time-scale domains (CinematicTime.h),
// QTE judging (CinematicQte.h), scene definitions/state (CinematicScene.h) and the per-tick runner
// (CinematicRunner.h). JSON: CinematicData.h; settings: CinematicConfigured.h; save/hash: CinematicPersistence.h.
// Presentation (audio, animation, camera) consumes SceneEvents and reports progress back as tick-stamped input.
#include "CinematicTime.h"
#include "CinematicQte.h"
#include "CinematicScene.h"
#include "CinematicRunner.h"
