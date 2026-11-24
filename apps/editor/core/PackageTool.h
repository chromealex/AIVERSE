// Runs the engine's package tool (tools/pkg/aiverse_pkg.py --json) for the editor, MCP and the project
// hub: package ops, catalog templates, projects created from template packages.
#pragma once
#include <string>
#include <vector>
#include "engine/data/Json.h"

namespace aiverse::editor {
// aiverse_pkg.py --json <args>; returns its "result" or throws std::runtime_error with its "error".
// Python is AIVERSE_PYTHON, else python3 (python on Windows).
Json runPackageTool(const std::string& engineDir, const std::vector<std::string>& args);
}
