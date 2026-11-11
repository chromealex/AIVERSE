// MCP / editor operations for the game's own screens (ui_api, ui_screen_*, ui_validate, ui_preview).
#pragma once
#include <filesystem>
#include "Workspace.h"

namespace aiverse::editor {
void registerUiOps(Workspace& ws);
// A folder holding only engine/ (the engine assets matching the client `exe`): the client's working
// directory when AIVERSE_CONTENT points at a project (the client reads "engine/..." relative to it).
std::filesystem::path engineAssetsRoot(const Workspace& ws, const std::string& exe);
}
