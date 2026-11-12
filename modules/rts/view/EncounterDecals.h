// Presentation of boss encounters (rules "encounters", RtsEncounters.h): telegraph progress decals on the ground
// (background shape + a fill that grows with the windup, as Drone Fall's TelegraphProgressCircle / Cone / Line,
// drawn by aiverselib TelegraphDecals.h) and status markers (rings under slowed / rooted / stunned / burning units).
// Read-only on the sim; the result is appended to the view.draw_ground batches (RtsGroundCanvas). docs/encounters.md.
#pragma once
#include <cstdint>
#include <vector>
#include "ViewCanvas.h"

namespace aiverse { class Simulation; }

namespace aiverse::game {

class RtsModule;
class RtsView;

// Appends this frame's encounter ground marks for `viewer` (0: spectator, sees everything; otherwise only what the
// viewer's fog shows; "own" telegraphs are those of the viewer's team). Nothing without rules "encounters".
void appendEncounterDecals(Simulation& sim, RtsModule& m, const RtsView& view, uint8_t viewer, std::vector<ViewGroundBatch>& out);

} // namespace aiverse::game
