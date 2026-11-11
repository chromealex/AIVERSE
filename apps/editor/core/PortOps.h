#pragma once
#include "Workspace.h"

namespace aiverse::editor {
Json runProjectPort(const std::string& engineDir, const std::string& operation, const Json& params);
void registerPortOps(Workspace& ws);
}
