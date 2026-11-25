// Light baking operations of the editor core (agent-lightbake). GPU-free part, works in aiversemcp too:
// the map's "bake" settings block and the bake folder next to the map (maps/<map>.bake, engine/bake).
// The desktop host supplies the runtime for lightbake_bake / lightbake_status. See docs/lightbake.md.
#pragma once
#include "Workspace.h"
#include "engine/bake/BakeScene.h"

namespace aiverse::editor {

using LightBakeRuntimeHandler = std::function<Json(const std::string&,const Json&)>;
void registerLightBakeRuntimeOps(Workspace& ws,const LightBakeRuntimeHandler& handler = {});
void registerLightBakeOps(Workspace& ws);
// Project-relative path of the saved map; empty for a map outside the project.
std::string lightBakeMapRel(const Workspace& ws);
// Settings of the open map: its "bake" block over the defaults.
bake::BakeSettings lightBakeSettings(const Workspace& ws);
void applyLightBakeSettings(Workspace& ws,const bake::BakeSettings& settings);
// Info about the bake folder of the open map (exists, hash, mode, pages, regions, probes, stats).
Json lightBakeInfo(const Workspace& ws);
Json lightBakeAtlasPreview(const std::string& project,const std::string& relativeMap,const Json& params);

} // namespace aiverse::editor
