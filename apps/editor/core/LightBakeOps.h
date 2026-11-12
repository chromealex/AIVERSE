// Light baking operations of the editor core (agent-lightbake). GPU-free part, works in aiversemcp too:
// the map's "bake" settings block and the bake folder next to the map (maps/<map>.bake, engine/bake).
// Baking itself needs the running editor (it captures the static scene the view draws): lightbake_bake /
// lightbake_status live in apps/editor/windows/LightBakeWindow.cpp. docs/lightbake.md.
#pragma once
#include "Workspace.h"
#include "engine/bake/BakeScene.h"

namespace aiverse::editor {

void registerLightBakeOps(Workspace& ws);
// "maps/<file>.json" of the open map (empty when the map was never saved).
std::string lightBakeMapRel(const Workspace& ws);
// Settings of the open map: its "bake" block over the defaults.
bake::BakeSettings lightBakeSettings(const Workspace& ws);
// Info about the bake folder of the open map (exists, hash, mode, pages, regions, probes, stats).
Json lightBakeInfo(const Workspace& ws);

} // namespace aiverse::editor
