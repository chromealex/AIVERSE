// Authored routes and the multiscreen block in the editor (docs/game-format.md § Routes): the map's "routes" and
// "screen" live in MapDoc `extra`. MCP ops route_list, route_set, route_delete, route_generate, screen_set; the
// Scene view draws the route lines (routePreviewLines) with the exact geometry the simulation walks.
#pragma once
#include <array>
#include <vector>
#include "Workspace.h"

namespace aiverse::editor {

void registerRouteOps(Workspace& ws);

struct RoutePreviewLine { int route = 0, line = 0; std::vector<std::array<float, 2>> pts; };
// Lines of every valid route of the open map (routes with bad data are skipped).
std::vector<RoutePreviewLine> routePreviewLines(const game::Rules& rules, const MapDoc& doc);

} // namespace aiverse::editor
