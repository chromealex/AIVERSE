#pragma once
#include "Workspace.h"

namespace aiverse::editor {
using StudioPlayHandler = std::function<Json(const std::string&, const Json&)>;
void registerStudioPlayTools(Workspace& workspace, const StudioPlayHandler& handler = {});
} // namespace aiverse::editor
